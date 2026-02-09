/**
 * @file handlers.h
 * @brief HTTP API handlers (Go: handlers)
 */

#ifndef HANDLERS_H
#define HANDLERS_H

#include "mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif

/* API 处理器 */
void handle_info(struct mg_connection *c, struct mg_http_message *hm);
void handle_execute_at(struct mg_connection *c, struct mg_http_message *hm);
void handle_set_network(struct mg_connection *c, struct mg_http_message *hm);
void handle_switch(struct mg_connection *c, struct mg_http_message *hm);
void handle_airplane_mode(struct mg_connection *c, struct mg_http_message *hm);
void handle_device_control(struct mg_connection *c, struct mg_http_message *hm);
void handle_clear_cache(struct mg_connection *c, struct mg_http_message *hm);
void handle_get_current_band(struct mg_connection *c,
                             struct mg_http_message *hm);

/* 短信 API */
void handle_sms_list(struct mg_connection *c, struct mg_http_message *hm);
void handle_sms_send(struct mg_connection *c, struct mg_http_message *hm);
void handle_sms_delete(struct mg_connection *c, struct mg_http_message *hm);
void handle_sms_webhook_get(struct mg_connection *c,
                            struct mg_http_message *hm);
void handle_sms_webhook_save(struct mg_connection *c,
                             struct mg_http_message *hm);
void handle_sms_webhook_test(struct mg_connection *c,
                             struct mg_http_message *hm);
void handle_sms_sent_list(struct mg_connection *c, struct mg_http_message *hm);
void handle_sms_sent_delete(struct mg_connection *c,
                            struct mg_http_message *hm);
void handle_sms_config_get(struct mg_connection *c, struct mg_http_message *hm);
void handle_sms_config_save(struct mg_connection *c,
                            struct mg_http_message *hm);
void handle_sms_fix_get(struct mg_connection *c, struct mg_http_message *hm);
void handle_sms_fix_set(struct mg_connection *c, struct mg_http_message *hm);

/* OTA更新 API */
void handle_update_version(struct mg_connection *c, struct mg_http_message *hm);
void handle_update_upload(struct mg_connection *c, struct mg_http_message *hm);
void handle_update_download(struct mg_connection *c,
                            struct mg_http_message *hm);
void handle_update_extract(struct mg_connection *c, struct mg_http_message *hm);
void handle_update_install(struct mg_connection *c, struct mg_http_message *hm);
void handle_update_check(struct mg_connection *c, struct mg_http_message *hm);

/* 系统时间 API */
void handle_get_system_time(struct mg_connection *c,
                            struct mg_http_message *hm);
void handle_set_system_time(struct mg_connection *c,
                            struct mg_http_message *hm);

/* 数据连接和漫游 API */
void handle_data_status(struct mg_connection *c, struct mg_http_message *hm);
void handle_roaming_status(struct mg_connection *c, struct mg_http_message *hm);

// /* APN 管理 API */
// void handle_apn_list(struct mg_connection *c, struct mg_http_message *hm);
// void handle_apn_set(struct mg_connection *c, struct mg_http_message *hm);

/* APN 配置管理 API */
void handle_apn_config_get(struct mg_connection *c, struct mg_http_message *hm);
void handle_apn_config_set(struct mg_connection *c, struct mg_http_message *hm);
void handle_apn_templates_list(struct mg_connection *c,
                               struct mg_http_message *hm);
void handle_apn_templates_create(struct mg_connection *c,
                                 struct mg_http_message *hm);
void handle_apn_templates_update(struct mg_connection *c,
                                 struct mg_http_message *hm);
void handle_apn_templates_delete(struct mg_connection *c,
                                 struct mg_http_message *hm);
void handle_apn_apply(struct mg_connection *c, struct mg_http_message *hm);
void handle_apn_clear(struct mg_connection *c, struct mg_http_message *hm);

/* 插件管理 API */
void handle_shell_execute(struct mg_connection *c, struct mg_http_message *hm);
void handle_plugin_list(struct mg_connection *c, struct mg_http_message *hm);
void handle_plugin_upload(struct mg_connection *c, struct mg_http_message *hm);
void handle_plugin_delete(struct mg_connection *c, struct mg_http_message *hm);
void handle_plugin_delete_all(struct mg_connection *c,
                              struct mg_http_message *hm);

/* 脚本管理 API */
void handle_script_list(struct mg_connection *c, struct mg_http_message *hm);
void handle_script_upload(struct mg_connection *c, struct mg_http_message *hm);
void handle_script_update(struct mg_connection *c, struct mg_http_message *hm);
void handle_script_delete(struct mg_connection *c, struct mg_http_message *hm);

/* 插件存储 API */
void handle_plugin_storage_get(struct mg_connection *c,
                               struct mg_http_message *hm);
void handle_plugin_storage_set(struct mg_connection *c,
                               struct mg_http_message *hm);
void handle_plugin_storage_delete(struct mg_connection *c,
                                  struct mg_http_message *hm);

/* 认证 API */
void handle_auth_login(struct mg_connection *c, struct mg_http_message *hm);
void handle_auth_logout(struct mg_connection *c, struct mg_http_message *hm);
void handle_auth_password(struct mg_connection *c, struct mg_http_message *hm);
void handle_auth_status(struct mg_connection *c, struct mg_http_message *hm);

/* Rathole 内网穿透 API */
void handle_rathole_config_get(struct mg_connection *c,
                               struct mg_http_message *hm);
void handle_rathole_config_set(struct mg_connection *c,
                               struct mg_http_message *hm);
void handle_rathole_services_list(struct mg_connection *c,
                                  struct mg_http_message *hm);
void handle_rathole_service_add(struct mg_connection *c,
                                struct mg_http_message *hm);
void handle_rathole_service_update(struct mg_connection *c,
                                   struct mg_http_message *hm);
void handle_rathole_service_delete(struct mg_connection *c,
                                   struct mg_http_message *hm);
void handle_rathole_start(struct mg_connection *c, struct mg_http_message *hm);
void handle_rathole_stop(struct mg_connection *c, struct mg_http_message *hm);
void handle_rathole_status(struct mg_connection *c, struct mg_http_message *hm);
void handle_rathole_logs(struct mg_connection *c, struct mg_http_message *hm);
void handle_rathole_server_config(struct mg_connection *c,
                                  struct mg_http_message *hm);
void handle_rathole_autostart(struct mg_connection *c,
                              struct mg_http_message *hm);

/* IPv6 Proxy 端口转发 API */
void handle_ipv6_proxy_config_get(struct mg_connection *c,
                                  struct mg_http_message *hm);
void handle_ipv6_proxy_config_set(struct mg_connection *c,
                                  struct mg_http_message *hm);
void handle_ipv6_proxy_rules_list(struct mg_connection *c,
                                  struct mg_http_message *hm);
void handle_ipv6_proxy_rules_add(struct mg_connection *c,
                                 struct mg_http_message *hm);
void handle_ipv6_proxy_rules_update(struct mg_connection *c,
                                    struct mg_http_message *hm);
void handle_ipv6_proxy_rules_delete(struct mg_connection *c,
                                    struct mg_http_message *hm);
void handle_ipv6_proxy_start(struct mg_connection *c,
                             struct mg_http_message *hm);
void handle_ipv6_proxy_stop(struct mg_connection *c,
                            struct mg_http_message *hm);
void handle_ipv6_proxy_restart(struct mg_connection *c,
                               struct mg_http_message *hm);
void handle_ipv6_proxy_status(struct mg_connection *c,
                              struct mg_http_message *hm);
void handle_ipv6_proxy_send(struct mg_connection *c,
                            struct mg_http_message *hm);
void handle_ipv6_proxy_test(struct mg_connection *c,
                            struct mg_http_message *hm);
void handle_ipv6_proxy_send_logs(struct mg_connection *c,
                                 struct mg_http_message *hm);

/* 短信Webhook日志 */
void handle_sms_webhook_logs(struct mg_connection *c,
                             struct mg_http_message *hm);

/* 密保 API */
void handle_security_status(struct mg_connection *c,
                            struct mg_http_message *hm);
void handle_security_setup(struct mg_connection *c, struct mg_http_message *hm);
void handle_security_questions(struct mg_connection *c,
                               struct mg_http_message *hm);
void handle_security_verify(struct mg_connection *c,
                            struct mg_http_message *hm);
void handle_security_reset_password(struct mg_connection *c,
                                    struct mg_http_message *hm);
void handle_security_factory_reset(struct mg_connection *c,
                                   struct mg_http_message *hm);

#ifdef __cplusplus
}
#endif

#endif /* HANDLERS_H */
