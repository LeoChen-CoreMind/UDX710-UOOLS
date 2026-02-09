/**
 * @file http_server.c
 * @brief HTTP 服务器实现 (对应 Go: main.go)
 */

#include "http_server.h"
#include "advanced.h"
#include "apn.h"
#include "auth.h"
#include "charge.h"
#include "dbus_core.h"
#include "handlers.h"
#include "http_utils.h"
#include "mongoose.h"
#include "netif.h"
#include "reboot.h"
#include "sms.h"
#include "system/ipv6_proxy.h"
#include "system/phone_case.h"
#include "system/rathole.h"
#include "system/security.h"
#include "traffic.h"
#include "usb_mode.h"
#include <glib.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 嵌入式文件系统声明 (packed_fs.c) */
extern int serve_packed_file(struct mg_connection *c,
                             struct mg_http_message *hm);

/* 全局变量 */
static struct mg_mgr g_mgr;
static volatile int g_running = 0;

/* 信号处理 */
static void signal_handler(int sig) {
  (void)sig;
  g_running = 0;
}

/**
 * 检查API是否在白名单中（无需认证）
 * @param uri 请求URI
 * @param method 请求方法 (GET/POST等)
 * @param method_len 方法字符串长度
 */
static int is_auth_whitelist(const char *uri, const char *method,
                             size_t method_len) {
  /* 认证相关API无需认证 */
  if (strncmp(uri, "/api/auth/login", 15) == 0)
    return 1;
  if (strncmp(uri, "/api/auth/status", 16) == 0)
    return 1;

  /* 公开信息API - 仅GET请求无需认证 */
  int is_get = (method_len == 3 && memcmp(method, "GET", 3) == 0);
  if (is_get) {
    if (strncmp(uri, "/api/info", 9) == 0)
      return 1;
    if (strncmp(uri, "/api/charge/config", 18) == 0)
      return 1;
    if (strncmp(uri, "/api/current_band", 17) == 0)
      return 1;
  }

  /* 设备控制API - POST和OPTIONS请求无需认证（OPTIONS用于CORS预检） */
  int is_post = (method_len == 4 && memcmp(method, "POST", 4) == 0);
  int is_options = (method_len == 7 && memcmp(method, "OPTIONS", 7) == 0);
  if (is_post || is_options) {
    if (strncmp(uri, "/api/device_control", 19) == 0)
      return 1;
  }

  /* 忘记密码相关API - 无需认证 */
  if (is_get) {
    if (strncmp(uri, "/api/security/questions", 23) == 0)
      return 1;
  }
  if (is_post) {
    if (strncmp(uri, "/api/security/verify", 20) == 0)
      return 1;
    if (strncmp(uri, "/api/security/reset-password", 28) == 0)
      return 1;
  }

  return 0;
}

/**
 * 验证请求的Token
 * @return 0验证通过，-1验证失败
 */
static int verify_request_token(struct mg_http_message *hm) {
  struct mg_str *auth_header = mg_http_get_header(hm, "Authorization");

  if (!auth_header || auth_header->len <= 7) {
    return -1;
  }

  /* 格式: "Bearer <token>" */
  if (strncmp(auth_header->buf, "Bearer ", 7) != 0) {
    return -1;
  }

  char token[65] = {0};
  size_t token_len = auth_header->len - 7;
  if (token_len >= sizeof(token)) {
    return -1;
  }

  memcpy(token, auth_header->buf + 7, token_len);
  token[token_len] = '\0';

  return auth_verify_token(token);
}

/* HTTP 事件处理函数 */
static void http_handler(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    char uri[256] = {0};
    size_t uri_len =
        hm->uri.len < sizeof(uri) - 1 ? hm->uri.len : sizeof(uri) - 1;
    memcpy(uri, hm->uri.buf, uri_len);

    /* 静态文件处理 */
    if (hm->uri.len < 5 || memcmp(hm->uri.buf, "/api/", 5) != 0) {
      if (serve_packed_file(c, hm)) {
        return; /* 静态文件已处理 */
      }
    }

    /* 认证 API - 优先处理，无需Token验证 */
    if (mg_match(hm->uri, mg_str("/api/auth/login"), NULL)) {
      handle_auth_login(c, hm);
      return;
    } else if (mg_match(hm->uri, mg_str("/api/auth/status"), NULL)) {
      handle_auth_status(c, hm);
      return;
    } else if (mg_match(hm->uri, mg_str("/api/auth/logout"), NULL)) {
      handle_auth_logout(c, hm);
      return;
    } else if (mg_match(hm->uri, mg_str("/api/auth/password"), NULL)) {
      handle_auth_password(c, hm);
      return;
    }

    /* 认证中间件 - 检查Token */
    if (!is_auth_whitelist(uri, hm->method.buf, hm->method.len)) {
      if (verify_request_token(hm) != 0) {
        HTTP_JSON(c, 401,
                  "{\"status\":\"error\",\"message\":\"未授权，请先登录\"}");
        return;
      }
    }

    /* API 路由 */
    if (mg_match(hm->uri, mg_str("/api/info"), NULL)) {
      handle_info(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/at"), NULL)) {
      handle_execute_at(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/set_network"), NULL)) {
      handle_set_network(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/switch"), NULL)) {
      handle_switch(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/airplane_mode"), NULL)) {
      handle_airplane_mode(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/device_control"), NULL)) {
      handle_device_control(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/clear_cache"), NULL)) {
      handle_clear_cache(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/current_band"), NULL)) {
      handle_get_current_band(c, hm);
    }
    /* 高级网络 API */
    else if (mg_match(hm->uri, mg_str("/api/bands"), NULL)) {
      handle_get_bands(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/lock_bands"), NULL)) {
      handle_lock_bands(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/unlock_bands"), NULL)) {
      handle_unlock_bands(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/cells"), NULL)) {
      handle_get_cells(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/lock_cell"), NULL)) {
      handle_lock_cell(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/unlock_cell"), NULL)) {
      handle_unlock_cell(c, hm);
    }
    /* 流量统计 API */
    else if (mg_match(hm->uri, mg_str("/api/get/Total"), NULL)) {
      handle_get_traffic_total(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/get/set"), NULL)) {
      handle_get_traffic_config(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/set/total"), NULL)) {
      handle_set_traffic_limit(c, hm);
    }
    /* 系统时间 API */
    else if (mg_match(hm->uri, mg_str("/api/get/time"), NULL)) {
      handle_get_system_time(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/set/time"), NULL)) {
      handle_set_system_time(c, hm);
    }
    /* 定时重启 API */
    else if (mg_match(hm->uri, mg_str("/api/get/first-reboot"), NULL)) {
      handle_get_first_reboot(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/set/reboot"), NULL)) {
      handle_set_reboot(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/claen/cron"), NULL)) {
      handle_clear_cron(c, hm);
    }
    /* 充电控制 API */
    else if (mg_match(hm->uri, mg_str("/api/charge/config"), NULL)) {
      handle_charge_config(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/charge/on"), NULL)) {
      handle_charge_on(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/charge/off"), NULL)) {
      handle_charge_off(c, hm);
    }
    /* 短信 API */
    else if (mg_match(hm->uri, mg_str("/api/sms"), NULL)) {
      handle_sms_list(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/sms/send"), NULL)) {
      handle_sms_send(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/sms/sent"), NULL)) {
      handle_sms_sent_list(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/sms/sent/*"), NULL)) {
      handle_sms_sent_delete(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/sms/config"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_sms_config_get(c, hm);
      } else {
        handle_sms_config_save(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/sms/webhook"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_sms_webhook_get(c, hm);
      } else {
        handle_sms_webhook_save(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/sms/webhook/test"), NULL)) {
      handle_sms_webhook_test(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/sms/webhook/logs"), NULL)) {
      handle_sms_webhook_logs(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/sms/fix"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_sms_fix_get(c, hm);
      } else {
        handle_sms_fix_set(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/sms/*"), NULL)) {
      handle_sms_delete(c, hm);
    }
    /* OTA更新 API */
    else if (mg_match(hm->uri, mg_str("/api/update/version"), NULL)) {
      handle_update_version(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/update/upload"), NULL)) {
      handle_update_upload(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/update/download"), NULL)) {
      handle_update_download(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/update/extract"), NULL)) {
      handle_update_extract(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/update/install"), NULL)) {
      handle_update_install(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/update/check"), NULL)) {
      handle_update_check(c, hm);
    }
    /* USB模式切换 API */
    else if (mg_match(hm->uri, mg_str("/api/usb/mode"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_usb_mode_get(c, hm);
      } else {
        handle_usb_mode_set(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/usb-advance"), NULL)) {
      handle_usb_advance(c, hm);
    }
    /* 数据连接和漫游 API */
    else if (mg_match(hm->uri, mg_str("/api/data"), NULL)) {
      handle_data_status(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/roaming"), NULL)) {
      handle_roaming_status(c, hm);
    }
    /* 网络接口监控 API */
    else if (mg_match(hm->uri, mg_str("/api/netif/list"), NULL)) {
      handle_netif_list(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/netif/stats"), NULL)) {
      handle_netif_stats(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/netif/monitor"), NULL)) {
      handle_netif_monitor(c, hm);
    }
    // /* APN 管理 API */
    // else if (mg_match(hm->uri, mg_str("/api/apn"), NULL)) {
    //     if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
    //         handle_apn_list(c, hm);
    //     } else {
    //         handle_apn_set(c, hm);
    //     }
    // }
    /* APN 配置管理 API */
    else if (mg_match(hm->uri, mg_str("/api/apn/config"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_apn_config_get(c, hm);
      } else {
        handle_apn_config_set(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/apn/templates"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_apn_templates_list(c, hm);
      } else {
        handle_apn_templates_create(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/apn/templates/*"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "PUT", 3) == 0) {
        handle_apn_templates_update(c, hm);
      } else {
        handle_apn_templates_delete(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/apn/apply"), NULL)) {
      handle_apn_apply(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/apn/clear"), NULL)) {
      handle_apn_clear(c, hm);
    }
    /* 插件管理 API */
    else if (mg_match(hm->uri, mg_str("/api/shell"), NULL)) {
      handle_shell_execute(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/plugins/all"), NULL)) {
      handle_plugin_delete_all(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/plugins"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_plugin_list(c, hm);
      } else {
        handle_plugin_upload(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/plugins/*"), NULL)) {
      handle_plugin_delete(c, hm);
    }
    /* 脚本管理 API */
    else if (mg_match(hm->uri, mg_str("/api/scripts"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_script_list(c, hm);
      } else {
        handle_script_upload(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/scripts/*"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "PUT", 3) == 0) {
        handle_script_update(c, hm);
      } else {
        handle_script_delete(c, hm);
      }
    }
    /* 插件存储 API */
    else if (mg_match(hm->uri, mg_str("/api/plugins/storage/*"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_plugin_storage_get(c, hm);
      } else if (hm->method.len == 4 &&
                 memcmp(hm->method.buf, "POST", 4) == 0) {
        handle_plugin_storage_set(c, hm);
      } else if (hm->method.len == 6 &&
                 memcmp(hm->method.buf, "DELETE", 6) == 0) {
        handle_plugin_storage_delete(c, hm);
      } else {
        HTTP_ERROR(c, 405, "Method not allowed");
      }
    }
    /* Rathole 内网穿透 API */
    else if (mg_match(hm->uri, mg_str("/api/rathole/config"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_rathole_config_get(c, hm);
      } else {
        handle_rathole_config_set(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/rathole/services"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_rathole_services_list(c, hm);
      } else {
        handle_rathole_service_add(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/rathole/services/*"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "PUT", 3) == 0) {
        handle_rathole_service_update(c, hm);
      } else {
        handle_rathole_service_delete(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/rathole/start"), NULL)) {
      handle_rathole_start(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/rathole/stop"), NULL)) {
      handle_rathole_stop(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/rathole/status"), NULL)) {
      handle_rathole_status(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/rathole/logs"), NULL)) {
      handle_rathole_logs(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/rathole/server-config"), NULL)) {
      handle_rathole_server_config(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/rathole/autostart"), NULL)) {
      handle_rathole_autostart(c, hm);
    }
    /* IPv6 Proxy 端口转发 API */
    else if (mg_match(hm->uri, mg_str("/api/ipv6-proxy/config"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_ipv6_proxy_config_get(c, hm);
      } else {
        handle_ipv6_proxy_config_set(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/ipv6-proxy/rules"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        handle_ipv6_proxy_rules_list(c, hm);
      } else {
        handle_ipv6_proxy_rules_add(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/ipv6-proxy/rules/*"), NULL)) {
      if (hm->method.len == 3 && memcmp(hm->method.buf, "PUT", 3) == 0) {
        handle_ipv6_proxy_rules_update(c, hm);
      } else {
        handle_ipv6_proxy_rules_delete(c, hm);
      }
    } else if (mg_match(hm->uri, mg_str("/api/ipv6-proxy/start"), NULL)) {
      handle_ipv6_proxy_start(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/ipv6-proxy/stop"), NULL)) {
      handle_ipv6_proxy_stop(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/ipv6-proxy/restart"), NULL)) {
      handle_ipv6_proxy_restart(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/ipv6-proxy/status"), NULL)) {
      handle_ipv6_proxy_status(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/ipv6-proxy/send"), NULL)) {
      handle_ipv6_proxy_send(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/ipv6-proxy/test"), NULL)) {
      handle_ipv6_proxy_test(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/ipv6-proxy/send-logs"), NULL)) {
      handle_ipv6_proxy_send_logs(c, hm);
    }
    /* 手机壳模式 API */
    else if (mg_match(hm->uri, mg_str("/api/phone-case"), NULL)) {
      handle_phone_case(c, hm);
    }
    /* 密保 API */
    else if (mg_match(hm->uri, mg_str("/api/security/status"), NULL)) {
      handle_security_status(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/security/setup"), NULL)) {
      handle_security_setup(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/security/questions"), NULL)) {
      handle_security_questions(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/security/verify"), NULL)) {
      handle_security_verify(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/security/reset-password"),
                        NULL)) {
      handle_security_reset_password(c, hm);
    } else if (mg_match(hm->uri, mg_str("/api/security/factory-reset"), NULL)) {
      handle_security_factory_reset(c, hm);
    }
    /* 未知 API 路由 */
    else {
      HTTP_ERROR(c, 404, "Endpoint not found");
    }
  }
}

int http_server_start(const char *port) {
  char listen_addr[64];

  /* 初始化 D-Bus */
  if (init_dbus() != 0) {
    printf("警告: D-Bus 初始化失败 (高级网络功能将不可用)\n");
  }

  /* 初始化流量统计 */
  init_traffic();

  /* 初始化充电控制 */
  init_charge();

  /* 初始化短信模块（必须在auth_init之前，因为auth依赖数据库） */
  if (sms_init("6677.db") != 0) {
    printf("警告: 短信模块初始化失败\n");
  }

  /* 初始化认证模块 */
  if (auth_init() != 0) {
    printf("警告: 认证模块初始化失败\n");
  }

  /* 初始化APN模块 */
  if (apn_init("6677.db") != 0) {
    printf("警告: APN模块初始化失败\n");
  }

  /* 初始化Rathole模块 */
  if (rathole_init("6677.db") != 0) {
    printf("警告: Rathole模块初始化失败\n");
  }

  /* 初始化手机壳模式模块 */
  phone_case_init();

  /* 初始化IPv6代理模块 */
  if (ipv6_proxy_init("6677.db") != 0) {
    printf("警告: IPv6代理模块初始化失败\n");
  }

  /* 初始化密保模块 */
  if (security_init() != 0) {
    printf("警告: 密保模块初始化失败\n");
  }

  /* 初始化 mongoose */
  mg_mgr_init(&g_mgr);

  /* 构建监听地址 - 使用 0.0.0.0 监听所有IPv4地址 */
  snprintf(listen_addr, sizeof(listen_addr), "http://0.0.0.0:%s", port);

  /* 创建 HTTP 监听器 */
  if (mg_http_listen(&g_mgr, listen_addr, http_handler, NULL) == NULL) {
    printf("无法监听端口 %s\n", port);
    mg_mgr_free(&g_mgr);
    return -1;
  }

  printf("Server starting on :%s\n", port);
  g_running = 1;

  /* 设置信号处理 */
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  return 0;
}

void http_server_stop(void) {
  g_running = 0;
  mg_mgr_free(&g_mgr);
  sms_deinit();
  close_dbus();
  printf("服务器已停止\n");
}

void http_server_run(void) {
  GMainContext *context = g_main_context_default();
  static int maintenance_counter = 0;

  while (g_running) {
    /* 处理GLib/D-Bus事件 - 优先处理，确保信号不丢失 */
    while (g_main_context_pending(context)) {
      g_main_context_iteration(context, FALSE);
    }

    /* 处理mongoose事件 - 减少超时时间以更快响应D-Bus信号 */
    mg_mgr_poll(&g_mgr, 10); /* 10ms超时 */

    /* 每30秒执行一次短信模块维护（检查D-Bus连接） */
    if (++maintenance_counter >= 3000) { /* 3000 * 10ms = 30秒 */
      maintenance_counter = 0;
      sms_maintenance();
    }
  }
}
