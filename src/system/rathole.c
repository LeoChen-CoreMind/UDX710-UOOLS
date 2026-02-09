/**
 * @file rathole.c
 * @brief Rathole 内网穿透控制模块实现
 *
 * 功能实现:
 * - SQLite 数据库配置存储
 * - TOML 配置文件生成
 * - 进程启动/停止/状态检测
 * - 日志文件读取
 * - 开机自启动处理
 */

#include "rathole.h"
#include "database.h"
#include "exec_utils.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/*============================================================================
 * 内部变量
 *============================================================================*/

static pthread_mutex_t g_rathole_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_rathole_initialized = 0;
static RatholeConfig g_current_config = {0};

/*============================================================================
 * 内部函数声明
 *============================================================================*/

static int create_rathole_tables(void);
static int load_rathole_config(void);
static int parse_service_row(const char *row, RatholeService *svc);

/*============================================================================
 * 数据库表创建
 *============================================================================*/

static int create_rathole_tables(void) {
  const char *sql = "CREATE TABLE IF NOT EXISTS rathole_config ("
                    "id INTEGER PRIMARY KEY DEFAULT 1,"
                    "server_addr TEXT,"
                    "auto_start INTEGER DEFAULT 0,"
                    "enabled INTEGER DEFAULT 0"
                    ");"
                    "CREATE TABLE IF NOT EXISTS rathole_services ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "name TEXT NOT NULL UNIQUE,"
                    "token TEXT NOT NULL,"
                    "local_addr TEXT NOT NULL,"
                    "enabled INTEGER DEFAULT 1,"
                    "created_at INTEGER NOT NULL"
                    ");";

  return db_execute(sql);
}

/*============================================================================
 * 配置加载
 *============================================================================*/

static int load_rathole_config(void) {
  char output[256];
  const char *sql = "SELECT server_addr || '|' || auto_start || '|' || enabled "
                    "FROM rathole_config WHERE id = 1;";

  pthread_mutex_lock(&g_rathole_mutex);
  int ret = db_query_string(sql, output, sizeof(output));
  pthread_mutex_unlock(&g_rathole_mutex);

  if (ret == 0 && strlen(output) > 0) {
    char *p1 = strchr(output, '|');
    if (p1) {
      *p1 = '\0';
      char *p2 = strchr(p1 + 1, '|');
      if (p2) {
        *p2 = '\0';
        strncpy(g_current_config.server_addr, output,
                sizeof(g_current_config.server_addr) - 1);
        g_current_config.auto_start = atoi(p1 + 1);
        g_current_config.enabled = atoi(p2 + 1);
      }
    }
  } else {
    /* 默认配置 */
    memset(&g_current_config, 0, sizeof(g_current_config));
  }

  printf("[Rathole] 配置加载完成: 服务器=%s, 自启动=%d, 启用=%d\n",
         g_current_config.server_addr, g_current_config.auto_start,
         g_current_config.enabled);
  return 0;
}

/*============================================================================
 * 初始化和清理
 *============================================================================*/

int rathole_init(const char *db_path) {
  if (g_rathole_initialized) {
    return 0;
  }

  printf("[Rathole] 初始化模块\n");

  /* 初始化数据库（如果未初始化） */
  if (db_path && strlen(db_path) > 0) {
    db_init(db_path);
  }

  /* 创建数据库表 */
  if (create_rathole_tables() != 0) {
    printf("[Rathole] 创建数据库表失败\n");
    return -1;
  }

  /* 加载配置 */
  load_rathole_config();

  /* 处理自启动 */
  if (g_current_config.enabled && g_current_config.auto_start) {
    printf("[Rathole] 检测到自启动配置，正在启动服务...\n");
    if (rathole_start() == 0) {
      printf("[Rathole] 自启动成功\n");
    } else {
      printf("[Rathole] 自启动失败\n");
    }
  }

  g_rathole_initialized = 1;
  printf("[Rathole] 模块初始化完成\n");
  return 0;
}

void rathole_deinit(void) {
  if (!g_rathole_initialized) {
    return;
  }

  /* 停止进程 */
  rathole_stop();

  g_rathole_initialized = 0;
  printf("[Rathole] 模块已清理\n");
}

/*============================================================================
 * 配置管理
 *============================================================================*/

int rathole_get_config(RatholeConfig *config) {
  if (!config) {
    return -1;
  }

  pthread_mutex_lock(&g_rathole_mutex);
  memcpy(config, &g_current_config, sizeof(RatholeConfig));
  pthread_mutex_unlock(&g_rathole_mutex);

  return 0;
}

int rathole_set_config(const char *server_addr, int auto_start, int enabled) {
  char sql[512];
  char escaped_addr[256];

  if (!server_addr) {
    server_addr = "";
  }

  db_escape_string(server_addr, escaped_addr, sizeof(escaped_addr));

  snprintf(sql, sizeof(sql),
           "INSERT OR REPLACE INTO rathole_config (id, server_addr, "
           "auto_start, enabled) "
           "VALUES (1, '%s', %d, %d);",
           escaped_addr, auto_start ? 1 : 0, enabled ? 1 : 0);

  pthread_mutex_lock(&g_rathole_mutex);
  int ret = db_execute(sql);
  pthread_mutex_unlock(&g_rathole_mutex);

  if (ret != 0) {
    printf("[Rathole] 保存配置失败\n");
    return -1;
  }

  /* 更新内存配置 */
  strncpy(g_current_config.server_addr, server_addr,
          sizeof(g_current_config.server_addr) - 1);
  g_current_config.auto_start = auto_start;
  g_current_config.enabled = enabled;

  printf("[Rathole] 配置保存成功: 服务器=%s, 自启动=%d, 启用=%d\n", server_addr,
         auto_start, enabled);
  return 0;
}

/*============================================================================
 * 服务管理
 *============================================================================*/

static int parse_service_row(const char *row, RatholeService *svc) {
  if (!row || !svc || strlen(row) == 0) {
    return -1;
  }

  char buf[1024];
  strncpy(buf, row, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *fields[6] = {NULL};
  int field_count = 0;
  char *p = buf;
  char *start = p;

  while (*p && field_count < 6) {
    if (*p == '|') {
      *p = '\0';
      fields[field_count++] = start;
      start = p + 1;
    }
    p++;
  }
  if (field_count < 6 && start) {
    fields[field_count++] = start;
  }

  if (field_count < 6) {
    return -1;
  }

  memset(svc, 0, sizeof(RatholeService));
  svc->id = atoi(fields[0]);
  strncpy(svc->name, fields[1], sizeof(svc->name) - 1);
  strncpy(svc->token, fields[2], sizeof(svc->token) - 1);
  strncpy(svc->local_addr, fields[3], sizeof(svc->local_addr) - 1);
  svc->enabled = atoi(fields[4]);
  svc->created_at = (time_t)atol(fields[5]);

  return 0;
}

int rathole_service_list(RatholeService *services, int max_count) {
  char *output = NULL;

  if (!services || max_count <= 0) {
    return -1;
  }

  output = (char *)malloc(32 * 1024);
  if (!output) {
    return -1;
  }

  const char *sql =
      "SELECT id || '|' || name || '|' || token || '|' || local_addr || '|' || "
      "enabled || '|' || created_at FROM rathole_services ORDER BY id ASC;";

  pthread_mutex_lock(&g_rathole_mutex);
  int ret = db_query_string(sql, output, 32 * 1024);
  pthread_mutex_unlock(&g_rathole_mutex);

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

    if (parse_service_row(line, &services[count]) == 0) {
      count++;
    }

    line = next_line;
  }

  free(output);
  printf("[Rathole] 获取到 %d 个服务\n", count);
  return count;
}

int rathole_service_add(const char *name, const char *token,
                        const char *local_addr) {
  char sql[1024];
  char escaped_name[128];
  char escaped_token[256];
  char escaped_addr[128];

  if (!name || !token || !local_addr || strlen(name) == 0 ||
      strlen(token) == 0 || strlen(local_addr) == 0) {
    printf("[Rathole] 服务参数无效\n");
    return -1;
  }

  db_escape_string(name, escaped_name, sizeof(escaped_name));
  db_escape_string(token, escaped_token, sizeof(escaped_token));
  db_escape_string(local_addr, escaped_addr, sizeof(escaped_addr));

  time_t now = time(NULL);

  snprintf(sql, sizeof(sql),
           "INSERT INTO rathole_services (name, token, local_addr, enabled, "
           "created_at) "
           "VALUES ('%s', '%s', '%s', 1, %ld);",
           escaped_name, escaped_token, escaped_addr, (long)now);

  pthread_mutex_lock(&g_rathole_mutex);
  int ret = db_execute(sql);
  pthread_mutex_unlock(&g_rathole_mutex);

  if (ret == 0) {
    printf("[Rathole] 服务添加成功: %s -> %s\n", name, local_addr);
  } else {
    printf("[Rathole] 服务添加失败\n");
  }

  return ret;
}

int rathole_service_update(int id, const char *name, const char *token,
                           const char *local_addr, int enabled) {
  char sql[1024];
  char escaped_name[128];
  char escaped_token[256];
  char escaped_addr[128];

  if (id <= 0) {
    return -1;
  }

  if (!name || !token || !local_addr || strlen(name) == 0 ||
      strlen(token) == 0 || strlen(local_addr) == 0) {
    printf("[Rathole] 服务参数无效\n");
    return -1;
  }

  db_escape_string(name, escaped_name, sizeof(escaped_name));
  db_escape_string(token, escaped_token, sizeof(escaped_token));
  db_escape_string(local_addr, escaped_addr, sizeof(escaped_addr));

  snprintf(
      sql, sizeof(sql),
      "UPDATE rathole_services SET name='%s', token='%s', local_addr='%s', "
      "enabled=%d WHERE id=%d;",
      escaped_name, escaped_token, escaped_addr, enabled ? 1 : 0, id);

  pthread_mutex_lock(&g_rathole_mutex);
  int ret = db_execute(sql);
  pthread_mutex_unlock(&g_rathole_mutex);

  if (ret == 0) {
    printf("[Rathole] 服务更新成功: ID=%d\n", id);
  } else {
    printf("[Rathole] 服务更新失败\n");
  }

  return ret;
}

int rathole_service_delete(int id) {
  char sql[128];

  if (id <= 0) {
    return -1;
  }

  snprintf(sql, sizeof(sql), "DELETE FROM rathole_services WHERE id = %d;", id);

  pthread_mutex_lock(&g_rathole_mutex);
  int ret = db_execute(sql);
  pthread_mutex_unlock(&g_rathole_mutex);

  if (ret == 0) {
    printf("[Rathole] 服务删除成功: ID=%d\n", id);
  } else {
    printf("[Rathole] 服务删除失败\n");
  }

  return ret;
}

/*============================================================================
 * TOML 配置生成
 *============================================================================*/

int rathole_generate_config(void) {
  FILE *fp;
  RatholeService services[RATHOLE_MAX_SERVICES];
  int count;

  /* 检查服务器地址 */
  if (strlen(g_current_config.server_addr) == 0) {
    printf("[Rathole] 服务器地址未配置\n");
    return -1;
  }

  /* 获取服务列表 */
  count = rathole_service_list(services, RATHOLE_MAX_SERVICES);
  if (count <= 0) {
    printf("[Rathole] 没有配置任何服务\n");
    return -1;
  }

  /* 生成 TOML 文件 */
  fp = fopen(RATHOLE_CONFIG_PATH, "w");
  if (!fp) {
    printf("[Rathole] 无法创建配置文件: %s\n", RATHOLE_CONFIG_PATH);
    return -1;
  }

  /* 写入 [client] 配置 */
  fprintf(fp, "# Rathole Client Configuration\n");
  fprintf(fp, "# Auto-generated by Web Management\n\n");
  fprintf(fp, "[client]\n");
  fprintf(fp, "remote_addr = \"%s\"\n\n", g_current_config.server_addr);

  /* 写入服务配置 */
  for (int i = 0; i < count; i++) {
    if (!services[i].enabled) {
      continue;
    }

    fprintf(fp, "[client.services.%s]\n", services[i].name);
    fprintf(fp, "token = \"%s\"\n", services[i].token);
    fprintf(fp, "local_addr = \"%s\"\n\n", services[i].local_addr);
  }

  fclose(fp);
  printf("[Rathole] 配置文件已生成: %s\n", RATHOLE_CONFIG_PATH);
  return 0;
}

/*============================================================================
 * 进程控制
 *============================================================================*/

int rathole_start(void) {
  char cmd[512];
  char output[256];

  /* 先强制停止所有可能存在的rathole进程 */
  printf("[Rathole] 启动前先清理可能存在的进程...\n");
  snprintf(cmd, sizeof(cmd), "pkill -9 -f '%s' 2>/dev/null; sleep 0.5",
           RATHOLE_BIN_PATH);
  run_command(output, sizeof(output), "sh", "-c", cmd, NULL);

  /* 清理PID文件 */
  unlink(RATHOLE_PID_PATH);

  /* 再次检查是否已运行 */
  if (rathole_get_status(NULL) == 1) {
    printf("[Rathole] 清理后仍有进程运行，再次强制终止\n");
    snprintf(cmd, sizeof(cmd), "pkill -9 -f '%s'", RATHOLE_BIN_PATH);
    run_command(output, sizeof(output), "sh", "-c", cmd, NULL);
    usleep(500000);
  }

  /* 检查可执行文件 */
  if (access(RATHOLE_BIN_PATH, X_OK) != 0) {
    printf("[Rathole] 可执行文件不存在或无执行权限: %s\n", RATHOLE_BIN_PATH);
    return -1;
  }

  /* 生成配置文件 */
  if (rathole_generate_config() != 0) {
    printf("[Rathole] 生成配置文件失败\n");
    return -1;
  }

  /* 清空旧日志 */
  rathole_clear_logs();

  /* 启动进程 */
  snprintf(cmd, sizeof(cmd), "nohup %s %s > %s 2>&1 & echo $!",
           RATHOLE_BIN_PATH, RATHOLE_CONFIG_PATH, RATHOLE_LOG_PATH);

  if (run_command(output, sizeof(output), "sh", "-c", cmd, NULL) != 0) {
    printf("[Rathole] 启动命令执行失败\n");
    return -1;
  }

  /* 保存 PID */
  int pid = atoi(output);
  if (pid > 0) {
    FILE *fp = fopen(RATHOLE_PID_PATH, "w");
    if (fp) {
      fprintf(fp, "%d\n", pid);
      fclose(fp);
    }
  }

  /* 等待一下检查是否成功启动 */
  usleep(500000); /* 500ms */

  if (rathole_get_status(NULL) == 1) {
    printf("[Rathole] 服务启动成功, PID=%d\n", pid);
    return 0;
  } else {
    printf("[Rathole] 服务启动后立即退出，请检查日志\n");
    return -1;
  }
}

int rathole_stop(void) {
  char cmd[256];
  char output[128];

  printf("[Rathole] 停止服务\n");

  /* 使用多种方式确保进程被终止 */
  /* 方式1: 通过PID文件终止 */
  FILE *fp = fopen(RATHOLE_PID_PATH, "r");
  if (fp) {
    int pid = 0;
    if (fscanf(fp, "%d", &pid) == 1 && pid > 0) {
      printf("[Rathole] 从PID文件读取到PID=%d，终止进程\n", pid);
      kill(pid, SIGTERM);
      usleep(300000);
      kill(pid, SIGKILL);
    }
    fclose(fp);
  }

  /* 方式2: 通过pkill终止所有相关进程 */
  snprintf(cmd, sizeof(cmd), "pkill -f '%s'", RATHOLE_BIN_PATH);
  run_command(output, sizeof(output), "sh", "-c", cmd, NULL);
  usleep(300000);

  /* 方式3: 强制终止 */
  snprintf(cmd, sizeof(cmd), "pkill -9 -f '%s'", RATHOLE_BIN_PATH);
  run_command(output, sizeof(output), "sh", "-c", cmd, NULL);
  usleep(300000);

  /* 清理 PID 文件 */
  unlink(RATHOLE_PID_PATH);

  if (rathole_get_status(NULL) == 0) {
    printf("[Rathole] 服务已停止\n");
    return 0;
  } else {
    printf("[Rathole] 警告：可能仍有进程未完全停止\n");
    return 0;
  }
}

int rathole_restart(void) {
  rathole_stop();
  usleep(500000); /* 500ms */
  return rathole_start();
}

int rathole_get_status(RatholeStatus *status) {
  char output[256] = {0};
  int running = 0;
  int pid = -1;

  /*
   * 方式1: 检查 PID 文件并验证进程是否存在
   * 最可靠且最快，不依赖任何外部命令
   */
  FILE *fp = fopen(RATHOLE_PID_PATH, "r");
  if (fp) {
    int saved_pid = 0;
    if (fscanf(fp, "%d", &saved_pid) == 1 && saved_pid > 0) {
      /* kill(pid, 0) 检查进程是否存在 - POSIX 标准 */
      if (kill(saved_pid, 0) == 0) {
        running = 1;
        pid = saved_pid;
      }
    }
    fclose(fp);
  }

  /*
   * 方式2: 使用 ps + grep 检测（兼容所有 BusyBox）
   * 注意：不使用 pgrep/pidof/awk，这些命令部分设备不支持
   */
  if (!running) {
    /* 检查是否有 rathole 进程在运行 */
    if (run_command(output, sizeof(output), "sh", "-c",
                    "ps | grep '[r]athole' | grep -v grep", NULL) == 0 &&
        strlen(output) > 0) {
      running = 1;
      /* 尝试从输出中提取 PID（第一个数字） */
      char *p = output;
      while (*p == ' ')
        p++; /* 跳过前导空格 */
      pid = atoi(p);
    }
  }

  if (status) {
    memset(status, 0, sizeof(RatholeStatus));
    status->running = running;
    status->pid = pid;

    /* 获取服务数量 */
    const char *sql =
        "SELECT COUNT(*) FROM rathole_services WHERE enabled = 1;";
    status->service_count = db_query_int(sql, 0);
  }

  return running;
}

/*============================================================================
 * 日志管理
 *============================================================================*/

int rathole_get_logs(char *buf, size_t size, int max_lines) {
  char cmd[512];

  if (!buf || size == 0) {
    return -1;
  }

  buf[0] = '\0';

  /* 检查日志文件是否存在 */
  if (access(RATHOLE_LOG_PATH, R_OK) != 0) {
    return 0;
  }

  if (max_lines > 0) {
    snprintf(cmd, sizeof(cmd), "tail -n %d '%s'", max_lines, RATHOLE_LOG_PATH);
  } else {
    snprintf(cmd, sizeof(cmd), "cat '%s'", RATHOLE_LOG_PATH);
  }

  if (run_command(buf, size, "sh", "-c", cmd, NULL) != 0) {
    buf[0] = '\0';
    return -1;
  }

  return (int)strlen(buf);
}

int rathole_clear_logs(void) {
  FILE *fp = fopen(RATHOLE_LOG_PATH, "w");
  if (fp) {
    fclose(fp);
    printf("[Rathole] 日志已清空\n");
    return 0;
  }
  return -1;
}
