# ROI Analyzer — v2.9 移植架構書（監控＋更名）

> 版本 v2.9 | 日期：2026-09-28 | 資料夾監控（ReadDirectoryChangesW）＋新檔提示彈窗＋更名（F2 / File > Rename）＋INI 持久化
> 移植自：`cSHARP-IMAGEVIEWER`（C# WinForms .NET 8）之 `MONITOR_RENAME_ARCHITECTURE.md` 完整鏈路。
> 目標專案：`roi-analyzer`（純 C11 / Win32 / WIC / GDI+ / ANSI 全 A 版 API）。

## 變更歷史

- **v2.9 修訂（2026-09-28）**：整合 Gemini Flash 3.8 與內部唯讀審查意見——寫入等待限定背景執行緒（UI 端不可 Sleep）；Mutex 改瞬時持有＋單實例待處理佇列（ring 64）取代超時丟棄；Modal 忙碌／重入入隊；覆寫前 `.bak`／`_conflict_N` 備份；消除雙重 `App_ConfirmDiscard()`（單一檢查點原則）；§7.1 理由修正（記憶體拷貝無常駐 handle）；§7.2 補 `FileList_Find`＋索引更新；log 搬移失敗只警告；ID 落點 `main.c`；量級修為 1100～1200 行；補關機序列與保留字實作。
  - **相對 C# 版架構之差異與 C/Win32 調適**：
    1. **監控引擎**：C# 使用 .NET `FileSystemWatcher`（封裝 Completion Port）；C 版使用 Win32 原生 `ReadDirectoryChangesW`（單一背景工作執行緒搭配 Overlapped I/O 與事件陣列 `WaitForMultipleObjects`），以 `PostMessageA(hwnd_main, WM_APP_NEW_FILE, ...)` 切換至 UI 訊息迴圈，嚴格遵循 Win32 單執行緒 UI 模型。
    2. **持久化設定**：C# 具備 `SettingsManager`；`roi-analyzer` 現況無設定檔系統，新增輕量級 `src/settings.h/.c`，基於 Win32 `GetPrivateProfileStringA` / `WritePrivateProfileStringA` 讀寫同目錄下之 `roi_analyzer.ini`。
    3. **新檔提示視窗**：C# `NewFilePromptForm` 改為 Win32 資源對話框 `IDD_NEW_FILE_PROMPT`（搭配 GDI+/WIC 縮圖繪製），保留前綴記憶（記憶體靜態前綴＋INI `LastRenamePrefix` 雙層機制）與 5 項操作分支（僅更名／更名並開啟／加入比較／立即比較／取消）。
    4. **Handle 釋放與防鎖**：C# `pictureBox.Image = null` 與雙 Bitmap `Dispose()`；C 版改為 `Image_Free(&g_app.img)`＋`ViewPyr_Free(&g_app.pyramid)`＋ListView 表格清空＋Histogram 清空後，再調用 `MoveFileExA`，徹底杜絕 Windows 檔案佔用導致之 `ERROR_SHARING_VIOLATION`。
    5. **更名輸入對話框**：C# `Interaction.InputBox` 改為 Win32 原生對話框 `IDD_RENAME_INPUT`，整合非法字元檢查（`<>:"/\|?*`）、覆寫確認及 `ExtractPrefix` 前綴自動推導。
    6. **專案整合與衝突收斂**：納入 v2.8 之 `is_modified` 未存檔防護（`App_ConfirmDiscard()`）；更名主圖時關閉已開比較視窗（`Compare_CloseAll()`）；若使用者僅更名不開啟新檔，當新檔位於目前目錄時主動調用 `FileList_Refresh()` 刷新索引。

## 1. 目標與範圍

| # | 項目 | 內容 |
|---|------|------|
| 1 | 資料夾監控 | 後台執行緒監控最多 3 組資料夾（`MONITOR_MAX_PATHS = 3`），監聽檔案新增事件，以 `PostMessageA` 傳遞路徑至主視窗 |
| 2 | 監控設定 UI | 模態對話框（`IDD_MONITOR_SETTINGS`）：3 組路徑編輯框、瀏覽資料夾按鈕、啟用勾選框，並持久化至 `roi_analyzer.ini` |
| 3 | 新檔提示彈窗 | 模態對話框（`IDD_NEW_FILE_PROMPT`）：縮圖預覽、檔名編輯、5 種操作按鈕、記憶體與 INI 雙層前綴記憶 |
| 4 | 寫入防護與多開防重 | 寫入完成等待**限定背景執行緒**（worker 內 `Sleep(500)` 起步＋`File_WaitForWriteComplete` size-穩定輪詢，UI 端收到 `WM_APP_NEW_FILE` 即彈窗、不可再 Sleep）；具名 Mutex（`"RoiAnalyzer_NewFileMutex"`）**瞬時持有**僅做多實例防重＋單實例待處理佇列（ring 64，滿則狀態列提示、不靜默丟） |
| 5 | 影像更名 | `File > Rename File...` 選單與 `F2` 快捷鍵；嚴格 Handle 釋放順序；非法字元校驗；目標存在覆寫確認；關聯 CSV 同步更名 |
| 6 | 前綴自動提取 | 純 C 實作 `ExtractPrefix`，識別後綴差集或包含關係，自動儲存並作為下次命名預設值 |
| 7 | 狀態衝突收斂 | 旋轉未存檔（`is_modified`）攔截防護；更名主圖時關閉比較視窗（`Compare_CloseAll()`）；僅更名時依目錄關係刷新 `filelist` |

非目標：遞迴子目錄深度監控（僅監控指定層級）；批次正則表示式更名工具；網路磁碟非同步斷線自動重連機制；自定義檔案副檔名關聯。

## 2. 新增與變更模組

```text
src/
├── monitor.h/.c     # 資料夾監控核心：ReadDirectoryChangesW 後台執行緒、路徑過濾、生命週期管理
├── rename.h/.c      # 更名邏輯：ExtractPrefix 演算法、檔名非法字元校驗、MoveFileExA 封裝
├── settings.h/.c    # INI 持久化：Get/WritePrivateProfileStringA 封裝（監控路徑＋前綴記憶）
├── app.h            # 僅 compare 兩顆 ID 在此；新增 ID 一律放 main.c（§8.1）
├── main.c           # 整合選單（Rename/Monitor）、F2 加速鍵、WM_APP 訊息處理、App_PerformRename、攔截點＋待處理佇列（ring 64）
├── app.rc           # 新增對話框資源：IDD_MONITOR_SETTINGS、IDD_NEW_FILE_PROMPT、IDD_RENAME_INPUT
└── CMakeLists.txt   # 新增 src/monitor.c、src/rename.c、src/settings.c
```

依賴關係：`main → monitor`, `main → rename`, `main → settings`, `monitor → settings`。模組維持 ANSI 全 A 版 API，無第三方依賴。

### 2.1 `src/monitor.h`

```c
#ifndef ROI_MONITOR_H
#define ROI_MONITOR_H

#include <windows.h>

#define MONITOR_MAX_PATHS 3
#define WM_APP_NEW_FILE   (WM_APP + 101)

typedef struct {
    char path[MAX_PATH];
    BOOL is_active;
} monitor_config_t;

typedef enum {
    MONITOR_STATUS_ACTIVE,   /* 監控中 */
    MONITOR_STATUS_STOPPED,  /* 已停止 */
    MONITOR_STATUS_UNSET     /* 未設定 */
} monitor_status_t;

/* 初始化監控子系統（讀取 INI 設定並啟動已啟用之監控） */
BOOL Monitor_Init(HWND hwnd_notify);

/* 停止並釋放監控子系統資源 */
void Monitor_Shutdown(void);

/* 更新設定：寫入 INI 並重啟後台監控執行緒 */
BOOL Monitor_UpdateConfigs(const monitor_config_t configs[MONITOR_MAX_PATHS]);

/* 讀取目前設定快照 */
void Monitor_GetConfigs(monitor_config_t configs[MONITOR_MAX_PATHS]);

/* 查詢第 index 組（0..2）之監控狀態 */
monitor_status_t Monitor_GetStatus(int index, char *out_path, size_t cap);

/* 啟動／停止全部已設定之監控路徑 */
void Monitor_StartAll(void);
void Monitor_StopAll(void);

/* 寫入完成輪詢（僅 monitor 背景執行緒呼叫；UI 端禁止 Sleep）。
   每 100ms 以 CreateFileA(GENERIC_READ, FILE_SHARE_READ)＋GetFileSizeEx 比對連續兩次長度；
   一致且可開啟即回 TRUE；超時 timeout_ms 回 FALSE。落點 monitor.h（非 filelist.h）。 */
BOOL File_WaitForWriteComplete(const char *path, DWORD timeout_ms);

/* 顯示監控設定對話框（Modal） */
void Monitor_ShowSettingsDialog(HWND hwnd_parent);

#endif /* ROI_MONITOR_H */
```

### 2.2 `src/rename.h`

```c
#ifndef ROI_RENAME_H
#define ROI_RENAME_H

#include <windows.h>
#include <stddef.h>

/* 前綴提取演算法：比對新檔名與原檔名，提取前綴並寫入 prefix 緩衝區。
   若新檔名以原檔名結尾，取前半段；若原檔名在中間，取 IndexOf 前半段；否則回傳整個新檔名。
   回傳值：成功寫入非空前綴回傳 TRUE，無有效前綴回傳 FALSE。 */
BOOL ExtractPrefix(const char *new_name, const char *orig_name,
                   char *prefix, size_t cap);

/* 檔名合法性校驗：檢查是否包含 Windows 非法字元（<>:"/\|?*）或控制字元。
   合法回傳 TRUE；非法回傳 FALSE 並輸出錯誤訊息至 err_msg（若非 NULL）。 */
BOOL Rename_ValidateFileName(const char *file_name, char *err_msg, size_t err_cap);

/* 副檔名白名單檢查：僅支援 png / jpg / jpeg / bmp（不分大小寫）。 */
BOOL Rename_IsSupportedExtension(const char *file_path);

/* 執行檔案更名：覆寫前先備份被覆蓋目標為 .bak（無條件帶 MOVEFILE_REPLACE_EXISTING；
   overwrite 參數僅控制「未確認時是否執行」，確認流程由呼叫端 MessageBox 負責）＋同目錄
   per-image CSV 檔案連動搬移（失敗只警告、不回滾主更名）。
   成功回傳 0；失敗回傳 GetLastError() 錯誤碼。 */
DWORD Rename_Execute(const char *old_path, const char *new_name,
                     char *out_new_path, size_t cap, BOOL overwrite);

#endif /* ROI_RENAME_H */
```

### 2.3 `src/settings.h`

```c
#ifndef ROI_SETTINGS_H
#define ROI_SETTINGS_H

#include <windows.h>
#include <stddef.h>
#include "monitor.h"

/* 取得 INI 檔案完整絕對路徑（與 roi_analyzer.exe 位於同目錄下之 roi_analyzer.ini） */
void Settings_GetIniPath(char *path, size_t cap);

/* 監控資料夾組態讀寫 */
BOOL Settings_LoadMonitorConfigs(monitor_config_t configs[MONITOR_MAX_PATHS]);
BOOL Settings_SaveMonitorConfigs(const monitor_config_t configs[MONITOR_MAX_PATHS]);

/* 更名前綴記憶讀寫 */
BOOL Settings_LoadLastRenamePrefix(char *prefix, size_t cap);
BOOL Settings_SaveLastRenamePrefix(const char *prefix);

#endif /* ROI_SETTINGS_H */
```

## 3. 核心機制與演算法

### 3.1 前綴提取演算法 `ExtractPrefix`

為配合產線連拍及檢測命名習慣（例如 `Sample_001.png` 更名為 `BatchA_Sample_001.png`），系統需自動提取共通前綴 `BatchA_`：

```
輸入：new_name（新檔名主體，不含副檔名）、orig_name（原檔名主體，不含副檔名）
輸出：prefix 緩衝區（最多 cap 位元組）

步驟：
1. 取得 len_new = strlen(new_name), len_orig = strlen(orig_name)。
2. 若 len_new == 0 或 len_orig == 0，清空 prefix，回傳 FALSE。
3. 後綴比對（Suffix Match）：
   若 len_new >= len_orig，且 strcmp(new_name + (len_new - len_orig), orig_name) == 0：
     prefix_len = len_new - len_orig;
     複製 new_name 前 prefix_len 位元組至 prefix，截斷並補 '\0'；回傳 (prefix_len > 0)。
4. 中間包含比對（Infix Match）：
   搜尋 orig_name 在 new_name 中第一次出現之位置 ptr = strstr(new_name, orig_name)：
   若 ptr != NULL 且 ptr > new_name：
     prefix_len = (size_t)(ptr - new_name);
     複製 new_name 前 prefix_len 位元組至 prefix，截斷並補 '\0'；回傳 TRUE。
5. 獨立全新檔名（Fallback）：
   新檔名不包含原檔名，直接將 new_name 完整複製至 prefix；回傳 TRUE。
```

### 3.2 Win32 `ReadDirectoryChangesW` 事件驅動模型

Win32 監控目錄變更的核心函式為 `ReadDirectoryChangesW`（無 ANSI 版本，檔名回傳為 UTF-16LE）。架構採用單一監控工作執行緒配合 Overlapped 事件陣列：

| 資源項目 | 規格與設定 |
|---------|------------|
| 監控路徑數 | 最多 3 組（`MONITOR_MAX_PATHS = 3`） |
| 目錄控制代碼 | `CreateFileA(dir, FILE_LIST_DIRECTORY, FILE_SHARE_READ \| FILE_SHARE_WRITE \| FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS \| FILE_FLAG_OVERLAPPED, NULL)` |
| 變更過濾器 | `FILE_NOTIFY_CHANGE_FILE_NAME \| FILE_NOTIFY_CHANGE_LAST_WRITE` |
| 通知緩衝區 | 每路徑專屬 `BYTE buffer[4096]`（4KB 雙字邊界對齊） |
| 執行緒同步 | `HANDLE events[4]`：`events[0] = hStopEvent`，`events[1..3] = hOverlappedEvents[0..2]` |
| 訊息切換 | 透過 `WaitForMultipleObjects(count, events, FALSE, INFINITE)` 等待事件觸發 |

**通知緩衝區解析邏輯**：
1. 當 `events[i + 1]` 被喚醒，調用 `GetOverlappedResult(hDir[i], &ov[i], &bytes, FALSE)`。
2. 逐項走訪 `FILE_NOTIFY_INFORMATION` 鏈結節點。
3. 若 `Action == FILE_ACTION_ADDED` 或 `Action == FILE_ACTION_RENAMED_NEW_NAME`：
   - 將 `FileName`（WCHAR）經 `WideCharToMultiByte(CP_ACP, 0, ...)` 轉為 ANSI 檔名。
   - 檢查副檔名是否屬於白名單（`.png`、`.jpg`、`.jpeg`、`.bmp`）。
   - 組裝絕對路徑：`PathCombineA(full_path, dir_path, file_name)`。
   - 配置字串副本 `char *msg_path = _strdup(full_path)`。
   - 發送訊息：`PostMessageA(hwnd_main, WM_APP_NEW_FILE, 0, (LPARAM)msg_path)`。
4. 重新投遞 `ReadDirectoryChangesW` 請求以保持連續監聽。

### 3.3 檔名合法性校驗規則

使用者輸入檔名需通過嚴格檢查，避免檔案系統例外或路徑走訪漏洞：
- **長度限制**：不得為空，且檔名主體不得大於 `MAX_PATH - 32`。
- **非法字元**：嚴格禁止 Windows 檔案保留字元：`<`、`>`、`:`、`"`、`/`、`\`、`|`、`?`、`*` 以及 ASCII 控制碼 `0x00` 至 `0x1F`。
- **裝置保留名稱**：檔名主體不得為 `CON`, `PRN`, `AUX`, `NUL`, `COM1`~`COM9`, `LPT1`~`LPT9`（不區分大小寫）。
- **結尾限制**：檔名末端不得為句點 `.` 或半形空格 ` `。

## 4. 主流程

### 4.1 監控後台執行緒與訊息分派流程

```text
[背景執行緒: Monitor_WorkerThread]
  │
  ├── 1. 建立 hStopEvent 與 3 組 Overlapped 事件
  ├── 2. 對啟用中之路徑開啟目錄 Handle，呼叫 ReadDirectoryChangesW 投遞非同步請求
  ├── 3. 進入迴圈: WaitForMultipleObjects(1 + active_count, events, FALSE, INFINITE)
  │      │
  │      ├── [收到 hStopEvent] ──> 關閉 Handles，結束執行緒
  │      │
  │      └── [收到 Overlapped 事件]
  │             ├── GetOverlappedResult() 讀取變更節點
  │             ├── 走訪 FILE_NOTIFY_INFORMATION (篩選 ADDED / RENAMED_NEW_NAME)
  │             ├── 轉為 ANSI 檔名並進行副檔名白名單驗證
  │             ├── 組裝完整絕對路徑
  │             ├── PostMessageA(g_app.hwnd_main, WM_APP_NEW_FILE, 0, (LPARAM)_strdup(path))
  │             └── 重新呼叫 ReadDirectoryChangesW 投遞請求
  ▼
[UI 主執行緒: MainWndProc]
  │
  ├── 收到 WM_APP_NEW_FILE:
  │      ├── 取得路徑指標 char *new_path = (char *)lparam
  │      ├── 調用 App_HandleNewFileArrival(new_path)
  │      └── free(new_path)
```

### 4.2 新檔提示彈窗流程 `App_HandleNewFileArrival`

```text
1. 命名 Mutex 瞬時搶佔（僅多實例防重，不橫跨 Modal）:
   hMutex = CreateMutexA(NULL, FALSE, "RoiAnalyzer_NewFileMutex");
   若 WaitForSingleObject(hMutex, 0) != WAIT_OBJECT_0:
     釋放資源並直接返回（他實例正在彈窗，本實例丟棄）。
   搶到即 ReleaseMutex（臨界區僅「檢查 s_prompt_open＋入隊」數行）。
   單實例內新檔一律入待處理佇列（ring 64；滿則狀態列提示「有 N 個新檔被略過」，不靜默丟）。
   若主視窗已有 Modal 忙碌或 s_prompt_open==TRUE：入隊即返；待 Modal 關閉／彈窗關閉後依序取出處理。
2. 寫入緩衝延遲（背景執行緒內；UI 端禁止 Sleep）:
   背景 worker 先 Sleep(500) 起步，再調 File_WaitForWriteComplete(path, 3000)；
   超時／不存在則丟棄、不 Post。UI 收到 WM_APP_NEW_FILE 即視為就緒、直接彈窗。
3. 檔案存在性與大小確認:
   若 GetFileAttributesA(new_path) == INVALID_FILE_ATTRIBUTES:
     釋放 Mutex 並返回。
4. 未存檔防護確認:
   若 g_app.is_modified == TRUE:
     若使用者後續操作涉及主圖切換，於彈窗後或切換前調用 App_ConfirmDiscard()。
5. 顯示 NewFilePromptForm 對話框 (IDD_NEW_FILE_PROMPT):
   - 背景載入縮圖 (WIC 唯讀解碼繪製至預設靜態預覽區)。
   - 輸入框預設值 = s_persistent_prefix + 原檔名主體。
   - 焦點全選檔名輸入框。
6. 使用者操作分支 (Dialog Result):
   ├── [按鈕 1: 僅更名 (Rename Only)]
   │     - 執行 Rename_Execute() 改名。
   │     - 更新 s_persistent_prefix 與 INI LastRenamePrefix。
   │     - 若新檔所在目錄 == g_app.files.dir，執行 FileList_Refresh() 刷新主清單 (見 §7.2)。
   ├── [按鈕 2: 更名並開啟 (Rename & Open)]
   │     - （單一檢查點原則：此處不做前置 App_ConfirmDiscard，依賴 OpenImageFile 內建檢查；避免 No 時被問兩次。）
   │     - 執行 Rename_Execute() 改名。
   │     - 更新前綴記憶。
   │     - 調用 OpenImageFile(final_path) 載入為當前主影像。
   ├── [按鈕 3: 加入比較 (Add to Compare)]
   │     - 執行 Rename_Execute() 改名。
   │     - 更新前綴記憶。
   │     - 若主圖有效，將當前主圖與新檔組裝為雙圖陣列，調用 CompareV1_Open()。
   ├── [按鈕 4: 立即比較 (Compare Now)]
   │     - 執行 Rename_Execute() 改名。
   │     - 更新前綴記憶。
   │     - 若主圖有效，直接呼叫 CompareV2_Open() 開啟疊加分割線比較視窗。
   └── [按鈕 5: 取消 (Cancel)]
         - 不執行任何更名或開啟動作。
7. 釋放 Mutex:
   ReleaseMutex(hMutex); CloseHandle(hMutex);
```

### 4.3 主視窗更名流程 `App_PerformRename`

```text
1. 狀態防呆:
   - 若 !g_app.img.valid 或 g_app.img.path[0] == '\0':
     MessageBoxA 提示 "No current image to rename."，返回。
   - 若 GetFileAttributesA(g_app.img.path) == INVALID_FILE_ATTRIBUTES:
     MessageBoxA 提示 "Current image file not found on disk."，返回。
2. 未存檔防護（單一檢查點）:
   - 若 g_app.is_modified == TRUE:
     調用 App_ConfirmDiscard()；若使用者按 Cancel 或存檔失敗，終止更名流程。
   - （是／否語義同 §5.3：Yes 先 App_SaveImage；No 拋棄旋轉成果。）
   - 註：回滾分支 OpenImageFile(old_path) 會把 is_modified 清零；此時 Yes/No 已結算，可接受。
3. 比較視窗關閉:
   - 調用 Compare_CloseAll()（關閉已開啟之 V1/V2 視窗；同 UI 執行緒同步 DestroyWindow，
     可直接接 MoveFileExA，無需非同步等待）。
   - 若更名時正處拖拽中（drag.dragging），先結束拖拽（ReleaseCapture），再 ROI_Clear。
4. 預設名稱計算:
   - 讀取 INI 之 LastRenamePrefix；若為空則原檔名主體；組裝為預設名稱。
5. 彈出更名輸入對話框 (IDD_RENAME_INPUT):
   - 使用者輸入新檔名主體（不含副檔名）。
   - 若取消或輸入與原檔名相同，直接返回。
   - 檔名合法性檢驗 (Rename_ValidateFileName)；若非法彈出警示並重輸。
6. 目標覆寫檢查（含備份）:
   - 組裝 new_path = 原目錄 + 新檔名 + 原副檔名。
   - 若 GetFileAttributesA(new_path) != INVALID_FILE_ATTRIBUTES:
     彈出 MessageBoxA(MB_YESNO) 詢問 "Target file already exists. Overwrite?"；選 No 則終止。
     選 Yes：先將既有目標搬為 <new_path>.bak（已存在 .bak 則改 _conflict_N 後綴），再執行更名。
7. 關鍵順序：釋放 Handle 再搬移檔案:
   a. 暫存 old_path = g_app.img.path。
   b. Image_Free(&g_app.img);             // 釋放記憶體 32bpp BGRA 點陣
   c. ViewPyr_Free(&g_app.pyramid);       // 釋放金字塔快取
   d. g_app.pyramid_attempted = FALSE;
   e. g_app.pyramid_pending = FALSE;
   f. ROI_Clear(&g_app.rois, &g_app.drag); // 清空 ROI 項目
   g. Table_Clear(g_app.hwnd_table);      // 清空表格
   h. HistPanel_ClearSource(g_app.hwnd_hist);
   i. InvalidateRect(g_app.hwnd_canvas, NULL, TRUE);
   j. UpdateWindow(g_app.hwnd_canvas);
8. 檔案更名 API:
   - 呼叫 MoveFileExA(old_path, new_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)。
   - 若失敗 (ret == 0):
     - 取得 err = GetLastError()。
     - MessageBoxA 顯示更名失敗原因。
     - 調用 OpenImageFile(old_path) 進行回滾載入，還原視窗顯示，返回。
9. 關聯 CSV 檔案搬移:
   - 檢查是否存在 `<dir>/<old_base>_drag.csv`、`grid3x3.csv`、`grid5x5.csv`。
   - 若存在，呼叫 MoveFileExA 將其更名為 `<new_base>_<mode>.csv`。
10. 前綴記憶儲存:
    - ExtractPrefix(new_base, old_base, prefix, sizeof(prefix))。
    - 若 prefix 非空，調用 Settings_SaveLastRenamePrefix(prefix)。
11. 重新載入新影像:
    - 調用 OpenImageFile(new_path) 重新解碼載入。
    - 狀態列提示: g_nav_status = "Renamed <old_base> to <new_base>"。
    - UpdateTitle(); App_UpdateStatus();
```

## 5. 防護與攔截機制

### 5.1 寫入競態防護（Write Race Condition）

外部相機、感測器或工業掃描設備寫入圖檔通常耗時 50ms～300ms。若在檔案建立瞬間即嘗試以 WIC 解碼，將遭遇 `ERROR_SHARING_VIOLATION`。
- **第一階段防護（起步實作，背景執行緒內）**：worker 先 `Sleep(500)` 再調
  `File_WaitForWriteComplete(path, 3000)`（§2.1）；超時則丟棄、不 Post。
  **UI 端（`WM_APP_NEW_FILE` handler）禁止任何 Sleep**，收到即視為就緒（回應 Gemini §三.1）。
- **第二階段升級（T2 穩定輪詢機制）**：以專屬輪詢函式 `File_WaitForWriteComplete(path, timeout_ms)` 取代盲等：
  每 100ms 執行一次非共用開啟測試：
  `CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)`
  並比對連續兩次檔案長度（`GetFileSizeEx`）。長度一致且 Handle 能成功取得時判定寫入完成；超時（如 3000ms）則放棄彈窗並輸出除錯紀錄。

### 5.2 多 Process / 多 Instance 防重（具名 Mutex）

若使用者同時開啟多個 ROI Analyzer 執行個體，當新檔案產生時，多個 Process 的背景監控將同時捕捉事件。
- 採用全域具名 Mutex：`"RoiAnalyzer_NewFileMutex"`，**瞬時持有**（`WaitForSingleObject(hMutex, 0)`；
  臨界區僅「檢查 `s_prompt_open`＋入待處理佇列」數行，搶到即 `ReleaseMutex`，絕不橫跨 Modal）。
- 搶不到→本實例丟棄（他實例處理中）；搶到→單實例內一律入待處理佇列（ring 64；
  滿則狀態列提示「有 N 個新檔被略過」，不靜默丟）。
- `s_prompt_open` 重入旗標：彈窗開著時到達的新檔只入隊不重彈；當前彈窗關閉後依序取出下一個。
- 主視窗已有其他 Modal（開檔對話框／旋轉角度框／MessageBox）時同樣只入隊，待無 Modal 時機再彈（回應 Gemini §一.1）。
- `WM_DESTROY` 關機序列：先 `Monitor_Shutdown()`（`CancelIoEx`→`SetEvent(hStop)`→join→關 handle），
  再排空未處理的 `WM_APP_NEW_FILE`（`free` 其 LPARAM `_strdup` 路徑），防洩漏／野指標。
  ```c
  /* 瞬時持有範式：臨界區僅數行，絕不橫跨 Modal */
  HANDLE hMutex = CreateMutexA(NULL, FALSE, "RoiAnalyzer_NewFileMutex");
  if (WaitForSingleObject(hMutex, 0) != WAIT_OBJECT_0) {
      if (hMutex) CloseHandle(hMutex);
      return; /* 他實例處理中，本實例丟棄 */
  }
  Queue_Push(&s_pending, new_path); /* 單實例待處理佇列（ring 64） */
  ReleaseMutex(hMutex);
  CloseHandle(hMutex);
  ```

### 5.3 旋轉未存檔 `is_modified` 互動防護

v2.8 定稿中引入了 `is_modified` 旗標與 `App_ConfirmDiscard()` 機制。更名與監控提示均涉及原檔更替或切換，必須納入攔截防護：
1. **主圖更名攔截**：執行 `App_PerformRename` 第一時間即檢查 `is_modified`。若為 TRUE，彈出確認對話框。
   - 選 **Yes**：調用 `App_SaveImage()` 存檔。若存檔成功，`is_modified` 轉為 FALSE，繼續更名流程；若存檔失敗則終止更名。
   - 選 **No**：拋棄記憶體中之旋轉成果，以磁碟原始檔案進行更名。
   - 選 **Cancel**：中斷當前更名，保留原狀。
2. **監控新檔「更名並開啟」攔截**：若使用者點選「更名並開啟」，因需載入新影像取代當前影像，同樣優先觸發 `App_ConfirmDiscard()`。

### 5.4 檔案 Handle 釋放順序

Windows 系統對開啟中的檔案實施嚴格鎖定。更名前若未完整釋放，`MoveFileExA` 必失敗。
釋放順序規格：
```
[1] Image_Free(&g_app.img)        --> 釋放 px 緩衝區，path[0]='\0', valid=FALSE
[2] ViewPyr_Free(&g_app.pyramid)  --> 釋放多層金字塔 Bitmap 快取
[3] ROI_Clear / Table_Clear       --> 清空分析矩形與清單視圖項目
[4] HistPanel_ClearSource         --> 斷開直方圖計算源引用
[5] InvalidateRect / UpdateWindow --> 強迫畫布完成重繪，斷開 GDI DC 關聯
[6] MoveFileExA(...)              --> 執行系統檔案更名
[7] OpenImageFile(new_path)       --> 重新開啟新檔案
```

## 6. 關鍵函式與實作

### 6.1 `src/settings.c` — INI 持久化

```c
#include "settings.h"
#include <shlwapi.h>
#include <stdio.h>

void Settings_GetIniPath(char *path, size_t cap)
{
    GetModuleFileNameA(NULL, path, (DWORD)cap);
    PathRemoveFileSpecA(path);
    PathAppendA(path, "roi_analyzer.ini");
}

BOOL Settings_LoadMonitorConfigs(monitor_config_t configs[MONITOR_MAX_PATHS])
{
    char ini[MAX_PATH], key[32];
    int i;
    Settings_GetIniPath(ini, sizeof(ini));
    for (i = 0; i < MONITOR_MAX_PATHS; i++) {
        snprintf(key, sizeof(key), "Path%d", i);
        GetPrivateProfileStringA("Monitor", key, "", configs[i].path, MAX_PATH, ini);
        snprintf(key, sizeof(key), "Active%d", i);
        configs[i].is_active = GetPrivateProfileIntA("Monitor", key, 0, ini) != 0;
    }
    return TRUE;
}

BOOL Settings_SaveMonitorConfigs(const monitor_config_t configs[MONITOR_MAX_PATHS])
{
    char ini[MAX_PATH], key[32], val[16];
    int i;
    Settings_GetIniPath(ini, sizeof(ini));
    for (i = 0; i < MONITOR_MAX_PATHS; i++) {
        snprintf(key, sizeof(key), "Path%d", i);
        WritePrivateProfileStringA("Monitor", key, configs[i].path, ini);
        snprintf(key, sizeof(key), "Active%d", i);
        snprintf(val, sizeof(val), "%d", configs[i].is_active ? 1 : 0);
        WritePrivateProfileStringA("Monitor", key, val, ini);
    }
    return TRUE;
}

BOOL Settings_LoadLastRenamePrefix(char *prefix, size_t cap)
{
    char ini[MAX_PATH];
    Settings_GetIniPath(ini, sizeof(ini));
    GetPrivateProfileStringA("Rename", "LastRenamePrefix", "", prefix, (DWORD)cap, ini);
    return prefix[0] != '\0';
}

BOOL Settings_SaveLastRenamePrefix(const char *prefix)
{
    char ini[MAX_PATH];
    Settings_GetIniPath(ini, sizeof(ini));
    return WritePrivateProfileStringA("Rename", "LastRenamePrefix", prefix ? prefix : "", ini);
}
```

### 6.2 `src/rename.c` — `ExtractPrefix` 與合法性校驗

```c
#include "rename.h"
#include <string.h>
#include <shlwapi.h>

/* 裝置保留名稱表（不分大小寫；含副檔名前主體比對）。 */
static BOOL is_reserved_base(const char *base, size_t len)
{
    static const char *const reserved[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    size_t i;
    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        size_t rlen = strlen(reserved[i]);
        if (len == rlen && _strnicmp(base, reserved[i], rlen) == 0)
            return TRUE;
    }
    return FALSE;
}

BOOL ExtractPrefix(const char *new_name, const char *orig_name,
                   char *prefix, size_t cap)
{
    size_t len_new, len_orig;
    const char *sub;
    if (!new_name || !orig_name || !prefix || cap == 0)
        return FALSE;
    prefix[0] = '\0';
    len_new = strlen(new_name);
    len_orig = strlen(orig_name);
    if (len_new == 0 || len_orig == 0)
        return FALSE;

    /* 1. 後綴完全符合：new_name 結尾是 orig_name */
    if (len_new >= len_orig &&
        strcmp(new_name + (len_new - len_orig), orig_name) == 0) {
        size_t diff = len_new - len_orig;
        if (diff >= cap) diff = cap - 1;
        strncpy(prefix, new_name, diff);
        prefix[diff] = '\0';
        return diff > 0;
    }

    /* 2. 中間包含符合：orig_name 在 new_name 中間某處 */
    sub = strstr(new_name, orig_name);
    if (sub && sub > new_name) {
        size_t diff = (size_t)(sub - new_name);
        if (diff >= cap) diff = cap - 1;
        strncpy(prefix, new_name, diff);
        prefix[diff] = '\0';
        return TRUE;
    }

    /* 3. 獨立名稱：回傳整個 new_name */
    strncpy(prefix, new_name, cap - 1);
    prefix[cap - 1] = '\0';
    return TRUE;
}

BOOL Rename_ValidateFileName(const char *file_name, char *err_msg, size_t err_cap)
{
    static const char invalid_chars[] = "<>:\"/\\|?*";
    const char *p;
    size_t len;
    if (!file_name || !file_name[0]) {
        if (err_msg && err_cap) strncpy(err_msg, "File name cannot be empty.", err_cap);
        return FALSE;
    }
    len = strlen(file_name);
    if (len > 228) { /* MAX_PATH(260) - 32，與 §3.3 對齊 */
        if (err_msg && err_cap) strncpy(err_msg, "File name exceeds maximum length.", err_cap);
        return FALSE;
    }
    for (p = file_name; *p; p++) {
        if ((unsigned char)*p < 32 || strchr(invalid_chars, *p)) {
            if (err_msg && err_cap)
                snprintf(err_msg, err_cap, "Contains invalid character: '%c'", *p);
            return FALSE;
        }
    }
    if (file_name[len - 1] == ' ' || file_name[len - 1] == '.') {
        if (err_msg && err_cap)
            strncpy(err_msg, "File name cannot end with a space or dot.", err_cap);
        return FALSE;
    }
    if (is_reserved_base(file_name, len)) { /* §3.3 裝置保留名稱 */
        if (err_msg && err_cap)
            strncpy(err_msg, "File name is a reserved device name.", err_cap);
        return FALSE;
    }
    return TRUE;
}

BOOL Rename_IsSupportedExtension(const char *file_path)
{
    const char *ext = PathFindExtensionA(file_path);
    if (!ext) return FALSE;
    return (_stricmp(ext, ".png") == 0 ||
            _stricmp(ext, ".jpg") == 0 ||
            _stricmp(ext, ".jpeg") == 0 ||
            _stricmp(ext, ".bmp") == 0);
}
```

## 7. 語義區分與政策決策

### 7.1 比較視窗開啟中更名主圖的語義拍板

> **【拍板定案】主圖更名時，自動關閉所有已開啟之比較視窗（調用 `Compare_CloseAll()`）。**

- **理由**（審查修正：比較圖為 `CmpImage_FromImage` 記憶體拷貝／`CmpImage_Load` 解碼後即關檔，
  並無常駐檔案 handle，`SHARING_VIOLATION` 風險低；關閉的真正理由如下）：
  1. 避免舊像素殘留＋指標語義懸空，與 v2.8 旋轉定稿（§1.5、§4.2）架構哲學一致。
  2. 同 UI 執行緒同步 `DestroyWindow`，可直接接 `MoveFileExA`，無需非同步等待。
  3. 禁止更名（Modal 報錯）將造成使用者頻繁遭遇阻斷，體驗不佳；自動關閉最簡潔且具備高度防禦性。

### 7.2 僅更名不開啟時之 `filelist` 刷新拍板

> **【拍板定案】若新檔所在目錄與主程式目前目錄相同（`_stricmp(folder, g_app.files.dir) == 0`），即使使用者點選「僅更名」，亦強制執行 `FileList_Refresh`＋`FileList_Find(basename)` 重算 `file_idx`＋`App_StatusSetIndex()`＋`App_UpdateStatus()`。**

- **理由**：
  1. 產線或連拍檢測場景下，操作者常藉由提示彈窗迅速將新檔重新命名（例如 `001.png` → `NG_001.png`），隨後在主視窗按 `Next` 鍵檢視。
  2. 若不主動刷新，內部 `files` 清單將殘留舊檔名，導致導覽時觸發 `Image_Load` 解碼失敗，甚至狀態列顯示 `No loadable image`。
  3. 若新檔位於其他未開啟之監控路徑，則不碰觸當前 `files`，避免無效磁碟 I/O。
- **守衛**：`g_app.files.dir` 為空（從未掃描）時 `_stricmp` 比對恆假→不觸發，加空目錄守衛。
- **註**：`FileList_Refresh` 第二參數是「從 image 路徑推導目錄」；「僅更名」分支主圖路徑沒變，推導出的仍是主目錄，碰巧可用。

### 7.3 `Ctrl+S` 存檔語義維持不變

- 主視窗 `Ctrl+S` 唯一對應 `IDM_SAVE_IMAGE`（旋轉後覆寫原圖存檔），與更名功能完全解耦。
- 更名專屬快捷鍵為 `F2`（符合 Windows 檔案總管更名標準習慣）。

### 7.4 關聯 ROI Log 檔案更名策略

`roi-analyzer` 的匯出機制會於影像同目錄產出 `<image>_<mode>.csv`（例如 `sample_grid3x3.csv`、`sample_drag.csv`）。
- **處理策略**：更名主影像時，走訪檢查並連動搬移存在之關聯 CSV 檔案：
  - `<dir>/<old_base>_drag.csv` → `<dir>/<new_base>_drag.csv`
  - `<dir>/<old_base>_grid3x3.csv` → `<dir>/<new_base>_grid3x3.csv`
  - `<dir>/<old_base>_grid5x5.csv` → `<dir>/<new_base>_grid5x5.csv`
- 確保更名後點選 `CSV > Open CSV File` 仍能正確開啟歷史分析紀錄，維持資料連續性。
- **錯誤語義**：log 搬移失敗（被編輯器佔住）**只警告、不回滾主更名**；先搬 log 後 `OpenImageFile(new)` 若解碼失敗，
  log 已改名而圖沒載入——不一致但可接受（註明）。
- **INI 落點**：`roi_analyzer.ini` 落 exe 同目錄（可攜式前設）；Program Files 下無寫權限時不做 fallback（註明限制）。

### 7.5 單一檢查點原則（消除雙重 ConfirmDiscard）

- `OpenImageFile`／`App_CompareOpenPaths`／`CompareV2_Open` 內建 `App_ConfirmDiscard`；
  所有呼叫端（§4.2 按鈕 2／按鈕 3／按鈕 4、§4.3 除外之新檔分支）**不做前置 `ConfirmDiscard`**。
  現有 `App_CompareOpenPaths`＋內部 `OpenImageFile` 已有同樣雙重詢問毛病，新分支不再重蹈。
- 比較分支 `CompareV1_Open`／`CompareV2_Open` 後的 `Unref` 參照 `main.c:1389-1390` 模式必須實作，否則洩漏 `cmp_image_t`；
  無主圖時兩顆比較按鈕禁用（`EnableWindow(FALSE)`）或提示，二選一實作。
- 新檔名預設值公式寫死：`default = (LastRenamePrefix 非空 ? prefix : "") + 原檔名主體`（無分隔符；空前綴回退原名）。
- `g_discard_approved`（每輪迴圈重置）與新 `WM_APP_NEW_FILE` 分支的互動：新分支不碰該旗標（一行規定）。

## 8. 選單／加速鍵／標記

### 8.1 選單命令 ID 分配

新增 ID 一律放 `main.c` 頂部 `#define`（與 File/Mode/Edit 既有風格一致；`app.h` 僅保留 Compare 兩顆，不新增）。
避開既有 ID（File 101-106、Mode 111-114、Rot 115-118、Edit 121-123、Log 131-132、Zoom 141-142、Hist 151/161-166、Compare 155-156）：

| ID 常數 | 數值 | 選單位置 | 顯示文字 | 說明 |
|---------|------|----------|----------|------|
| `IDM_RENAME_FILE` | 107 | File（Save Image 之後） | `Rename File...\tF2` | 主影像檔案更名 |
| `IDM_MONITOR_SETTINGS` | 108 | File（Export CSV 之後） | `Folder Monitor Settings...` | 開啟資料夾監控設定 |

### 8.2 對話框與控制項 ID

| ID 常數 | 數值 | 類型 | 說明 |
|---------|------|------|------|
| `IDD_MONITOR_SETTINGS` | 210 | Dialog | 監控資料夾設定視窗 |
| `IDC_MON_PATH1..3` | 211..213 | Edit | 監控路徑 1～3 輸入框 |
| `IDC_MON_BROWSE1..3` | 214..216 | Button | 瀏覽路徑按鈕 |
| `IDC_MON_ACTIVE1..3` | 217..219 | Checkbox | 啟用勾選框 |
| `IDD_NEW_FILE_PROMPT` | 220 | Dialog | 新檔案提示彈窗（TopMost） |
| `IDC_PROMPT_THUMB` | 221 | Static | 縮圖顯示區（OwnerDraw） |
| `IDC_PROMPT_NAME` | 222 | Edit | 新檔名輸入框 |
| `IDC_PROMPT_BTN_RENAME` | 223 | Button | 僅更名（Rename Only） |
| `IDC_PROMPT_BTN_RENAME_OPEN` | 224 | Button | 更名並開啟（Rename & Open） |
| `IDC_PROMPT_BTN_COMPARE_ADD` | 225 | Button | 加入比較（Add to Compare） |
| `IDC_PROMPT_BTN_COMPARE_NOW` | 226 | Button | 立即比較（Compare Now） |
| `IDD_RENAME_INPUT` | 230 | Dialog | File > Rename 輸入對話框 |
| `IDC_RENAME_EDIT` | 231 | Edit | 新檔名輸入框 |

### 8.3 加速鍵與狀態控制

- 加速鍵表加入：`{ FVIRTKEY, VK_F2, IDM_RENAME_FILE }`（`main.c` 加速鍵表）。
- 選單灰化：無有效影像載入時（`!g_app.img.valid`），`IDM_RENAME_FILE` 追加進 `App_UpdateImageMenu()` 灰化陣列
  （目前只有 SAVE/ROT），以 `EnableMenuItem(..., MF_GRAYED)` 禁用。
- 狀態列提示：監控啟動時於除錯或狀態列簡要顯示 `Monitor: 2 active`；更名成功顯示 `Renamed <old> to <new>`。

## 9. 驗收項目

### 9.1 監控子系統驗收項（M1～M8）

| # | 驗收情境 | 測試方法與操作 | 預期通過標準 |
|---|----------|----------------|--------------|
| M1 | 監控設定對話框 | 點選 `File > Folder Monitor Settings...` | 正確顯示 3 組路徑與勾選框；能瀏覽選取目錄；點 Save 後寫入 `roi_analyzer.ini`，重啟後設定完整保留 |
| M2 | 新檔寫入即時偵測 | 啟動監控後，外部複製 `test_new.png` 至監控目錄 | 500ms 內彈出 `NewFilePromptForm`，標題顯示來源路徑，輸入框獲得焦點且文字全選 |
| M3 | 縮圖正常載入 | 寫入大尺寸 JPEG 或 PNG 圖檔 | 彈窗正確繪製縮圖，維持長寬比例置中，無 GDI 洩漏，無檔案鎖死（可讀寫） |
| M4 | 僅更名不開啟 | 在彈窗輸入新名並按「僅更名」 | 磁碟檔案成功改名；若在當前瀏覽目錄，主視窗 `FileList_Refresh` 生效，按 Next 鍵可正確導覽至該檔 |
| M5 | 更名並開啟 | 在彈窗按「更名並開啟」 | 磁碟檔案更名，主視窗畫布立即載入該新圖，標題列與狀態列索引更新，ROI 清除並依目前模式初始化 |
| M6 | 加入與立即比較 | 當主圖有效時，於新檔彈窗按「立即比較」 | 檔案完成更名，立即開啟 V2 比較視窗，左側為原主圖、右側為新圖，支援分割線互動 |
| M7 | 非法副檔名過濾 | 複製 `data.txt`、`script.bat`、`image.gif` 至監控目錄 | 監控後台靜默略過，不彈出提示視窗，不影響程式執行 |
| M8 | 多實例 Mutex 防重＋單實例佇列 | 同時開啟兩個 `roi_analyzer.exe`，產生新圖檔；單實例連拍 5 檔不關彈窗 | 他實例搶不到 Mutex 丟棄、無雙重彈窗；單實例 5 檔全入 ring 佇列、依序彈出不漏單（滿 64 則狀態列提示，不靜默丟） |
| M9 | Modal 忙碌／重入不重彈 | 開檔對話框／旋轉角度框開著時新檔到達；彈窗開著時第二檔到達 | 只入隊不重彈；Modal 關閉／當前彈窗關閉後依序取出處理，無 Z-Order 錯亂 |

### 9.2 更名子系統驗收項（R1～R8）

| # | 驗收情境 | 測試方法與操作 | 預期通過標準 |
|---|----------|----------------|--------------|
| R1 | F2 / 選單更名彈窗 | 開啟有效影像，按下 `F2` 或選單 `File > Rename File...` | 彈出更名輸入框，預設值為 `LastRenamePrefix + 原檔名`，原名稱高亮選取 |
| R2 | 正常檔案更名 | 輸入合法新檔名（如 `Sample_OK`）並確認 | 檔案釋放後 `MoveFileExA` 成功，自動重新載入新影像，狀態列顯示 `Renamed to Sample_OK.png` |
| R3 | 前綴自動推導記憶 | 原圖 `IMG_001.png` 更名為 `Batch1_IMG_001.png` | `ExtractPrefix` 正確提取 `Batch1_` 並存入 INI；下次更名預設名稱自動帶入 `Batch1_` |
| R4 | 非法字元防護 | 輸入含 `*?<>:"/\|` 或全形空格之檔名 | 彈出錯誤訊息提示非法字元，阻止更名並保留於輸入對話框，磁碟原檔不變 |
| R5 | 目標已存在覆寫防護（含備份） | 輸入同一目錄下已存在之檔名 | 彈出 MessageBox 詢問是否覆寫：選 No 返回；選 Yes 先將既有目標搬為 `.bak`（已存在則 `_conflict_N`），再覆寫並重新載入 |
| R6 | Handle 徹底釋放 | 於高解析度圖檔（含金字塔快取與分區 ROI）執行更名 | 記憶體、DC、金字塔及表格確實釋放，更名不回傳 `ERROR_SHARING_VIOLATION` |
| R7 | 關聯 CSV 檔案連動 | 對已匯出 `_grid3x3.csv` 之影像進行更名 | 同目錄下之 `<old>_grid3x3.csv` 自動更名為 `<new>_grid3x3.csv`，無日誌遺失 |
| R8 | 旋轉未存檔互動防護 | 將影像旋轉 90° 後直接按 `F2` 更名 | 觸發 `App_ConfirmDiscard()`：選 Yes 存檔後更名；選 Cancel 中斷更名，保留旋轉標記 `*` |

## 10. 建置調整

### 10.1 `CMakeLists.txt`

```cmake
add_executable(roi_analyzer
    src/app.rc
    src/main.c
    src/canvas.c
    src/filelist.c
    src/image.c
    src/image_wic.c
    src/view.c
    src/roi.c
    src/analyze.c
    src/table.c
    src/export.c
    src/histogram.c
    src/histpanel.c
    src/compare_image.c
    src/compare_core.c
    src/compare_snap.c
    src/compare_v1.c
    src/compare_v2.c
    src/rotate.c
    src/image_save.c
    src/settings.c
    src/rename.c
    src/monitor.c
)
```

既有連結程式庫已包含 `kernel32`, `user32`, `gdi32`, `comctl32`, `comdlg32`, `shlwapi`, `windowscodecs`, `ole32`，無需增加額外鏈結庫。編譯旗標維持 `-Wall -Wextra` 零警告。

### 10.2 資源檔 `src/app.rc` 擴充

新增 `IDD_MONITOR_SETTINGS`、`IDD_NEW_FILE_PROMPT` 與 `IDD_RENAME_INPUT` 對話框範本（字體設定為 `MS Shell Dlg`, 9 pt）。

## 11. 實作順序（派工用）

依模組相依性與測試隔離度，切分為 T1～T5 五大階段：

| 任務 | 範圍與內容 | 產出與行數預估 | 驗收標準 |
|------|------------|----------------|----------|
| **T1** | **設定持久化與 UI**：建立 `settings.h/.c`，支援監控組態與前綴讀寫；在 `app.rc` 建立 `IDD_MONITOR_SETTINGS` 對話框與事件迴圈 | `src/settings.h/.c` (~120 行)<br>`src/app.rc` (~60 行) | M1 通過：設定可儲存至 INI，重啟程式後正確讀回 |
| **T2** | **監控核心與後台執行緒**：實作 `monitor.h/.c`，封裝 `ReadDirectoryChangesW`、Overlapped 事件迴圈（稠密壓縮＋對照表；4KB 溢位即放棄＋OutputDebugString）、白名單過濾、`Sleep(500)`＋`File_WaitForWriteComplete`（背景執行緒內；CP_ACP 先經 `wide_to_acp_strict` 過濾 `?`；`bWatchSubtree=FALSE`） | `src/monitor.h/.c` (~350 行) | 外部新增圖檔時，後台執行緒能即時捕捉並發送 `WM_APP_NEW_FILE` 至主視窗；連拍 burst 無凍結、無漏單 |
| **T3** | **新檔提示視窗與比較串接**：在 `app.rc` 新增 `IDD_NEW_FILE_PROMPT`；實作縮圖繪製、5 個按鈕事件分支（單一檢查點：不做前置 ConfirmDiscard；比較分支 `Unref` 參照 `main.c:1389-1390`；無主圖時比較按鈕禁用）、`s_prompt_open`＋ring 佇列＋瞬時 Mutex、`WM_DESTROY` 關機排空 | `src/main.c` 擴充 (~200 行)<br>`src/app.rc` (~80 行) | M2～M9 通過：彈窗介面完整，更名／開啟／比較串接正確，雙開無重複彈窗，Modal 忙碌不重彈 |
| **T4** | **更名核心與 Handle 釋放順序**：實作 `rename.h/.c`（`ExtractPrefix`、合法性校驗、MoveFile 封裝）；在 `main.c` 整合 `App_PerformRename`、F2 快捷鍵、未存檔防護與關聯 log 搬移 | `src/rename.h/.c` (~150 行)<br>`src/main.c` 擴充 (~130 行)<br>`src/app.rc` (~40 行) | R1～R8 通過：更名無 Handle 鎖死衝突，前綴記憶推導正確，未存檔防護無漏洞 |
| **T5** | **整體建置與整合驗收**：調整 `CMakeLists.txt`，執行 Release Clean 全量編譯（`-Wall -Wextra` 0 warning）；進行 M1～M9 與 R1～R8 驗收測試 | `CMakeLists.txt` (~5 行)<br>驗證紀錄補充 | 編譯 0 警告、0 錯誤；產出 `bin/roi_analyzer.exe`；功能驗收全數通過 |

**程式碼行數預估**（審查修訂）：
- 新增模組合計約 620 行（`settings` 約 120 行、`rename` 約 150 行、`monitor` 約 350 行）。
- 既有檔案變更合計約 560 行（`main.c` 約 330 行、`app.rc` 約 180 行、其餘約 50 行）。
- 專案總計擴充約 1,100～1,200 行純 C / 資源程式碼；T2/T3 各加 1 天 buffer。
