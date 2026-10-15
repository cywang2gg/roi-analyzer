# 拖放圖片分流（取代／比較詢問）架構書 v1.0

> 專案：`C:\Github\roi-analyzer`（純 C ＋ Win32 ＋ GDI，ANSI 全 A 版，`-Wall -Wextra` 零警告，C11，宣告置頂，禁 `//` 註解）
> 基準：`master`（`15242f5`，v3.6）
> 需求：**已開圖**時拖入新圖，詢問「取代」或「比較」；**拖 2–4 張＝直接進比較**（不詢問）；**取消 Ctrl 快捷**（不再提供隱藏手勢）
> 日期：2026-10-08
> 狀態：**v1.1（併入 agy round-1：核心更正單圖 Compare 走 `CompareV2_OpenMode`、雙重詢問修正、行號更正）→ GO，發包 gh**

## 變更歷史

| 版本 | 日期 | 內容 |
|---|---|---|
| v1.0 | 2026-10-08 | 初稿；使用者拍板：2–4 張直接比較、移除 Ctrl 手勢 |
| v1.1 | 2026-10-08 | agy round 1（NO-GO→修正）：(1) **核心拍板更正——單圖 Compare 走 `CompareV2_OpenMode`，不走 `App_CompareOpenPaths`**（實碼 `App_CompareOpenPaths` 內部開 V1；1v1 比照 `PROMPT_ACTION_COMPARE_NOW` 慣例：`Compare_CanOpen(2)`＋`CmpImage_FromImage`/`CmpImage_Load`＋`CompareV2_OpenMode(a,b,CMP_ANA_NONE)`＋Unref）；(2) **雙重詢問修正**——`OpenImageFile`（`main.c:1133`）**內部已含 `App_ConfirmDiscard()`**，Replace 分支直接呼叫 `OpenImageFile` 即可，不得再前置呼叫；(3) 行號更正（F2=1204-1243、F4=compare.h:111-113、F6=OpenImageFile 定義 1133/`App_ReplaceImage` 1147）；(4) `IDD/IDC` 須於 `app.rc` 與 `main.c` **雙邊 #define** |

---

## 1. 現況（實碼）

| # | 事實 | 證據 |
|---|---|---|
| F1 | `App_OnDropFiles` 一律先 `App_ConfirmDiscard()`（未存檔警告）→ `CmpDrop_Collect` → `App_CompareOpenPaths` | `main.c:1305-1312` |
| F2 | `App_CompareOpenPaths(paths, count, mode)`：**開頭即 `App_ConfirmDiscard()`**；`CMP_OPEN_MAIN_FIRST` 時先 `OpenImageFile(paths[0])`（取代主窗）再開 **V1**（`CompareV1_Open`，main.c:1276）；`CMP_OPEN_WITH_CURRENT` 時以 `CmpImage_FromImage(&g_app.img)` 把現圖加進 V1 | `main.c:1204-1243` |
| F3 | Ctrl 手勢：`include_current = GetAsyncKeyState(VK_CONTROL)`，決定 mode；未按時訊息附註「Ctrl+drop to include current image」 | `main.c:1309,1326-1335` |
| F4 | 現成比較入口：V1＝`CompareV1_Open`；V2＝`CompareV2_OpenMode(a, b, CMP_ANA_NONE)` | `compare.h:111-113` |
| F5 | `IDD_NEW_FILE_PROMPT`（220）是既有「監控新檔詢問」dialog 慣例：thumb＋文字＋多按鈕 | `app.rc:51-66` |
| F6 | 單檔取代入口＝`OpenImageFile(path)`（`main.c:1133` 定義；**內部首行即 `App_ConfirmDiscard()`**；內部呼叫 `App_ReplaceImage` 於 1147） | `main.c:1133,1137,1147` |

## 2. 新行為矩陣（拍板後）

| 拖入 | 無圖開著 | 有圖開著 |
|---|---|---|
| 1 張 | 直接取代（現行，不詢問） | **Dialog 詢問**：Replace／Compare／Cancel |
| 2–4 張 | 現行（主窗第一張＋V1 全部） | **直接比較**：V1＝現圖＋全部拖入（不詢問、不取代） |

- **移除 Ctrl 手勢**（F3 整段刪除，含訊息附註）；`cmp_open_mode_t` 的 `CMP_OPEN_WITH_CURRENT` 保留（對話框與 2–4 張路徑共用）。
- **未存檔警告（`App_ConfirmDiscard`）時機改變**：從「收集前」移到「使用者選了 Replace 之後」——Compare 路徑不動現圖，不該被未存檔提示擋住；Cancel 路徑也不動。

## 3. 設計

### 3.1 `App_OnDropFiles` 重排

```
收集（CmpDrop_Collect，不變；失敗提示不變）
├─ count==0 → 現行提示
├─ 無圖開著（!g_app.img.valid）
│   └─ 現行流程：App_CompareOpenPaths(…, CMP_OPEN_MAIN_FIRST)
│       （1 張=取代；2–4 張=主窗第一＋V1——皆不受本需求影響）
└─ 有圖開著（g_app.img.valid）
    ├─ count==1 → Dialog IDD_REPLACE_OR_COMPARE
    │   ├─ Replace → OpenImageFile(paths[0])
    │   │            （v1.1：OpenImageFile 內部已含 App_ConfirmDiscard，
    │   │              不得再前置呼叫——否則雙重詢問）
    │   ├─ Compare → **CompareV2_OpenMode 路徑**（v1.1 核心更正：
    │   │            不走 App_CompareOpenPaths——它內部開的是 V1）
    │   │            if (!Compare_CanOpen(2)) 提示上限;
    │   │            a = CmpImage_FromImage(&g_app.img);      /* 現圖 */
    │   │            b = CmpImage_Load(paths[0]);              /* 新圖 */
    │   │            成功→ CompareV2_OpenMode(a, b, CMP_ANA_NONE);
    │   │            末段 Unref a、b（比照 main.c PROMPT_ACTION_COMPARE_NOW 慣例）
    │   └─ Cancel  → return（不動任何狀態）
    └─ count>=2 → 直接比較（無詢問）：
        App_CompareOpenPaths(…, CMP_OPEN_WITH_CURRENT)   /* V1=現圖＋全部拖入 */
```

- **比較路徑**：`App_CompareOpenPaths` 內 `App_ConfirmDiscard()` **須移除/條件化**（見 §3.3）——它現在開頭就問，會在 Compare 分支誤擋。
- **2–4 張時 `CMP_OPEN_WITH_CURRENT` 語義即「現圖＋全部」**：F2 既有程式已把 `paths[]` 與現圖去重（同路徑跳過）、依 CMP_MAX_CELLS=4 上限取前 N——多於 4 張時既有的 `beyond the four-image limit` 統計照舊提示。

### 3.2 新 Dialog `IDD_REPLACE_OR_COMPARE`（ID 250，`app.rc`）

```
IDD_REPLACE_OR_COMPARE DIALOGEX 0, 0, 240, 70
STYLE DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU
CAPTION "Image Already Open"
FONT 9, "MS Shell Dlg"
BEGIN
    LTEXT   "Drop N: <檔名>", IDC_ROC_TEXT, 7, 7, 226, 24
    DEFPUSHBUTTON "Replace", IDC_ROC_REPLACE, 24, 40, 60, 16
    PUSHBUTTON    "Compare",  IDC_ROC_COMPARE, 92, 40, 60, 16
    PUSHBUTTON    "Cancel",   IDC_ROC_CANCEL,  160, 40, 60, 16
END
```

| # | 值 |
|---|---|
| `IDD_REPLACE_OR_COMPARE` | 250（既有最大 240；無衝突） |
| `IDC_ROC_TEXT/REPLACE/COMPARE/CANCEL` | 251/252/253/254 |
| 字型 | `MS Shell Dlg` 9（與 `IDD_RENAME_INPUT` 等同） |
| 文字 | `LTEXT` 內容執行期 `_snprintf` 填「`Drop: %s` 」＋副檔名提示（檔名 `PathFindFileNameA`，DBCS 安全沿用 shlwapi 慣例） |
| 回傳 | DialogBoxParamA 回 IDC_ROC_REPLACE / IDC_ROC_COMPARE / IDCANCEL（Esc） |
| **ID 定義位置（v1.1）** | `app.rc` 與 `main.c` **雙邊 #define**（專案慣例：兩檔各持一份） |

- **thumb 縮圖不做**（v1 從簡；監控提示那套 `WIC 縮圖` 可日後沿用）。
- 對話框為 modal；拖放訊息已在 drop 處理內，modal 不影響。

### 3.3 `App_CompareOpenPaths` 的 `ConfirmDiscard` 移除

- 現況 F2：函式開頭 `if (!App_ConfirmDiscard()) return FALSE;`——它同時服務「取代（MAIN_FIRST 會 OpenImageFile 換掉現圖）」與「比較（不換現圖）」兩種語義。
- **定案（v1.1）**：從 `App_CompareOpenPaths` **移除**。呼叫點盤點（agy 已核對，共 3 處）：
  1. `App_OnDropFiles`（重構後見 §3.1）
  2. `CompareFilesDialog`（Ctrl+K）——純比較，移除後行為更正確
  3. `App_HandleNewFileArrival`「加入比較」——純比較，同上
  - `CMP_OPEN_MAIN_FIRST` 內部的取代保護由 `OpenImageFile` 內建 `App_ConfirmDiscard()` 承接（`main.c:1133,1137`），無保護空窗。
  - 單圖 Compare 分支不走此函式（§3.1 v1.1），自帶 `Compare_CanOpen(2)` 檢查。
- 狀態列訊息「Ctrl+drop to include current image」附註隨 Ctrl 手勢一併刪除。

### 3.4 不動項

- 無圖時的行為（1 張取代／2–4 張 V1）——現行即符合矩陣左欄。
- `CmpDrop_Collect`、drop 統計提示、`Compare_CanOpen` 上限、`App_HandleNewFileArrival`（監控新檔流程）、`cmp_open_mode_t` 列舉。
- 資料夾監控的新檔詢問 dialog（220）不變。

## 4. 實作分段（一 commit）

| 階段 | 檔案 | 內容 |
|---|---|---|
| **D1** | `app.rc`、`resource` 定義（`app.rc` 內 #define，沿用現況集中法）、`main.c` | Dialog＋`App_OnDropFiles` 重排＋`App_CompareOpenPaths` ConfirmDiscard 移除＋Ctrl 手勢刪除 |

**檔案範圍**：`src/app.rc`、`src/main.c`（僅此 2 檔；`compare.h/.c` 零改動——開啟函式皆現成）。

## 5. 驗收清單

| # | 項目 | 預期 |
|---|---|---|
| T1 | 無圖＋拖 1 張 | 直接取代，無對話框 |
| T2 | 有圖＋拖 1 張 | Dialog 三選；Replace 走未存檔警告→取代；Compare 開 V2（現圖 vs 新圖）；Cancel 無任何變化 |
| T3 | 有圖＋拖 2–4 張 | 直接 V1（現圖＋全部），**無對話框**；同路徑去重 |
| T4 | 有圖＋拖 >4 張 | 提示 beyond limit；取前 4＋現圖去重後進 V1 |
| T5 | 未存檔修改後 | Replace → 先見未存檔警告；Compare/Cancel → 不見 |
| T6 | Ctrl+drop | 與一般 drop 相同（手勢已移除，無特殊行為） |
| T7 | 回歸 | `ctest` 2/2、零警告；O 開檔／K 導覽／監控新檔流程不變 |

## 6. agy round-1 待確認（已判定，v1.1 採納）

| # | 議題 | 判定 |
|---|---|---|
| Q1 | Dialog modal 卡拖放訊息 | **無虞**（`DragFinish` 先行；既有 `MessageBoxA` 同模式） |
| Q2 | 移除 ConfirmDiscard 影響面 | 呼叫點共 3 處，皆純比較（§3.3）；MAIN_FIRST 保護由 `OpenImageFile` 承接 |
| Q3 | 2–4 張上限提示 | 沿用 beyond-limit 提示（實碼 `count<CMP_MAX_CELLS` 截斷＋訊息） |
| Q4 | 檔名過長 | `_snprintf` 截斷 |
| Q5 | dialog 縮圖 | v1 不做 |
| **新 Q6** | 單圖 Compare 開 V1 or V2 | **v1.1 核心更正：走 `CompareV2_OpenMode`（比照 `PROMPT_ACTION_COMPARE_NOW`）**——§3.1/§5 T2 已同步 |
| **新 Q7** | Replace 雙重詢問 | **v1.1 修正：直接呼叫 `OpenImageFile`**（內含 ConfirmDiscard），不再前置呼叫 |
