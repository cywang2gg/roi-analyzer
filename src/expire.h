#ifndef EXPIRE_H
#define EXPIRE_H

#include <windows.h>

typedef enum {
    EXPIRE_OK = 0,
    EXPIRE_ERR_TIMEOUT,     /* 超過指定截止日期 (2026-11-12) */
    EXPIRE_ERR_ROLLBACK     /* 系統時間遭倒調 (早於建置或寫入時間) */
} ExpireStatus;

/* 於 WinMain 初始化前呼叫 */
ExpireStatus Expire_CheckStartup(void);

/* 供核心模組 (Image_Load) 內聯檢驗 */
BOOL Expire_IsActive(void);

#endif /* EXPIRE_H */
