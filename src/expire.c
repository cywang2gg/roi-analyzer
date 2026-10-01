#include "expire.h"

#include <time.h>

/* 2026-11-12 23:59:59 UTC+8 = 1795027199 */
#define EXP_DEADLINE_RAW   1795027199ULL
#define EXP_XOR_KEY        0x5A9C3F12D54E8B07ULL
#define EXP_DEADLINE_ENC   (EXP_DEADLINE_RAW ^ EXP_XOR_KEY)

static volatile LONG g_session_valid = 0; /* 0=無效, 1=有效 */

static unsigned long long Expire_GetDeadline(void)
{
    return EXP_DEADLINE_ENC ^ EXP_XOR_KEY;
}

static unsigned long long Expire_GetPEBuildTimestamp(void)
{
    HMODULE hMod = GetModuleHandleA(NULL);
    PIMAGE_DOS_HEADER dos;
    PIMAGE_NT_HEADERS nt;
    if (!hMod)
        return 0;

    dos = (PIMAGE_DOS_HEADER)hMod;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    nt = (PIMAGE_NT_HEADERS)((BYTE *)hMod + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;

    return (unsigned long long)nt->FileHeader.TimeDateStamp;
}

static unsigned long long Expire_GetExeWriteTimestamp(void)
{
    char path[MAX_PATH];
    HANDLE hFile;
    FILETIME ftWrite;
    unsigned long long epoch = 0;
    if (!GetModuleFileNameA(NULL, path, MAX_PATH))
        return 0;

    hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return 0;

    if (GetFileTime(hFile, NULL, NULL, &ftWrite)) {
        ULARGE_INTEGER ull;
        ull.LowPart = ftWrite.dwLowDateTime;
        ull.HighPart = ftWrite.dwHighDateTime;
        /* Windows FILETIME 轉 Unix Epoch (秒) */
        if (ull.QuadPart >= 116444736000000000ULL)
            epoch = (ull.QuadPart - 116444736000000000ULL) / 10000000ULL;
    }
    CloseHandle(hFile);
    return epoch;
}

ExpireStatus Expire_CheckStartup(void)
{
    time_t raw_now = time(NULL);
    unsigned long long now;
    unsigned long long deadline;
    unsigned long long build_time;
    unsigned long long file_write;
    if (raw_now == (time_t)-1) {
        InterlockedExchange(&g_session_valid, 0);
        return EXPIRE_ERR_TIMEOUT;
    }

    now = (unsigned long long)raw_now;
    deadline = Expire_GetDeadline();
    build_time = Expire_GetPEBuildTimestamp();
    file_write = Expire_GetExeWriteTimestamp();

    /* 1. 到期檢驗 */
    if (now > deadline) {
        InterlockedExchange(&g_session_valid, 0);
        return EXPIRE_ERR_TIMEOUT;
    }

    /* 2. 防調回時間 (MinGW 若 build_time 為 0 則略過, 以 file_write 為主) */
    if (build_time > 0 && now < build_time) {
        InterlockedExchange(&g_session_valid, 0);
        return EXPIRE_ERR_ROLLBACK;
    }
    if (file_write > 0 && now < file_write) {
        InterlockedExchange(&g_session_valid, 0);
        return EXPIRE_ERR_ROLLBACK;
    }

    InterlockedExchange(&g_session_valid, 1);
    return EXPIRE_OK;
}

BOOL Expire_IsActive(void)
{
    return (InterlockedCompareExchange(&g_session_valid, 0, 0) == 1) ? TRUE : FALSE;
}
