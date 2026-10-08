# Histogram 分割條拖曳（canvas ↔ 面板寬度可調）架構書 v1.1

> 專案：`C:\Github\roi-analyzer`（純 C ＋ Win32 ＋ GDI，ANSI 全 A 版，`-Wall -Wextra` 零警告，C11，宣告置頂，禁 `//` 註解）
> 基準：`master`（`28c4c0c`，v3.5 UI 高清化已合併）
> 需求：主視窗影像畫布與 Histogram 面板之間的直立分隔線可左右拖曳改變兩者寬度；面板內元件跟隨新寬度
> 日期：2026-10-07
> 狀態：**v1.1 定稿（agy round 1 NO-GO 五項 Must-Fix 全併入、round 2 GO）→ 發包 gh**

## 變更歷史

| 版本 | 日期 | 內容 |
|---|---|---|
| v1.0 | 2026-10-07 | 初稿 |
| v1.1 | 2026-10-07 | agy round 1 **NO-GO → 5 項 Must-Fix 全採納**：MF1 splitter `LBUTTONDOWN` 判定須在 `!img.valid` guard **之前**（否則空圖無法拖）；MF2 擊中條件補 `g_app.hwnd_hist && IsWindowVisible(...)`（否則面板隱藏時窗右緣 6px 成幽靈條）；MF3 `Layout()` 取 `g_app.hist_width` 後**無條件下限 clamp** `>= Ui_Scale(HISTPANEL_MIN_WIDTH)`（既有 clamp 只在 canvas<320 分支內，寬窗＋異常 ini 值會破版）；MF4 游標只在 `WM_SETCURSOR` 設定、`WM_MOUSEMOVE` **僅拖曳中** early-return（一般 hover 不得攔，否則狀態列 `App_UpdateStatus` 凍結——canvas.c:296）；MF5 行號更正 `histpanel.c:781-786`（原誤 739-744）。位置裁決：維持 **Canvas Edge（方案 A）**（Minimal Code Footprint；方案 B 獨立 HWND 為備選） |

---

## 0. 需求解讀

| 項目 | 採用 |
|---|---|
| 拖曳分隔線 | canvas 右緣 6px 狀態帶＝splitter；hover 出 `IDC_SIZEWE`；拖曳即時更新兩側寬度 |
| 內件「等比例拉伸」 | **面板 reflow 即可**（見 §1）；**影像畫布不拉伸**（維持 zoom，重新 letterbox）；**表格欄不做**（Win32 ListView 表頭天生可拖） |
| 記憶 | 寬度存 `roi_analyzer.ini`（96dpi 邏輯值），下次還原；`show_hist` 關閉時 splitter 無效 |

## 1. 現況與設計

### 1.1 實碼錨點

| # | 事實 | 證據 |
|---|---|---|
| F1 | `Layout()` 每次從 `HISTPANEL_DEF_WIDTH`(300) 重算 `hist_width`，無記憶；約束：canvas ≥ `Ui_Scale(320)`→hist ≥ `Ui_Scale(220)`；`width-hist < Ui_Scale(200)` 時隱藏面板 | `main.c:1070-1080` |
| F2 | canvas 與 hist 為**相鄰兄弟子視窗**，中間無分割條控項；無 `WM_SETCURSOR`（main 與 canvas 皆無） | `main.c:1098-1103`、grep 0 |
| F3 | canvas WndProc 已有 `WM_LBUTTONDOWN`＋`SetCapture`（ROI 拖曳）與 `WM_MOUSEMOVE`（游標 ROI 顯示） | `canvas.c:239,249,261,324` |
| F4 | `settings.c` 慣例：`Get/WritePrivateProfile[A]`＋分節（`Monitor`/`Rename`/`locate`）；`settings.h` 每項一對 Load/Save | `settings.c:37-131` |
| F5 | `g_app.show_hist` 開關（View 選單切換，`main.c:2424`），初值 TRUE（2662） | — |
| F6 | 面板 `update_layout` 全部依 client 寬度 reflow（`graph.right = width-8`、combo/log 相對定位） | `histpanel.c:80-104` |
| F6b | `HistPanel_SetSource` 快取命中即跳過重算（`img_gen/whole/src` 相同）——拖曳中 `App_UpdateHistogram` 成本低 | `histpanel.c:781-786`（MF5 更正，原誤 739-744） |
| F7 | canvas `WM_LBUTTONDOWN` 開頭即 `if (!g_app.img.valid) return 0;`（MF1：splitter 判定必須置於其**之前**）；滑鼠移動即呼叫 `App_UpdateStatus`（canvas.c:296，MF4：一般 hover 不得 early-return） | `canvas.c:242,296` |

### 1.2 「元件跟著拉伸」的落實

| 元件 | 做法 |
|---|---|
| Histogram 內容 | **零改動**——`update_layout` 已用 client 寬度算（F6），hist 寬度一變即 reflow；combo `width-100`／log `width-82`／graph/ramp/stats 全自動 |
| 影像畫布 | **不拉伸**：拖曳只改兩子視窗寬度，`view.zoom` 不動 → letterbox 自然調整；雙擊 fit 為既有行為 |
| ROI 表格 | **不動**：`LVS_REPORT` 表頭分隔線原生可拖（`table.c` 零改動） |

## 2. 設計

### 2.1 交互（canvas WndProc 為入口，F3 現成 capture 慣例）

** splitter 擊中判定（MF2，三條件同時成立）**：

```c
static BOOL v1_splitter_hit(HWND hwnd, POINT pt)
{
    RECT rc;
    if (!g_app.hwnd_hist || !IsWindowVisible(g_app.hwnd_hist))
        return FALSE;                       /* MF2：面板隱藏 → 無幽靈條 */
    if (!GetClientRect(hwnd, &rc))
        return FALSE;
    return pt.x >= rc.right - Ui_Scale(6);
}
```

| 訊息 | 處理 |
|---|---|
| `WM_SETCURSOR` | **游標唯一設定點（MF4）**：拖曳中或 `v1_splitter_hit` 命中（`LOWORD(lparam)==HTCLIENT`）→ `SetCursor(LoadCursor(NULL, IDC_SIZEWE))`、`return 1`；否則落原邏輯 |
| `WM_LBUTTONDOWN` | **判定須在 `if (!g_app.img.valid) return 0;` 之前（MF1）**：命中 → 進入拖曳（`splitting=TRUE`、`SetCapture`、記起點 x 與當前 `hist_width`）、early-return 不進 ROI 拖曳；否則原 ROI 邏輯（空圖也照舊 return） |
| `WM_MOUSEMOVE`（**僅拖曳中** early-return，MF4） | `delta = pt.x - start_x`；`new_hist = start_hist - delta`；clamp `[Ui_Scale(HISTPANEL_MIN_WIDTH), width_client - Ui_Scale(320)]`；寫 `g_app.hist_width` → `Layout()`（SetWindowPos 即時跟手；hist 快取命中僅重繪 F6b，免節流）；**拖曳中 return**（不跑 ROI/狀態列邏輯） |
| `WM_MOUSEMOVE`（**非拖曳，含 splitter hover**） | **一律走原邏輯，不得 early-return（MF4）**——`App_UpdateStatus`（canvas.c:296）持續更新游標座標/RGB；游標切換全權交 `WM_SETCURSOR`（MF4：在 MOUSEMOVE 設 `SetCursor` 會被 DefWindowProc 立即覆寫） |
| `WM_LBUTTONUP`／`WM_CAPTURECHANGED` | 結束拖曳、`ReleaseCapture`、`Settings_SaveHistWidth()`（存 96dpi 邏輯值） |

**Canvas 內既有 ROI 滑鼠邏輯**：除 splitter 分支外零改變；拖曳中才 early-return（MF4）。

### 2.2 `Layout()` 變更（`main.c`）

`hist_width` 計算改為優先取 `g_app.hist_width`（持久化偏好，0=未設定）：

```c
if (show_panel) {
    if (g_app.hist_width > 0)
        hist_width = g_app.hist_width;              /* 拖曳記憶值（已含 Ui_Scale） */
    else
        hist_width = Ui_Scale(HISTPANEL_DEF_WIDTH); /* 預設 */
    /* MF3：無條件下限 clamp（既有 clamp 只在 canvas<320 分支內；
       寬窗＋異常 ini 值會直通破版） */
    if (hist_width < Ui_Scale(HISTPANEL_MIN_WIDTH))
        hist_width = Ui_Scale(HISTPANEL_MIN_WIDTH);
    /* 其餘既有 clamp 鏈不變：canvas<320 → hist>=220；整窗過窄 → 隱藏面板 */
}
```

- `g_app.hist_width` 存**物理 px**（拖曳時當場給值）；啟動時由 ini 的**邏輯值**經 `Ui_Scale` 換算（見 §2.3）。
- 拖曳期間 `show_hist` 必為 TRUE（splitter 僅存在於面板顯示時）。
- `App_UpdateHistogram()` 每次 Layout 都會被呼叫（現況即如此）；拖曳時統計重算成本可接受（`Hist_Compute` 依 `img_gen/src` 快取跳過，`histpanel.c:781-786` 快取命中僅重繪）。

### 2.3 持久化（`settings.h/.c`，F4 慣例）

```c
/* settings.h */
BOOL Settings_LoadHistWidth(int *logical_width);   /* 0=未設定 */
BOOL Settings_SaveHistWidth(int logical_width);
/* settings.c：分節 [Layout] key HistWidth（96dpi 邏輯 px；0 或缺→未設定） */
```

- **載入**（`WinMain` 最早，`g_ui_dpi` 設定後）：`g_app.hist_width = logical>0 ? Ui_Scale(logical) : 0;`
- **儲存**（splitter 拖曳結束）：`logical = MulDiv(g_app.hist_width, 96, g_ui_dpi)`（與 `Ui_Scale` 互逆，96dpi 下恆等）。
- `App.h` `app_t` 加欄位 `int hist_width;`（0=用預設）。

### 2.4 邊界與失敗

| 情境 | 行為 |
|---|---|
| 拖到最小/最大 | clamp 後停住（`Ui_Scale(220)`／`width-Ui_Scale(320)`） |
| 窗寬不足（`show_panel` FALSE） | 面板隱藏，splitter 不存在；拖曳狀態若進行中 → 由 `WM_CAPTURECHANGED` 結束 |
| `show_hist` 切換 OFF | 面板隱藏；`g_app.hist_width` 保留（再開啟時還原） |
| ini 讀失敗 | `hist_width=0` → 預設 300 |
| ini 寫失敗 | 忽略（與既有 Settings 慣例一致，僅回傳 FALSE） |
| 縮放 DPI 變更（重啟） | 邏輯值 × 新 DPI → 物理值，維持視覺比例 |

## 3. 實作分段（一 commit）

| 階段 | 檔案 | 內容 |
|---|---|---|
| **S1** | `settings.h/.c`、`app.h`、`main.c`、`canvas.c` | §2.1–§2.3 全部；無分階段必要（同問題） |

**檔案範圍**：`src/settings.h`、`src/settings.c`、`src/app.h`、`src/main.c`、`src/canvas.c`（僅此 5 檔）。

**agy round-2 實作注意（5 項，gh 必須照做）**：
1. `canvas.c` 補 `#include "ui_scale.h"`（否則 `-Wimplicit-function-declaration`）。
2. `canvas.c` 用匯出函式 `Ui_Dpi()`（`g_ui_dpi` 是 `main.c` file-static，未匯出）。
3. `Layout()` 為 static：`app.h` 加 `void App_Layout(void);`，`main.c` 實作轉呼 `Layout();`，`canvas.c` 拖曳中呼叫 `App_Layout()`。
4. `WM_SETCURSOR` 的 `lparam` 是 hit-test 碼非座標：`GetCursorPos`＋`ScreenToClient` 取座標再餵 `v1_splitter_hit`。
5. 上限 clamp 的 `width_client` 要取**主視窗**寬（`GetClientRect(g_app.hwnd_main)`），canvas 自身寬僅為下限側。

## 4. 驗收清單

| # | 項目 | 預期 |
|---|---|---|
| T1 | hover 分隔帶 | 游標變 `IDC_SIZEWE`（左右 6px 內） |
| T2 | 拖曳 | 兩側即時變寬；canvas 內容不變形（zoom 不動）；面板圖/統計 reflow 正常 |
| T3 | 邊界 | 拖到 220 或 canvas 320 停住；繼續拖不破版 |
| T4 | 釋放後重開視窗 | 寬度還原（ini）；換 DPI 重開仍為合理比例 |
| T5 | 拖曳時 ROI | 分割帶內 LBUTTONDOWN 不觸發 ROI 框；畫布中部拖曳 ROI 照常 |
| T6 | `show_hist` 開關 | 關→面板藏、無 splitter；開→還原記憶寬度 |
| T7 | 100% 回歸 | 無拖曳時與現況一致（預設 300）；`ctest` 2/2、零警告 |

## 5. 待確認（agy review 重點）

| # | 議題 | 本書立場 |
|---|---|---|
| Q1 | splitter 入口放 canvas WndProc 還是獨立 splitter HWND | canvas（F3 capture 慣例現成、少一個視窗、`Layout` 零新控項）；獨立 HWND 為替代 |
| Q2 | 拖曳中是否節流 `Layout()` | 不節流（SetWindowPos＋hist 快取命中成本低）；若實測掉幀再加 16ms timer |
| Q3 | 存邏輯值 vs 物理值 | 邏輯值（96dpi），跨 DPI 穩定 |
| Q4 | 雙擊 splitter 是否 fit | 不做（畫布雙擊已有 fit）；另立需求 |
| Q5 | 拖曳寬度是否進 View 選單（重設預設） | 不做；重設＝把 ini HistWidth 刪掉（或 v1.1 加選單項） |
