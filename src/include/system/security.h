/**
 * @file security.h
 * @brief 密保功能模块头文件
 * @description 提供密保问题设置、验证、找回密码、重置数据等功能
 */

#ifndef SECURITY_H
#define SECURITY_H

#include <stddef.h>

/*============================================================================
 * 常量定义
 *============================================================================*/

#define SECURITY_QUESTION_MAX_LEN 256      /* 问题最大长度 */
#define SECURITY_ANSWER_MAX_LEN 128        /* 答案最大长度 */
#define SECURITY_MD5_LEN 33                /* MD5哈希长度(32+1) */
#define SECURITY_ICCID_MAX_LEN 24          /* ICCID最大长度 */
#define SECURITY_CONFIRM_TEXT "已知晓风险" /* 确认文本 */

/*============================================================================
 * 数据结构
 *============================================================================*/

/**
 * @brief 密保状态
 */
typedef struct {
  int is_set;                         /* 是否已设置密保 */
  char iccid[SECURITY_ICCID_MAX_LEN]; /* 绑定的ICCID */
  long created_at;                    /* 创建时间戳 */
} SecurityStatus;

/**
 * @brief 密保问题（不含答案）
 */
typedef struct {
  char question1[SECURITY_QUESTION_MAX_LEN];
  char question2[SECURITY_QUESTION_MAX_LEN];
} SecurityQuestions;

/**
 * @brief 密保设置请求
 */
typedef struct {
  char question1[SECURITY_QUESTION_MAX_LEN];
  char answer1[SECURITY_ANSWER_MAX_LEN];
  char question2[SECURITY_QUESTION_MAX_LEN];
  char answer2[SECURITY_ANSWER_MAX_LEN];
} SecuritySetupRequest;

/**
 * @brief 密保验证请求
 */
typedef struct {
  char answer1[SECURITY_ANSWER_MAX_LEN];
  char answer2[SECURITY_ANSWER_MAX_LEN];
  char iccid[SECURITY_ICCID_MAX_LEN];
  char confirm[32]; /* 确认文本 */
} SecurityVerifyRequest;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief 初始化密保模块
 * @return 0=成功, -1=失败
 */
int security_init(void);

/**
 * @brief 检查密保是否已设置
 * @param status 输出密保状态
 * @return 0=成功, -1=失败
 */
int security_get_status(SecurityStatus *status);

/**
 * @brief 设置密保问题（只能设置一次）
 * @param req 设置请求
 * @return 0=成功, -1=已设置不可更改, -2=参数错误
 */
int security_setup(const SecuritySetupRequest *req);

/**
 * @brief 获取密保问题（不含答案）
 * @param questions 输出问题
 * @return 0=成功, -1=未设置
 */
int security_get_questions(SecurityQuestions *questions);

/**
 * @brief 验证密保答案
 * @param req 验证请求
 * @return 0=验证通过, -1=答案错误, -2=ICCID不匹配, -3=确认文本错误
 */
int security_verify(const SecurityVerifyRequest *req);

/**
 * @brief 找回密码（验证后重置密码为默认）
 * @param req 验证请求
 * @return 0=成功, 其他=失败
 */
int security_reset_password(const SecurityVerifyRequest *req);

/**
 * @brief 重置数据（验证后删除所有数据）
 * @param req 验证请求
 * @return 0=成功, 其他=失败
 */
int security_factory_reset(const SecurityVerifyRequest *req);

/**
 * @brief 获取当前设备ICCID
 * @param iccid 输出缓冲区
 * @param size 缓冲区大小
 * @return 0=成功, -1=失败
 */
int security_get_current_iccid(char *iccid, size_t size);

#endif /* SECURITY_H */
