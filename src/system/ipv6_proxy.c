/**
 * @file ipv6_proxy.c
 * @brief IPv6端口转发模块实现 - 内置6tunnel代码
 *
 * 功能实现:
 * - SQLite数据库配置存储
 * - 内置6tunnel端口转发逻辑
 * - 进程管理（每规则一个进程）
 * - GLib定时器驱动Webhook发送
 * - 开机自启动处理
 *
 * 6tunnel代码来源: https://github.com/wojtekka/6tunnel
 * 版权: (C) 2000-2025 Wojtek Kaniewski <wojtekka@toxygen.net>
 * 许可: GPL-2.0
 */

#include "ipv6_proxy.h"
#include "database.h"
#include "exec_utils.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*============================================================================
 * 内部变量
 *============================================================================*/

static pthread_mutex_t g_ipv6_proxy_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_ipv6_proxy_initialized = 0;
static IPv6ProxyConfig g_current_config = {0};
static guint g_send_timer_id = 0;
static volatile int g_service_running = 0;

/* 子进程PID数组 */
static pid_t g_rule_pids[IPV6_PROXY_MAX_RULES];
static int g_rule_ids[IPV6_PROXY_MAX_RULES];
static int g_rule_count = 0;

/*============================================================================
 * 日志内存存储
 *============================================================================*/
typedef struct {
  int id;
  char ipv6_addr[64];
  char content[1024];
  char response[1024];
  int result;
  time_t created_at;
} IPv6SendLog;

#define MAX_IPV6_SEND_LOGS 30
static IPv6SendLog g_ipv6_send_logs[MAX_IPV6_SEND_LOGS];
static int g_ipv6_log_count = 0;
static int g_ipv6_log_id = 0;
static pthread_mutex_t g_ipv6_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/*============================================================================
 * 内部函数声明
 *============================================================================*/

static int create_ipv6_proxy_tables(void);
static int load_ipv6_proxy_config(void);
static void setup_send_timer(void);
static void cancel_send_timer(void);
static gboolean send_timer_callback(gpointer user_data);
static gboolean send_startup_ipv6_callback(gpointer user_data);
static void do_send_ipv6(int retry_on_fail);
static int send_webhook_notification_with_result(const char *ipv6_addr);
static void log_send_record(const char *ipv6_addr, const char *content,
                            const char *response, int result);

/* 6tunnel内置代码 */
static char *proxy_xmalloc(int size);
static char *proxy_xrealloc(char *ptr, int size);
static char *proxy_xntop(const struct sockaddr *sa);
static struct addrinfo *proxy_resolve_host(const char *name, int port,
                                           int hint);
static void proxy_make_tunnel(int rsock, int local_port);
static void proxy_rule_process(int ipv6_port, int local_port);
static void proxy_sigchld_handler(int sig);

/* IPv6防火墙规则管理 */
static void add_ipv6_firewall_rule(int port);
static void remove_ipv6_firewall_rule(int port);

/*============================================================================
 * 6tunnel内置代码 - 工具函数
 *============================================================================*/

static char *proxy_xmalloc(int size) {
  char *tmp = malloc(size);
  if (tmp == NULL) {
    perror("[IPv6Proxy] malloc");
    exit(1);
  }
  return tmp;
}

static char *proxy_xrealloc(char *ptr, int size) {
  char *tmp = realloc(ptr, size);
  if (tmp == NULL) {
    perror("[IPv6Proxy] realloc");
    exit(1);
  }
  return tmp;
}

static char *proxy_xntop(const struct sockaddr *sa) {
  char *tmp = NULL;

  if (sa->sa_family == AF_INET) {
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;
    tmp = proxy_xmalloc(INET_ADDRSTRLEN);
    if (inet_ntop(sa->sa_family, &sin->sin_addr, tmp, INET_ADDRSTRLEN) ==
        NULL) {
      free(tmp);
      tmp = NULL;
    }
  } else if (sa->sa_family == AF_INET6) {
    struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
    tmp = proxy_xmalloc(INET6_ADDRSTRLEN);
    if (inet_ntop(sa->sa_family, &sin6->sin6_addr, tmp, INET6_ADDRSTRLEN) ==
        NULL) {
      free(tmp);
      tmp = NULL;
    }
  }

  return tmp;
}

static struct addrinfo *proxy_resolve_host(const char *name, int port,
                                           int hint) {
  struct addrinfo *result = NULL;
  struct addrinfo hints;
  char port_str[16];
  int rc;

  snprintf(port_str, sizeof(port_str), "%u", port);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = hint;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = (name == NULL && port != 0) ? AI_PASSIVE : 0;

  rc = getaddrinfo(name, (port != 0) ? port_str : NULL, &hints, &result);

  if (rc == 0)
    return result;

  return NULL;
}

/*============================================================================
 * 6tunnel内置代码 - 隧道转发
 *============================================================================*/

static void proxy_make_tunnel(int rsock, int local_port) {
  char buf[4096];
  char *outbuf = NULL, *inbuf = NULL;
  int sock = -1, outlen = 0, inlen = 0;
  struct addrinfo *connect_ai = NULL;
  struct addrinfo *ai_ptr;

  /* 连接到本地端口 */
  connect_ai = proxy_resolve_host("127.0.0.1", local_port, AF_INET);

  if (connect_ai == NULL) {
    printf("[IPv6Proxy] 无法解析本地地址 127.0.0.1:%d\n", local_port);
    goto cleanup;
  }

  for (ai_ptr = connect_ai; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next) {
    sock = socket(ai_ptr->ai_family, ai_ptr->ai_socktype, 0);

    if (sock == -1) {
      if (ai_ptr->ai_next != NULL)
        continue;
      printf("[IPv6Proxy] 无法创建socket\n");
      goto cleanup;
    }

    if (connect(sock, ai_ptr->ai_addr, ai_ptr->ai_addrlen) != -1)
      break;

    if (ai_ptr->ai_next == NULL) {
      printf("[IPv6Proxy] 连接到本地端口%d失败\n", local_port);
      goto cleanup;
    }

    close(sock);
    sock = -1;
  }

  freeaddrinfo(connect_ai);
  connect_ai = NULL;

  /* 数据转发循环 */
  for (;;) {
    fd_set rds, wds;
    int ret, sent;

    FD_ZERO(&rds);
    FD_SET(sock, &rds);
    FD_SET(rsock, &rds);

    FD_ZERO(&wds);
    if (outbuf && outlen)
      FD_SET(rsock, &wds);
    if (inbuf && inlen)
      FD_SET(sock, &wds);

    ret = select((sock > rsock) ? (sock + 1) : (rsock + 1), &rds, &wds, NULL,
                 NULL);

    if (ret < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    /* 写入到客户端 */
    if (FD_ISSET(rsock, &wds)) {
      sent = write(rsock, outbuf, outlen);

      if (sent < 1)
        goto cleanup;

      if (sent == outlen) {
        free(outbuf);
        outbuf = NULL;
        outlen = 0;
      } else {
        memmove(outbuf, outbuf + sent, outlen - sent);
        outlen -= sent;
      }
    }

    /* 写入到服务器 */
    if (FD_ISSET(sock, &wds)) {
      sent = write(sock, inbuf, inlen);

      if (sent < 1)
        goto cleanup;

      if (sent == inlen) {
        free(inbuf);
        inbuf = NULL;
        inlen = 0;
      } else {
        memmove(inbuf, inbuf + sent, inlen - sent);
        inlen -= sent;
      }
    }

    /* 从服务器读取 */
    if (FD_ISSET(sock, &rds)) {
      ret = read(sock, buf, sizeof(buf));
      if (ret < 1)
        goto cleanup;

      sent = write(rsock, buf, ret);

      if (sent < 1)
        goto cleanup;

      if (sent < ret) {
        outbuf = proxy_xrealloc(outbuf, outlen + ret - sent);
        memcpy(outbuf + outlen, buf + sent, ret - sent);
        outlen = ret - sent;
      }
    }

    /* 从客户端读取 */
    if (FD_ISSET(rsock, &rds)) {
      ret = read(rsock, buf, sizeof(buf));
      if (ret < 1)
        goto cleanup;

      sent = write(sock, buf, ret);

      if (sent < 1)
        goto cleanup;

      if (sent < ret) {
        inbuf = proxy_xrealloc(inbuf, inlen + ret - sent);
        memcpy(inbuf + inlen, buf + sent, ret - sent);
        inlen = ret - sent;
      }
    }
  }

cleanup:
  if (connect_ai != NULL)
    freeaddrinfo(connect_ai);

  if (outbuf)
    free(outbuf);
  if (inbuf)
    free(inbuf);

  close(rsock);

  if (sock != -1)
    close(sock);
}

/*============================================================================
 * 6tunnel内置代码 - 规则进程
 *============================================================================*/

static void proxy_sigchld_handler(int sig) {
  (void)sig;
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;
  signal(SIGCHLD, proxy_sigchld_handler);
}

static void proxy_rule_process(int ipv6_port, int local_port) {
  int listen_fd, jeden = 1;
  struct addrinfo *ai;
  struct addrinfo *ai_ptr;

  printf("[IPv6Proxy] 规则进程启动: IPv6端口%d -> 本地端口%d\n", ipv6_port,
         local_port);

  /* 解析IPv6监听地址 */
  ai = proxy_resolve_host(NULL, ipv6_port, AF_INET6);

  if (ai == NULL) {
    printf("[IPv6Proxy] 无法解析监听地址\n");
    exit(1);
  }

  /* 创建socket */
  listen_fd = socket(ai->ai_family, ai->ai_socktype, 0);

  if (listen_fd == -1) {
    perror("[IPv6Proxy] socket");
    freeaddrinfo(ai);
    exit(1);
  }

  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &jeden, sizeof(jeden)) ==
      -1) {
    perror("[IPv6Proxy] setsockopt");
    close(listen_fd);
    freeaddrinfo(ai);
    exit(1);
  }

  /* 绑定 */
  for (ai_ptr = ai; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next) {
    if (bind(listen_fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen) == 0)
      break;
    if (ai_ptr->ai_next == NULL) {
      perror("[IPv6Proxy] bind");
      close(listen_fd);
      freeaddrinfo(ai);
      exit(1);
    }
  }

  freeaddrinfo(ai);

  /* 监听 */
  if (listen(listen_fd, 100) == -1) {
    perror("[IPv6Proxy] listen");
    close(listen_fd);
    exit(1);
  }

  printf("[IPv6Proxy] 开始监听IPv6端口 %d\n", ipv6_port);

  /* 设置子进程信号处理 */
  signal(SIGCHLD, proxy_sigchld_handler);

  /* 主循环 */
  for (;;) {
    int client_fd, ret;
    struct sockaddr_in6 sin6;
    socklen_t sin6_len = sizeof(sin6);
    fd_set rds;

    FD_ZERO(&rds);
    FD_SET(listen_fd, &rds);

    if (select(listen_fd + 1, &rds, NULL, NULL, NULL) == -1) {
      if (errno == EINTR)
        continue;
      perror("[IPv6Proxy] select");
      break;
    }

    client_fd = accept(listen_fd, (struct sockaddr *)&sin6, &sin6_len);

    if (client_fd == -1) {
      if (errno == EINTR)
        continue;
      perror("[IPv6Proxy] accept");
      break;
    }

    char *client_addr = proxy_xntop((struct sockaddr *)&sin6);
    printf("[IPv6Proxy] 新连接来自 %s\n",
           client_addr ? client_addr : "unknown");

    /* fork处理连接 */
    ret = fork();

    if (ret == -1) {
      printf("[IPv6Proxy] fork失败\n");
      close(client_fd);
      if (client_addr)
        free(client_addr);
      continue;
    }

    if (ret == 0) {
      /* 子进程处理连接 */
      close(listen_fd);
      proxy_make_tunnel(client_fd, local_port);
      if (client_addr)
        free(client_addr);
      exit(0);
    }

    /* 父进程 */
    close(client_fd);
    if (client_addr)
      free(client_addr);
  }

  close(listen_fd);
  exit(0);
}

/*============================================================================
 * 数据库表创建
 *============================================================================*/

static int create_ipv6_proxy_tables(void) {
  const char *sql1 = "CREATE TABLE IF NOT EXISTS ipv6_proxy_config ("
                     "id INTEGER PRIMARY KEY DEFAULT 1,"
                     "enabled INTEGER DEFAULT 0,"
                     "auto_start INTEGER DEFAULT 0,"
                     "send_enabled INTEGER DEFAULT 0,"
                     "send_interval INTEGER DEFAULT 60,"
                     "webhook_url TEXT,"
                     "webhook_body TEXT,"
                     "webhook_headers TEXT"
                     ");";

  const char *sql2 = "CREATE TABLE IF NOT EXISTS ipv6_proxy_rules ("
                     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "local_port INTEGER NOT NULL,"
                     "ipv6_port INTEGER NOT NULL,"
                     "enabled INTEGER DEFAULT 1,"
                     "created_at INTEGER NOT NULL"
                     ");";

  /* 发送日志表 */
  const char *sql3 = "CREATE TABLE IF NOT EXISTS ipv6_send_log ("
                     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "ipv6_addr TEXT,"
                     "content TEXT,"
                     "result INTEGER DEFAULT 0,"
                     "created_at INTEGER NOT NULL"
                     ");";

  int ret1 = db_execute(sql1);
  if (ret1 != 0) {
    printf("[IPv6Proxy] 创建配置表失败 (ret=%d)\n", ret1);
    return ret1;
  }

  int ret2 = db_execute(sql2);
  if (ret2 != 0) {
    printf("[IPv6Proxy] 创建规则表失败 (ret=%d)\n", ret2);
    return ret2;
  }

  int ret3 = db_execute(sql3);
  if (ret3 != 0) {
    printf("[IPv6Proxy] 创建发送日志表失败 (ret=%d)\n", ret3);
    return ret3;
  }

  printf("[IPv6Proxy] 数据库表创建/验证成功\n");
  return 0;
}

/*============================================================================
 * 配置加载
 *============================================================================*/

static int load_ipv6_proxy_config(void) {
  char output[4096];
  const char *sql = "SELECT enabled, auto_start, send_enabled, send_interval, "
                    "webhook_url, webhook_body, webhook_headers "
                    "FROM ipv6_proxy_config WHERE id = 1;";

  pthread_mutex_lock(&g_ipv6_proxy_mutex);
  int ret = db_query_rows(sql, "|", output, sizeof(output));
  pthread_mutex_unlock(&g_ipv6_proxy_mutex);

  if (ret != 0 || strlen(output) == 0) {
    /* 默认配置 */
    memset(&g_current_config, 0, sizeof(g_current_config));
    g_current_config.send_interval = 60;
    strcpy(g_current_config.webhook_body,
           "{\"ipv6\":\"#{ipv6}\",\"link\":\"#{link}\",\"time\":\"#{time}\"}");
    return 0;
  }

  /* 解析输出 */
  char *fields[7] = {NULL};
  int field_count = 0;
  char *p = output;
  char *start = p;

  while (*p && field_count < 7) {
    if (*p == '|') {
      *p = '\0';
      fields[field_count++] = start;
      start = p + 1;
    }
    p++;
  }
  if (field_count < 7 && start) {
    fields[field_count++] = start;
  }

  if (field_count >= 7) {
    g_current_config.enabled = atoi(fields[0]);
    g_current_config.auto_start = atoi(fields[1]);
    g_current_config.send_enabled = atoi(fields[2]);
    g_current_config.send_interval = atoi(fields[3]);
    strncpy(g_current_config.webhook_url, fields[4],
            sizeof(g_current_config.webhook_url) - 1);
    strncpy(g_current_config.webhook_body, fields[5],
            sizeof(g_current_config.webhook_body) - 1);
    strncpy(g_current_config.webhook_headers, fields[6],
            sizeof(g_current_config.webhook_headers) - 1);

    /* 去除末尾换行符 */
    char *nl = strchr(g_current_config.webhook_headers, '\n');
    if (nl)
      *nl = '\0';

    /* 反转义 */
    db_unescape_string(g_current_config.webhook_url);
    db_unescape_string(g_current_config.webhook_body);
    db_unescape_string(g_current_config.webhook_headers);
  }

  printf("[IPv6Proxy] 配置加载完成: 启用=%d, 自启动=%d, 发送=%d, 间隔=%d分钟\n",
         g_current_config.enabled, g_current_config.auto_start,
         g_current_config.send_enabled, g_current_config.send_interval);
  return 0;
}

/*============================================================================
 * 定时器管理
 *============================================================================*/

static gboolean send_timer_callback(gpointer user_data) {
  (void)user_data;
  printf("[IPv6Proxy] 定时器触发，发送IPv6地址\n");
  do_send_ipv6(1); /* 失败重试 */
  return G_SOURCE_CONTINUE;
}

/* 开机延迟发送回调（一次性） */
static gboolean send_startup_ipv6_callback(gpointer user_data) {
  (void)user_data;
  printf("[IPv6Proxy] 设备已启动15秒，开始发送IPv6地址\n");
  do_send_ipv6(1);        /* 失败重试 */
  return G_SOURCE_REMOVE; /* 只执行一次 */
}

static void setup_send_timer(void) {
  cancel_send_timer();

  if (!g_current_config.send_enabled || g_current_config.send_interval <= 0) {
    return;
  }

  int interval_seconds = g_current_config.send_interval * 60;
  g_send_timer_id =
      g_timeout_add_seconds(interval_seconds, send_timer_callback, NULL);
  printf("[IPv6Proxy] 定时器已设置: 每%d分钟发送一次\n",
         g_current_config.send_interval);
}

static void cancel_send_timer(void) {
  if (g_send_timer_id > 0) {
    g_source_remove(g_send_timer_id);
    g_send_timer_id = 0;
    printf("[IPv6Proxy] 定时器已取消\n");
  }
}

/*============================================================================
 * Webhook发送
 *============================================================================*/

/* 带返回值的Webhook发送函数 */
static int send_webhook_notification_with_result(const char *ipv6_addr) {
  if (strlen(g_current_config.webhook_url) == 0) {
    printf("[IPv6Proxy] Webhook URL未配置\n");
    return -1;
  }

  printf("[IPv6Proxy] 发送Webhook到: %s\n", g_current_config.webhook_url);

  /* 替换变量 */
  char body[4096];
  strncpy(body, g_current_config.webhook_body, sizeof(body) - 1);
  body[sizeof(body) - 1] = '\0';

  char *p;
  char temp[4096];

  /* 替换 #{ipv6} */
  while ((p = strstr(body, "#{ipv6}")) != NULL) {
    *p = '\0';
    snprintf(temp, sizeof(temp), "%s%s%s", body, ipv6_addr, p + 7);
    strncpy(body, temp, sizeof(body) - 1);
  }

  /* 兼容性对比 #{sender} -> #{ipv6} */
  while ((p = strstr(body, "#{sender}")) != NULL) {
    *p = '\0';
    snprintf(temp, sizeof(temp), "%s%s%s", body, ipv6_addr, p + 9);
    strncpy(body, temp, sizeof(body) - 1);
  }

  /* 替换 #{port} - 获取所有端口列表 */
  IPv6ProxyRule rules[IPV6_PROXY_MAX_RULES];
  int rule_count = ipv6_proxy_rule_list(rules, IPV6_PROXY_MAX_RULES);
  char ports_str[256] = "port";
  if (rule_count > 0) {
    int offset = 0;
    for (int i = 0; i < rule_count && offset < (int)sizeof(ports_str) - 16;
         i++) {
      if (rules[i].enabled) {
        if (offset > 0) {
          offset += snprintf(ports_str + offset, sizeof(ports_str) - offset,
                             ",%d", rules[i].ipv6_port);
        } else {
          offset += snprintf(ports_str + offset, sizeof(ports_str) - offset,
                             "%d", rules[i].ipv6_port);
        }
      }
    }
  }
  while ((p = strstr(body, "#{port}")) != NULL) {
    *p = '\0';
    snprintf(temp, sizeof(temp), "%s%s%s", body, ports_str, p + 7);
    strncpy(body, temp, sizeof(body) - 1);
  }

  /* 替换 #{link} */
  char link[1024] = "";
  if (rule_count > 0) {
    int link_offset = 0;
    for (int i = 0; i < rule_count && link_offset < (int)sizeof(link) - 128;
         i++) {
      if (rules[i].enabled) {
        if (link_offset > 0) {
          link_offset +=
              snprintf(link + link_offset, sizeof(link) - link_offset,
                       "\\n[%s]:%d", ipv6_addr, rules[i].ipv6_port);
        } else {
          link_offset +=
              snprintf(link + link_offset, sizeof(link) - link_offset,
                       "[%s]:%d", ipv6_addr, rules[i].ipv6_port);
        }
      }
    }
  } else {
    snprintf(link, sizeof(link), "[%s]:port", ipv6_addr);
  }
  while ((p = strstr(body, "#{link}")) != NULL) {
    *p = '\0';
    snprintf(temp, sizeof(temp), "%s%s%s", body, link, p + 7);
    strncpy(body, temp, sizeof(body) - 1);
  }

  /* 替换 #{time} */
  char time_str[32];
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
  while ((p = strstr(body, "#{time}")) != NULL) {
    *p = '\0';
    snprintf(temp, sizeof(temp), "%s%s%s", body, time_str, p + 7);
    strncpy(body, temp, sizeof(body) - 1);
  }

  /* 将body写入临时文件 */
  char tmp_file[128];
  snprintf(tmp_file, sizeof(tmp_file), "/tmp/ipv6_webhook_%d.json",
           (int)getpid());
  FILE *fp = fopen(tmp_file, "w");
  if (fp) {
    fputs(body, fp);
    fclose(fp);
  } else {
    printf("[IPv6Proxy] 无法创建临时文件\n");
    return -1;
  }

  /* 构建curl命令 */
  char cmd[8192];
  char headers_part[1024] = "";

  /* 解析自定义headers */
  if (strlen(g_current_config.webhook_headers) > 0) {
    char headers_copy[1024];
    strncpy(headers_copy, g_current_config.webhook_headers,
            sizeof(headers_copy) - 1);
    headers_copy[sizeof(headers_copy) - 1] = '\0';

    char *saveptr;
    char *line = strtok_r(headers_copy, "\n", &saveptr);
    while (line) {
      while (*line == ' ' || *line == '\r')
        line++;
      if (strlen(line) > 0 && strchr(line, ':')) {
        char header_arg[256];
        char *cr = strchr(line, '\r');
        if (cr)
          *cr = '\0';
        snprintf(header_arg, sizeof(header_arg), " -H '%s'", line);
        strncat(headers_part, header_arg,
                sizeof(headers_part) - strlen(headers_part) - 1);
      }
      line = strtok_r(NULL, "\n", &saveptr);
    }
  }

  /* 同步执行curl获取响应 */
  if (strstr(headers_part, "Content-Type") == NULL) {
    snprintf(cmd, sizeof(cmd),
             "curl -s --max-time 10 -X POST '%s' -H 'Content-Type: "
             "application/json'%s -d @%s 2>&1",
             g_current_config.webhook_url, headers_part, tmp_file);
  } else {
    snprintf(cmd, sizeof(cmd),
             "curl -s --max-time 10 -X POST '%s'%s -d @%s 2>&1",
             g_current_config.webhook_url, headers_part, tmp_file);
  }

  printf("[IPv6Proxy] 执行: %s\n", cmd);

  /* 使用popen捕获响应 */
  char response[1024] = "";
  FILE *pipe = popen(cmd, "r");
  if (pipe) {
    size_t total = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe) && total < sizeof(response) - 1) {
      size_t len = strlen(buf);
      if (total + len < sizeof(response)) {
        strcat(response, buf);
        total += len;
      }
    }
    pclose(pipe);
  } else {
    strncpy(response, "执行curl失败", sizeof(response) - 1);
  }

  /* 删除临时文件 */
  unlink(tmp_file);

  /* 判断是否成功 - 检查curl错误和HTTP错误 */
  int result = 0;
  if (strlen(response) > 0 && strstr(response, "curl:") == NULL &&
      strstr(response, "Could not resolve") == NULL &&
      strstr(response, "Connection refused") == NULL &&
      strstr(response, "Connection timed out") == NULL) {
    result = 1;
  }

  printf("[IPv6Proxy] Webhook响应: %s, 结果: %s\n", response,
         result ? "成功" : "失败");

  /* 记录日志 */
  log_send_record(ipv6_addr, body, response, result);

  return result ? 0 : -1;
}

static void do_send_ipv6(int retry_on_fail) {
  char ipv6_addr[64] = {0};
  int max_retries =
      retry_on_fail ? 30 : 1; /* 重试模式最多30次，每次10秒，共5分钟 */
  int retry_count = 0;

  while (retry_count < max_retries) {
    /* 获取IPv6地址 */
    if (ipv6_proxy_get_ipv6_addr(ipv6_addr, sizeof(ipv6_addr)) != 0 ||
        strlen(ipv6_addr) == 0) {
      printf("[IPv6Proxy] 获取IPv6地址失败，等待10秒后重试 (%d/%d)\n",
             retry_count + 1, max_retries);
      if (retry_on_fail && retry_count < max_retries - 1) {
        sleep(10);
        retry_count++;
        continue;
      }
      return;
    }

    printf("[IPv6Proxy] 当前IPv6地址: %s\n", ipv6_addr);

    /* 尝试发送 */
    if (send_webhook_notification_with_result(ipv6_addr) == 0) {
      printf("[IPv6Proxy] Webhook发送成功\n");
      return;
    }

    /* 发送失败，重试 */
    if (retry_on_fail && retry_count < max_retries - 1) {
      printf("[IPv6Proxy] 发送失败，10秒后重试 (%d/%d)\n", retry_count + 1,
             max_retries);
      sleep(10);
      retry_count++;
    } else {
      printf("[IPv6Proxy] 发送失败，不再重试\n");
      return;
    }
  }
}

/*============================================================================
 * 初始化和清理
 *============================================================================*/

int ipv6_proxy_init(const char *db_path) {
  if (g_ipv6_proxy_initialized) {
    return 0;
  }

  printf("[IPv6Proxy] 初始化模块\n");

  /* 初始化数据库 */
  if (db_path && strlen(db_path) > 0) {
    db_init(db_path);
  }

  /* 创建数据库表 */
  if (create_ipv6_proxy_tables() != 0) {
    printf("[IPv6Proxy] 创建数据库表失败\n");
    return -1;
  }

  /* 创建PID目录 */
  mkdir(IPV6_PROXY_PID_DIR, 0755);

  /* 初始化PID数组 */
  memset(g_rule_pids, 0, sizeof(g_rule_pids));
  memset(g_rule_ids, 0, sizeof(g_rule_ids));
  g_rule_count = 0;

  /* 初始化日志计数 */
  pthread_mutex_lock(&g_ipv6_log_mutex);
  g_ipv6_log_count = 0;
  g_ipv6_log_id = 0;
  pthread_mutex_unlock(&g_ipv6_log_mutex);

  /* 加载配置 */
  load_ipv6_proxy_config();

  /* 处理自启动 */
  if (g_current_config.enabled && g_current_config.auto_start) {
    printf("[IPv6Proxy] 检测到自启动配置，正在启动服务...\n");
    if (ipv6_proxy_start() == 0) {
      printf("[IPv6Proxy] 自启动成功\n");
    } else {
      printf("[IPv6Proxy] 自启动失败\n");
    }
  }

  /* 设置定时器 */
  setup_send_timer();

  /* 开机发送一次（延迟15秒，等待设备完全启动） */
  if (g_current_config.send_enabled &&
      strlen(g_current_config.webhook_url) > 0) {
    printf("[IPv6Proxy] 开机后15秒发送IPv6地址\n");
    g_timeout_add_seconds(15, (GSourceFunc)send_startup_ipv6_callback, NULL);
  }

  g_ipv6_proxy_initialized = 1;
  printf("[IPv6Proxy] 模块初始化完成\n");
  return 0;
}

void ipv6_proxy_deinit(void) {
  if (!g_ipv6_proxy_initialized) {
    return;
  }

  /* 取消定时器 */
  cancel_send_timer();

  /* 停止服务 */
  ipv6_proxy_stop();

  g_ipv6_proxy_initialized = 0;
  printf("[IPv6Proxy] 模块已清理\n");
}

/*============================================================================
 * 配置管理
 *============================================================================*/

int ipv6_proxy_get_config(IPv6ProxyConfig *config) {
  if (!config) {
    return -1;
  }

  pthread_mutex_lock(&g_ipv6_proxy_mutex);
  memcpy(config, &g_current_config, sizeof(IPv6ProxyConfig));
  pthread_mutex_unlock(&g_ipv6_proxy_mutex);

  return 0;
}

int ipv6_proxy_set_config(const IPv6ProxyConfig *config) {
  char sql[8192];
  char escaped_url[1024];
  char escaped_body[4096];
  char escaped_headers[1024];

  if (!config) {
    return -1;
  }

  db_escape_string(config->webhook_url, escaped_url, sizeof(escaped_url));
  db_escape_string(config->webhook_body, escaped_body, sizeof(escaped_body));
  db_escape_string(config->webhook_headers, escaped_headers,
                   sizeof(escaped_headers));

  /* 开启自启动时强制启用服务 */
  int final_enabled = config->enabled;
  if (config->auto_start) {
    final_enabled = 1;
  }

  snprintf(sql, sizeof(sql),
           "INSERT OR REPLACE INTO ipv6_proxy_config "
           "(id, enabled, auto_start, send_enabled, send_interval, "
           "webhook_url, webhook_body, webhook_headers) "
           "VALUES (1, %d, %d, %d, %d, '%s', '%s', '%s');",
           final_enabled, config->auto_start, config->send_enabled,
           config->send_interval, escaped_url, escaped_body, escaped_headers);

  pthread_mutex_lock(&g_ipv6_proxy_mutex);
  int ret = db_execute(sql);
  pthread_mutex_unlock(&g_ipv6_proxy_mutex);

  if (ret != 0) {
    printf("[IPv6Proxy] 保存配置失败\n");
    return -1;
  }

  /* 更新内存配置 - 使用数据库实际保存的值 */
  memcpy(&g_current_config, config, sizeof(IPv6ProxyConfig));
  g_current_config.enabled = final_enabled; /* 使用实际保存的值 */

  /* 重新设置定时器 */
  setup_send_timer();

  printf("[IPv6Proxy] 配置保存成功\n");
  return 0;
}

/*============================================================================
 * 规则管理
 *============================================================================*/

int ipv6_proxy_rule_list(IPv6ProxyRule *rules, int max_count) {
  char *output = NULL;

  if (!rules || max_count <= 0) {
    return -1;
  }

  output = (char *)malloc(32 * 1024);
  if (!output) {
    return -1;
  }

  const char *sql =
      "SELECT id || '|' || local_port || '|' || ipv6_port || '|' || "
      "enabled || '|' || created_at FROM ipv6_proxy_rules ORDER BY id ASC;";

  pthread_mutex_lock(&g_ipv6_proxy_mutex);
  int ret = db_query_string(sql, output, 32 * 1024);
  pthread_mutex_unlock(&g_ipv6_proxy_mutex);

  if (ret != 0 || strlen(output) == 0) {
    free(output);
    return 0;
  }

  /* 解析输出 */
  int count = 0;
  char *line = output;
  char *next_line;

  while (line && *line && count < max_count) {
    next_line = strchr(line, '\n');
    if (next_line) {
      *next_line = '\0';
      next_line++;
    }

    if (strlen(line) == 0) {
      line = next_line;
      continue;
    }

    /* 解析字段 */
    char *fields[5] = {NULL};
    int field_count = 0;
    char *p = line;
    char *start = p;

    while (*p && field_count < 5) {
      if (*p == '|') {
        *p = '\0';
        fields[field_count++] = start;
        start = p + 1;
      }
      p++;
    }
    if (field_count < 5 && start) {
      fields[field_count++] = start;
    }

    if (field_count >= 5) {
      rules[count].id = atoi(fields[0]);
      rules[count].local_port = atoi(fields[1]);
      rules[count].ipv6_port = atoi(fields[2]);
      rules[count].enabled = atoi(fields[3]);
      rules[count].created_at = (time_t)atol(fields[4]);
      count++;
    }

    line = next_line;
  }

  free(output);
  printf("[IPv6Proxy] 获取到 %d 条规则\n", count);
  return count;
}

int ipv6_proxy_rule_add(int local_port, int ipv6_port) {
  char sql[512];

  if (local_port <= 0 || local_port > 65535 || ipv6_port <= 0 ||
      ipv6_port > 65535) {
    printf("[IPv6Proxy] 端口参数无效\n");
    return -1;
  }

  time_t now = time(NULL);

  snprintf(sql, sizeof(sql),
           "INSERT INTO ipv6_proxy_rules (local_port, ipv6_port, enabled, "
           "created_at) "
           "VALUES (%d, %d, 1, %ld);",
           local_port, ipv6_port, (long)now);

  pthread_mutex_lock(&g_ipv6_proxy_mutex);
  int ret = db_execute(sql);
  pthread_mutex_unlock(&g_ipv6_proxy_mutex);

  if (ret == 0) {
    /* 查询最大ID作为新插入的ID
     * (因为每次sqlite3调用是独立会话,last_insert_rowid不可用) */
    int new_id = db_query_int("SELECT MAX(id) FROM ipv6_proxy_rules;", 1);
    printf("[IPv6Proxy] 规则添加成功: ID=%d, %d -> %d\n", new_id, ipv6_port,
           local_port);
    return new_id > 0 ? new_id : 1; /* 确保返回正数 */
  }

  printf("[IPv6Proxy] 规则添加失败 (ret=%d)\n", ret);
  return -1;
}

int ipv6_proxy_rule_update(int id, int local_port, int ipv6_port, int enabled) {
  char sql[512];

  if (id <= 0 || local_port <= 0 || local_port > 65535 || ipv6_port <= 0 ||
      ipv6_port > 65535) {
    return -1;
  }

  snprintf(sql, sizeof(sql),
           "UPDATE ipv6_proxy_rules SET local_port=%d, ipv6_port=%d, "
           "enabled=%d WHERE id=%d;",
           local_port, ipv6_port, enabled ? 1 : 0, id);

  pthread_mutex_lock(&g_ipv6_proxy_mutex);
  int ret = db_execute(sql);
  pthread_mutex_unlock(&g_ipv6_proxy_mutex);

  if (ret == 0) {
    printf("[IPv6Proxy] 规则更新成功: ID=%d\n", id);
  }

  return ret;
}

int ipv6_proxy_rule_delete(int id) {
  char sql[128];

  if (id <= 0) {
    return -1;
  }

  snprintf(sql, sizeof(sql), "DELETE FROM ipv6_proxy_rules WHERE id = %d;", id);

  pthread_mutex_lock(&g_ipv6_proxy_mutex);
  int ret = db_execute(sql);
  pthread_mutex_unlock(&g_ipv6_proxy_mutex);

  if (ret == 0) {
    printf("[IPv6Proxy] 规则删除成功: ID=%d\n", id);
  }

  return ret;
}

/*============================================================================
 * IPv6防火墙规则管理
 *============================================================================*/

static void add_ipv6_firewall_rule(int port) {
  char cmd[256];
  /* 先检查规则是否存在，不存在则添加 */
  snprintf(cmd, sizeof(cmd),
           "ip6tables -C INPUT -p tcp --dport %d -j ACCEPT 2>/dev/null || "
           "ip6tables -A INPUT -p tcp --dport %d -j ACCEPT",
           port, port);
  int ret = system(cmd);
  if (ret == 0) {
    printf("[IPv6Proxy] 添加防火墙规则: 端口 %d\n", port);
  } else {
    printf("[IPv6Proxy] 添加防火墙规则失败: 端口 %d (ret=%d)\n", port, ret);
  }
}

static void remove_ipv6_firewall_rule(int port) {
  char cmd[256];
  snprintf(cmd, sizeof(cmd),
           "ip6tables -D INPUT -p tcp --dport %d -j ACCEPT 2>/dev/null", port);
  int ret = system(cmd);
  printf("[IPv6Proxy] 删除防火墙规则: 端口 %d (ret=%d)\n", port, ret);
}

/*============================================================================
 * 服务控制
 *============================================================================*/

int ipv6_proxy_start(void) {
  IPv6ProxyRule rules[IPV6_PROXY_MAX_RULES];
  int count;

  if (g_service_running) {
    printf("[IPv6Proxy] 服务已在运行中\n");
    return 0;
  }

  /* 获取规则列表 */
  count = ipv6_proxy_rule_list(rules, IPV6_PROXY_MAX_RULES);
  if (count <= 0) {
    printf("[IPv6Proxy] 没有配置任何转发规则\n");
    return -1;
  }

  printf("[IPv6Proxy] 启动服务，共 %d 条规则\n", count);

  /* 打印所有规则用于调试 */
  for (int i = 0; i < count; i++) {
    printf("[IPv6Proxy] 规则[%d]: id=%d, ipv6_port=%d, local_port=%d, "
           "enabled=%d\n",
           i, rules[i].id, rules[i].ipv6_port, rules[i].local_port,
           rules[i].enabled);
  }

  /* 为每条启用的规则fork一个子进程 */
  g_rule_count = 0;

  for (int i = 0; i < count; i++) {
    if (!rules[i].enabled) {
      printf("[IPv6Proxy] 规则 %d 未启用，跳过\n", rules[i].id);
      continue;
    }

    /* 保存当前规则的端口信息 */
    int cur_ipv6_port = rules[i].ipv6_port;
    int cur_local_port = rules[i].local_port;
    int cur_id = rules[i].id;

    printf("[IPv6Proxy] 准备启动规则 %d: IPv6端口 %d -> 本地端口 %d\n", cur_id,
           cur_ipv6_port, cur_local_port);

    pid_t pid = fork();

    if (pid == -1) {
      printf("[IPv6Proxy] fork失败: %s\n", strerror(errno));
      continue;
    }

    if (pid == 0) {
      /* 子进程 - 使用保存的局部变量 */
      printf("[IPv6Proxy] 子进程启动 (PID=%d): IPv6端口 %d -> 本地端口 %d\n",
             getpid(), cur_ipv6_port, cur_local_port);
      proxy_rule_process(cur_ipv6_port, cur_local_port);
      exit(0); /* 不应该到达这里 */
    }

    /* 父进程记录PID */
    g_rule_pids[g_rule_count] = pid;
    g_rule_ids[g_rule_count] = cur_id;
    g_rule_count++;

    printf("[IPv6Proxy] 规则 %d 启动，PID=%d\n", cur_id, pid);

    /* 添加IPv6防火墙规则 */
    add_ipv6_firewall_rule(cur_ipv6_port);

    /* 短暂延迟，确保子进程开始监听 */
    usleep(100000); /* 100ms */
  }

  g_service_running = 1;
  printf("[IPv6Proxy] 服务启动完成，共启动 %d 个进程\n", g_rule_count);
  return 0;
}

int ipv6_proxy_stop(void) {
  if (!g_service_running && g_rule_count == 0) {
    printf("[IPv6Proxy] 服务未运行\n");
    return 0;
  }

  printf("[IPv6Proxy] 停止服务\n");

  /* 获取规则列表以删除防火墙规则 */
  IPv6ProxyRule rules[IPV6_PROXY_MAX_RULES];
  int count = ipv6_proxy_rule_list(rules, IPV6_PROXY_MAX_RULES);

  /* 删除所有启用规则的防火墙规则 */
  for (int i = 0; i < count; i++) {
    if (rules[i].enabled) {
      remove_ipv6_firewall_rule(rules[i].ipv6_port);
    }
  }

  /* 直接强制终止所有子进程 */
  for (int i = 0; i < g_rule_count; i++) {
    if (g_rule_pids[i] > 0) {
      printf("[IPv6Proxy] 强制终止进程 PID=%d\n", g_rule_pids[i]);
      kill(g_rule_pids[i], SIGKILL);
      waitpid(g_rule_pids[i], NULL, WNOHANG);
    }
  }

  /* 短暂等待确保进程退出 */
  usleep(100000); /* 100ms */

  /* 清理状态 */
  memset(g_rule_pids, 0, sizeof(g_rule_pids));
  memset(g_rule_ids, 0, sizeof(g_rule_ids));
  g_rule_count = 0;
  g_service_running = 0;

  printf("[IPv6Proxy] 服务已停止\n");
  return 0;
}

int ipv6_proxy_restart(void) {
  ipv6_proxy_stop();
  usleep(500000); /* 500ms */
  return ipv6_proxy_start();
}

int ipv6_proxy_get_status(IPv6ProxyStatus *status) {
  if (status) {
    memset(status, 0, sizeof(IPv6ProxyStatus));
    status->running = g_service_running;
    status->active_count = g_rule_count;

    /* 获取规则总数 */
    const char *sql = "SELECT COUNT(*) FROM ipv6_proxy_rules;";
    status->rule_count = db_query_int(sql, 0);

    /* 获取IPv6地址 */
    ipv6_proxy_get_ipv6_addr(status->ipv6_addr, sizeof(status->ipv6_addr));
  }

  return g_service_running;
}

/*============================================================================
 * IPv6地址获取
 *============================================================================*/

int ipv6_proxy_get_ipv6_addr(char *addr, size_t size) {
  char output[2048] = {0};
  char cmd[] = "ip -6 addr show scope global | grep inet6 | awk '{print $2}' | "
               "cut -d'/' -f1 | head -n1";

  if (!addr || size == 0) {
    return -1;
  }

  addr[0] = '\0';

  if (run_command(output, sizeof(output), "sh", "-c", cmd, NULL) != 0) {
    printf("[IPv6Proxy] 获取IPv6地址命令执行失败\n");
    return -1;
  }

  /* 去除换行符 */
  char *nl = strchr(output, '\n');
  if (nl)
    *nl = '\0';

  if (strlen(output) > 0) {
    strncpy(addr, output, size - 1);
    addr[size - 1] = '\0';
  }

  return 0;
}

/*============================================================================
 * Webhook发送
 *============================================================================*/

int ipv6_proxy_send_now(void) {
  printf("[IPv6Proxy] 立即发送IPv6地址\n");
  do_send_ipv6(1);
  return 0;
}

int ipv6_proxy_test_send(void) {
  printf("[IPv6Proxy] 测试发送\n");
  do_send_ipv6(0); /* 不重试 */
  return 0;
}

/*============================================================================
 * 发送日志管理
 *============================================================================*/

/* 记录发送日志 */
static void log_send_record(const char *ipv6_addr, const char *content,
                            const char *response, int result) {
  pthread_mutex_lock(&g_ipv6_log_mutex);

  int idx = g_ipv6_log_id % MAX_IPV6_SEND_LOGS;
  g_ipv6_log_id++;

  g_ipv6_send_logs[idx].id = g_ipv6_log_id;
  strncpy(g_ipv6_send_logs[idx].ipv6_addr, ipv6_addr ? ipv6_addr : "",
          sizeof(g_ipv6_send_logs[idx].ipv6_addr) - 1);
  strncpy(g_ipv6_send_logs[idx].content, content ? content : "",
          sizeof(g_ipv6_send_logs[idx].content) - 1);
  strncpy(g_ipv6_send_logs[idx].response, response ? response : "",
          sizeof(g_ipv6_send_logs[idx].response) - 1);
  g_ipv6_send_logs[idx].result = result;
  g_ipv6_send_logs[idx].created_at = time(NULL);

  if (g_ipv6_log_count < MAX_IPV6_SEND_LOGS) {
    g_ipv6_log_count++;
  }

  pthread_mutex_unlock(&g_ipv6_log_mutex);

  printf("[IPv6Proxy] 发送日志已添加, ID=%d, 结果=%d\n", g_ipv6_log_id, result);
}

/* JSON字符串转义工具 */
static void json_escape(char *dest, const char *src, size_t dest_size) {
  if (!dest || !src || dest_size == 0)
    return;

  size_t i = 0, j = 0;
  while (src[i] && j < dest_size - 1) {
    unsigned char c = (unsigned char)src[i];
    if (c == '"') {
      if (j + 2 < dest_size) {
        dest[j++] = '\\';
        dest[j++] = '"';
      } else
        break;
    } else if (c == '\\') {
      if (j + 2 < dest_size) {
        dest[j++] = '\\';
        dest[j++] = '\\';
      } else
        break;
    } else if (c == '\b') {
      if (j + 2 < dest_size) {
        dest[j++] = '\\';
        dest[j++] = 'b';
      } else
        break;
    } else if (c == '\f') {
      if (j + 2 < dest_size) {
        dest[j++] = '\\';
        dest[j++] = 'f';
      } else
        break;
    } else if (c == '\n') {
      if (j + 2 < dest_size) {
        dest[j++] = '\\';
        dest[j++] = 'n';
      } else
        break;
    } else if (c == '\r') {
      if (j + 2 < dest_size) {
        dest[j++] = '\\';
        dest[j++] = 'r';
      } else
        break;
    } else if (c == '\t') {
      if (j + 2 < dest_size) {
        dest[j++] = '\\';
        dest[j++] = 't';
      } else
        break;
    } else if (c < 32) {
      if (j + 6 < dest_size) {
        j += snprintf(dest + j, 7, "\\u%04x", c);
      } else
        break;
    } else {
      dest[j++] = c;
    }
    i++;
  }
  dest[j] = '\0';
}

/* 获取发送日志列表 */
int ipv6_proxy_get_send_logs(char *json_output, size_t size, int max_count) {
  if (!json_output || size < 10)
    return -1;

  pthread_mutex_lock(&g_ipv6_log_mutex);

  if (max_count <= 0 || max_count > MAX_IPV6_SEND_LOGS) {
    max_count = MAX_IPV6_SEND_LOGS;
  }
  if (max_count > g_ipv6_log_count) {
    max_count = g_ipv6_log_count;
  }

  int offset = 0;
  offset += snprintf(json_output + offset, size - offset, "[");

  for (int i = 0; i < max_count; i++) {
    /* 从最新的开始取 */
    int idx = (g_ipv6_log_id - 1 - i) % MAX_IPV6_SEND_LOGS;
    if (idx < 0)
      idx += MAX_IPV6_SEND_LOGS;

    if (i > 0)
      offset += snprintf(json_output + offset, size - offset, ",");

    char escaped_content[3072] = "";
    char escaped_response[3072] = "";

    json_escape(escaped_content, g_ipv6_send_logs[idx].content,
                sizeof(escaped_content));
    json_escape(escaped_response, g_ipv6_send_logs[idx].response,
                sizeof(escaped_response));

    offset += snprintf(json_output + offset, size - offset,
                       "{\"id\":%d,\"ipv6\":\"%s\",\"content\":\"%s\","
                       "\"response\":\"%s\",\"result\":%d,\"created_at\":%ld}",
                       g_ipv6_send_logs[idx].id,
                       g_ipv6_send_logs[idx].ipv6_addr, escaped_content,
                       escaped_response, g_ipv6_send_logs[idx].result,
                       (long)g_ipv6_send_logs[idx].created_at);

    if (offset >= (int)size - 10)
      break;
  }

  offset += snprintf(json_output + offset, size - offset, "]");

  pthread_mutex_unlock(&g_ipv6_log_mutex);
  return 0;
}
