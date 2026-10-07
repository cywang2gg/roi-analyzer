# 全套 DPI 縮放（版面常數 DPI 化）架構書 v1.2

> 專案：`C:\Github\roi-analyzer`（純 C ＋ Win32 ＋ GDI ＋ WIC，ANSI 全 A 版，`-Wall -Wextra` 零警告，C11，宣告置頂，禁 `//` 註解）
> 基準：`master`（`ui_font_dpi` 已併，`s_ui_font`＋`dpiAware` 已生效，未 commit）
> 問題：使用者回報「UI 變清楚了，但版面不 fit」——字型已隨 DPI 放大，版面常數仍為 96dpi 邏輯像素
> 日期：2026-10-05
> 狀態：**v1.2（併入 agy round-2 review，GO）→ 發包 gh**

## 變更歷史

| 版本 | 日期 | 內容 |
|---|---|---|
| v1.0 | 2026-10-05 | 初稿（`Ui_Scale` 模組＋5 檔常數 DPI 化） |
| v1.1 | 2026-10-05 | 併入 agy review round 1（7 項）：**C1** `histpanel.c` WM_PAINT 的 `TextOutA/W` 座標（`8,32`／`56,32`）漏縮放→標籤疊在 `Source:` 上（CRITICAL）；**C2** `compare_v1.c` `v1_render` 狀態文字 `text_rect.left += 5`／`right -= 25` 漏縮放→撞關閉鈕（CRITICAL）；**C3** `compare_v2.c` `pan_right` 用 `gx1 + 82` 複合偏移漏縮放→疊在 `pan_left` 上（CRITICAL）；**C4** `compare_v2.c` `v2_render` A:/B: 標籤 `{8,8,w/2,32}` 漏縮放→文字垂直裁切；**C5** `main.c` 內嵌於比例式的固定偏移 `width - 320`／`available - 100` 漏縮放；**C6** `histpanel.c` combo 閾值 `160`／fallback `60`／`line_height` fallback `16` 漏縮放；**C7** `V1_TOOLBAR_H` 實際用於 `WM_SIZE`（1184/1188）與 `WM_CREATE`（1154）非 `v1_layout`，且**主視窗與 V1 均無 `WM_GETMINMAXINFO`**→須新增。另 C8：`v2_render` 分割線寬 `2` 漏縮放 |
| v1.2 | 2026-10-05 | 併入 agy review round 2（GO）：**C9** `main.c:1065` clamp 賦值 `hist_width = HISTPANEL_MIN_WIDTH;` 亦須 `Ui_Scale`（只改比較式會漏）；**C10** `main.c:1082` `table_height = 140;` 賦值處須 `Ui_Scale(140)`；**C11** `histpanel.c:367` `paint_stats` 的 `line_height` fallback `16` 亦須 `Ui_Scale(16)`（與 line 77 對稱）；**C12** `compare_v2.c:803-806` 群組框 y `2` → `Ui_Scale(2)`。另確認：無雙重縮放風險、比例式運算全部安全、`WM_GETMINMAXINFO` 置於 `V1WndProc`（非 `V1GridProc`） |

---

## 0. Root Cause（實測）

前次 `ui_font_dpi` 已讓 manifest 宣告 `dpiAware`（System DPI aware）＋字型依 DPI 建立。
**實測**：本機系統 DPI = `0xd8` = **216 = 225% 縮放**（`HKCU\Control Panel\Desktop\WindowMetrics\AppliedDPI`）。

| # | 現象 | 真因 |
|---|---|---|
| RC1 | 字型清楚但過大、塞不進控制項 | 字型依 DPI 放大（9pt→約 27px），但**所有版面常數是 96dpi 邏輯像素寫死**（histogram 300px、按鈕 28px、狀態列各區 220/200/90/260px、表格欄寬 38/150/…、比較視窗 40/76/64/28px） |
| RC2 | 狀態列文字截斷、訊息擠一起 | `App_StatusLayout` 各區寬度寫死，未隨字寬放大 |
| RC3 | 表格表頭 `Co...`/`R s...` 截斷、不用滿寬 | `table.c` `g_widths[]` 寫死 |
| RC4 | Histogram 面板標題/統計行距錯位 | `update_layout` 邊距寫死（8/36/52/…） |

**核心矛盾**：DPI-aware 下座標是物理像素，但版面按 96dpi 邏輯像素設計。**正解**＝所有版面常數乘 `dpi/96`。

## 1. 設計

### 1.1 縮放來源與 helper（新檔 `src/ui_scale.h`）

```c
/* File: src/ui_scale.h */
#ifndef ROI_UI_SCALE_H
#define ROI_UI_SCALE_H

int Ui_Dpi(void);          /* 目前系統 DPI（96 為基準） */
int Ui_Scale(int px);      /* px * dpi / 96，四捨五入（MulDiv） */

#endif
```

```c
/* File: src/main.c —— 檔案層級（與 s_ui_font 同區） */
int g_ui_dpi = 96;

int Ui_Dpi(void)
{
    return g_ui_dpi;
}

int Ui_Scale(int px)
{
    return MulDiv(px, g_ui_dpi, 96);
}
```

- `g_ui_dpi` 於 `WinMain` 最早處（`create_ui_font` 之前）由 `GetDeviceCaps(GetDC(NULL), LOGPIXELSY)` 設定一次；`create_ui_font` 改讀 `g_ui_dpi`（不重複取）。
- `Ui_Scale(x)` 在 `g_ui_dpi==96` 時**恆等回 x**（100% 縮放零變化，回歸安全）。
- `MulDiv` 為 Win32 API（四捨五入），已可用；`ui_scale.h` 不需 include（宣告用 int）。

### 1.2 使用慣例

各檔在檔頭加 `#include "ui_scale.h"`，版面字面值一律包 `Ui_Scale(...)`；**比例式運算（`width/2`、`usable*2/5`）不動，只縮放固定邊距與固定尺寸**。

## 2. 逐檔修改清單（精確錨點）

### 2.1 `src/main.c`

**`WinMain`（`create_ui_font` 之前）**：

```c
{
    HDC screen = GetDC(NULL);
    g_ui_dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    if (screen)
        ReleaseDC(NULL, screen);
    if (g_ui_dpi <= 0)
        g_ui_dpi = 96;
}
```

`create_ui_font()` 內 `dpi = GetDeviceCaps(...)` 改為 `dpi = g_ui_dpi;`（移除自身 GetDC/ReleaseDC）。

**`Layout()`（約 1045–1105）**——下列字面值包 `Ui_Scale`：

| 現值 | 改為 |
|---|---|
| `button_height = 28` | `Ui_Scale(28)` |
| `tabs_height = 28` | `Ui_Scale(28)` |
| `width >= 520` | `width >= Ui_Scale(520)` |
| `hist_width = HISTPANEL_DEF_WIDTH` | `Ui_Scale(HISTPANEL_DEF_WIDTH)` |
| `width - hist_width < 320` | `< Ui_Scale(320)` |
| `hist_width < HISTPANEL_MIN_WIDTH` | `< Ui_Scale(HISTPANEL_MIN_WIDTH)` |
| `hist_width = HISTPANEL_MIN_WIDTH;`（clamp 賦值，C9） | `Ui_Scale(HISTPANEL_MIN_WIDTH)` |
| `width - hist_width < 200` | `< Ui_Scale(200)` |
| `table_height < 140` | `< Ui_Scale(140)` |
| `table_height = 140;`（clamp 賦值，C10） | `Ui_Scale(140)` |
| `available - table_height < 100` | `< Ui_Scale(100)` |
| 按鈕 `SetWindowPos(..., 8, canvas_height+2, 78, 24, ...)` | `Ui_Scale(8), canvas_height+Ui_Scale(2), Ui_Scale(78), Ui_Scale(24)` |
| `(..., 92, ..., 70, 24, ...)` | `Ui_Scale(92), Ui_Scale(70), Ui_Scale(24)` |
| `(..., 172, ..., 100, 24, ...)` | `Ui_Scale(172), Ui_Scale(100), Ui_Scale(24)` |

（`table_height = (int)((double)client.bottom * 0.30)` 為比例，不動。）

**`App_StatusLayout()`（約 626）**：`left=220, mode=200, index=90, time=260` 與 `minimum_left=80, minimum_mode=90, minimum_index=45, minimum_time=120` 全部包 `Ui_Scale`。比例式退路（`width/4` 等）不動。

**`WM_CREATE` 控制項建立（Export/Clear/Multi）**：以 `0,0,0,0` 建立，實際尺寸在 `Layout()` 設定（已列入上表）。

**`WM_GETMINMAXINFO`（新增，主視窗目前無此 handler——C7）**：

```c
case WM_GETMINMAXINFO: {
    MINMAXINFO *limits = (MINMAXINFO *)lparam;
    limits->ptMinTrackSize.x = Ui_Scale(640);
    limits->ptMinTrackSize.y = Ui_Scale(480);
    return 0;
}
```

（225% 下狀態列最小需 ~754px、Export/Clear/Multi 需 ~612px、垂直面板 ~716px；未加會使視窗縮到內容崩壞。）

**內嵌比例式的固定偏移（C5）**：`hist_width = width - Ui_Scale(320)`（約 1063）、`table_height = available - Ui_Scale(100)`（約 1084）。

### 2.2 `src/histpanel.c`

`update_layout()`（約 71–97）：邊距/尺寸包 `Ui_Scale`：

| 現值 | 改為 |
|---|---|
| combo `(8, 3, width-100, 220)` | `(Ui_Scale(8), Ui_Scale(3), width-Ui_Scale(100), Ui_Scale(220))` |
| log `(width-82, 4, 74, 22)` | `(width-Ui_Scale(82), Ui_Scale(4), Ui_Scale(74), Ui_Scale(22))` |
| `graph.left = 8` / `graph.right = width-8` | `Ui_Scale(8)` |
| `graph.top = 52` | `Ui_Scale(52)` |
| `graph_bottom = height - (line_height*6 + 36)` | `height - (line_height*6 + Ui_Scale(36))` |
| ramp/stats 邊距 `8`、ramp 高 `10` | `Ui_Scale(8)`、`Ui_Scale(10)` |
| `graph_bottom < graph.top + 20` | `+ Ui_Scale(20)` |
| stats.bottom `height - 4` | `height - Ui_Scale(4)` |

`font_height` 已由 `GetTextMetricsA` 隨 DPI（不動）；`paint_stats` 行距用 `font_height`（不動）。

**C1（CRITICAL）WM_PAINT 文字座標（約 665–671）**：

| 現值 | 改為 |
|---|---|
| `TextOutA(back_dc, 8, 32, source_prefix, ...)` | `Ui_Scale(8), Ui_Scale(32)` |
| `TextOutW(back_dc, 56, 32, panel->label, ...)` | `x = Ui_Scale(8) + GetTextExtentPoint32A(prefix) + Ui_Scale(6)`（動態量測），`y = Ui_Scale(32)` |
| `TextOutA(back_dc, 56, 32, "No image", 8)` | 同上動態 x，`y = Ui_Scale(32)` |

（`56` 是 `8 + "Source: " 寬 + 6` 的複合值；225% 下 `Source: ` 約 96px，寫死 56 會使標籤疊在 `Source:` 上。）

**C6 其他漏縮放（約 77/81）**：

| 現值 | 改為 |
|---|---|
| `int line_height = panel->font_height ? panel->font_height : 16;` | fallback `Ui_Scale(16)`（**line 77 與 line 367 兩處**，C6＋C11） |
| `width > 160 ? width - 100 : 60` | `width > Ui_Scale(160) ? width - Ui_Scale(100) : Ui_Scale(60)` |

### 2.3 `src/table.c`

`g_widths[]`（14 欄）改為執行期計算：`Table_Create` 內 `col.cx = Ui_Scale(g_widths[i]);`（`g_widths` 保留為 96dpi 基準值）。表頭文字與欄寬同步放大 → 截斷解除。

### 2.4 `src/compare_v1.c`

- `#define V1_TOOLBAR_H 40` → **實際用於 `WM_SIZE`（1184/1188）與 `WM_CREATE`（1154）**（C7：非 `v1_layout`）；兩處均改 `Ui_Scale(V1_TOOLBAR_H)`。
- 工具列按鈕建立座標（`7,7,67,24`／`77,7,45,24`／`126,7,82,24`／`212,7,62,24`／`278,7,70,24`／`352,7,66,24`／`424,7,94,24`／`522,7,86,24`／`614,9,440,22`）全部包 `Ui_Scale`。
- `v1_layout`：`gap`（維持 1px）、`band = Ui_Scale(23)`、`close_rect` 的 `5`/`20` → `Ui_Scale`。
- **C2（CRITICAL）`v1_render` 狀態文字（457–458）**：`text_rect.left += Ui_Scale(5); text_rect.right -= Ui_Scale(25);`（`25` 是 `close_rect` 寬 `Ui_Scale(20)`＋邊距；未縮放會撞關閉鈕）。
- **C7 `WM_GETMINMAXINFO`（新增，V1 目前無此 handler）**：

```c
case WM_GETMINMAXINFO: {
    MINMAXINFO *limits = (MINMAXINFO *)lparam;
    limits->ptMinTrackSize.x = Ui_Scale(720);
    limits->ptMinTrackSize.y = Ui_Scale(400);
    return 0;
}
```

### 2.5 `src/compare_v2.c`

- `V2_TOP_H 76`／`V2_GROUP_H 64`／`V2_BOTTOM_H 28` → 執行期 `Ui_Scale`。
- `v2_layout`：`gz=4`、所有 `+4`、`+8`、`+6`、`+12`、`+18`、`20`、`42`、`44`、`45`、`32`、`24`、`22`、`18`、`130`、`70`、`78`、`track_w - 45 - 10`、`< 80` 全部包 `Ui_Scale`；**C3（CRITICAL）`pan_right` 的 `gx1 + 82` → `gx1 + Ui_Scale(82)`**（`82` 是 `8+70+4` 複合值，未縮放會使 `pan_right` 疊在 `pan_left` 上 76px）；**C12 群組框 y `2` → `Ui_Scale(2)`**（803–806）；比例式（`usable*2/5` 等）不動。
- **C4 `v2_render` A:/B: 標籤（約 620–645）**：非 Snapshot 分支 `label.left = Ui_Scale(8); label.top = Ui_Scale(8); label.bottom = Ui_Scale(32);`（`right` 用 `client.right/2` 或 `client.right - Ui_Scale(8)`）；Snapshot 分支的 logical `{8,8,w/2,32}` 亦改 `Ui_Scale`。
- **C8 `v2_render` 分割線寬（約 567）**：非 Snapshot `line_width` 由 `2` 改 `Ui_Scale(2)`（Snapshot 分支已用 `Snap_Round(scale)`，不動）。
- `WM_GETMINMAXINFO`（`compare_v2.c:1152`）：`ptMinTrackSize.x = Ui_Scale(900); .y = Ui_Scale(420);`。

### 2.6 不動項

- `compare_core.c` `Cmp_Blit`／`CmpView_*`（影像座標系，與 UI DPI 無關）。
- `compare_snap.c` `Snap_PhysicalScale`（System aware 下回 1.0；`CmpSnap_CreateScaledFont` 的 `dpi` 亦為 96 → 直通）。
- `histogram.c`（純計算）。
- 字型（已 DPI 化）。

## 3. 實作分段（各自獨立 commit）

| 階段 | 內容 | 檔案 | 驗收 |
|---|---|---|---|
| **D1 基礎** | `ui_scale.h`＋`main.c` 的 `g_ui_dpi`／`Ui_Dpi`／`Ui_Scale`＋`WinMain` 設定＋`create_ui_font` 改讀 | `ui_scale.h`(新)、`main.c` | 100% 下 `Ui_Scale(x)==x`；150% 下回 1.5x；建置零警告 |
| **D2 主視窗** | `Layout()`＋`App_StatusLayout()`＋Export/Clear/Multi 建立尺寸 | `main.c` | 狀態列 5 區不截斷、訊息不擠；按鈕/分頁/表格高度足 |
| **D3 Histogram＋Table** | `histpanel.c` `update_layout`＋`table.c` 欄寬 | `histpanel.c`、`table.c` | 面板標題/統計不重疊；表頭不截斷、用滿寬 |
| **D4 比較視窗** | `compare_v1.c`＋`compare_v2.c` 全部版面常數＋**新增 V1 `WM_GETMINMAXINFO`**（C7）＋v1_render 狀態文字邊距（C2）＋v2 pan_right 偏移（C3）＋v2 標籤座標（C4）＋分割線寬（C8） | `compare_v1.c`、`compare_v2.c` | V1 工具列/格線/狀態文字不重疊；V2 四群組/滑桿/標籤不重疊；min 尺寸隨 DPI |

D1 先行（D2–D4 依賴它）；D2/D3/D4 可各自 commit。

## 4. 風險與邊界

| 項目 | 處理 |
|---|---|
| 100% 縮放回歸 | `Ui_Scale` 恆等；所有版面與改版前一致 |
| 多螢幕跨 DPI 拖曳 | **不在範圍**（System DPI aware 固定啟動螢幕 DPI）；需跨 DPI 才升 Per-Monitor V2（另案） |
| `g_ui_dpi` 未初始化 | `WinMain` 最早設定；`ui_scale` 預設 96；`<=0` 防護回 96 |
| 既有 `Snap_Round`／`MulDiv` | 不衝突（`Snap_*` 屬 Snapshot 路徑） |
| 表格欄寬總和 > 視窗 | 225% 下總和約 2324px < 視窗物理寬（約 2800px），可容納；若未來超寬，ListView 水平捲軸自動出現 |
| `table.c` 的 `snprintf` | 既有檔已有（非本次新增）；新程式碼仍用 `_snprintf` |

## 5. 驗收清單

| # | 項目 | 預期 |
|---|---|---|
| T1 | 100% 縮放 | 版面與改版前一致（`Ui_Scale` 恆等） |
| T2 | 225%（本機） | 狀態列 5 區文字完整、訊息不擠；按鈕/分頁/表格高度足 |
| T3 | Histogram 面板 | 標題、統計各行不重疊；`StdDev` 數值完整 |
| T4 | ROI 表格 | 表頭 14 欄不截斷、用滿寬 |
| T5 | 比較視窗 V1 | 工具列 9 鈕不重疊；格線/狀態帶正常 |
| T6 | 比較視窗 V2 | 四群組不重疊；滑桿/按鈕/標籤完整；min 尺寸隨 DPI |
| T7 | 建置 | 零警告；`ctest` 2/2 |
| T8 | 中文/英文 UI | 無豆腐、無亂碼 |

## 6. 發包清單（gh）

**Commit D1**：`src/ui_scale.h`（新）＋`main.c`（`g_ui_dpi`/`Ui_Dpi`/`Ui_Scale`/`WinMain` 設定/`create_ui_font` 改讀）。
**Commit D2**：`main.c`（`Layout()`＋`App_StatusLayout()`＋按鈕建立尺寸）。
**Commit D3**：`histpanel.c`（`update_layout`）＋`table.c`（`col.cx = Ui_Scale`）。
**Commit D4**：`compare_v1.c`＋`compare_v2.c`（全部版面常數＋MINMAXINFO）。

gh：四 commit 分開、只實作不 push 不驗證；Hermes 本地複驗＋使用者手測。

## 7. 待確認（agy review 重點）

| # | 議題 | 本書立場 |
|---|---|---|
| Q1 | `Ui_Scale` 放 `main.c` 定義、`ui_scale.h` 宣告 | 最小改動；避免 include 循環（不拉 `app.h` 進 `table.c`/`histpanel.c`） |
| Q2 | 比例式運算不縮放 | 只縮放固定邊距/尺寸，比例自然適應 |
| Q3 | 表格欄寬 96dpi 基準值保留 | `g_widths` 不動，執行期 `Ui_Scale` |
| Q4 | 是否處理跨 DPI 螢幕 | 不做（System aware 限制），另案 |
| Q5 | `Ui_Scale(1)` gap 是否需縮放 | 1px 格線維持 1px（視覺更細）；如需可改 `Ui_Scale(1)`，預設維持 1 |

## 8. agy review round 1 處置（7 項＋C8）

| # | 判定 | 處置 |
|---|---|---|
| 1 RC/216dpi | PASS | 保留 |
| 2 Ui_Scale/MulDiv | PASS | 保留 |
| 3 常數清單完整度 | FAIL | **採納**：C1–C6 全補（§2 各節） |
| 4 ui_scale.h 位置 | PASS(with note) | `main.c` 須 `#include "ui_scale.h"` 防 `-Wmissing-prototypes` |
| 5 表格欄寬 | PASS | 維持固定 DPI 縮放，不比例化（`LVS_REPORT` 自動水平捲軸） |
| 6 比例 vs 固定分離 | PARTIAL | **採納**：C5 內嵌偏移補縮放 |
| 7 時機／MINMAXINFO | FAIL | **採納**：C7 主視窗＋V1 新增 `WM_GETMINMAXINFO`（`g_ui_dpi` 時機已 PASS） |
| C8 v2 分割線寬 | 新增 | **採納**：非 Snapshot `Ui_Scale(2)` |
| C9–C12 clamp 賦值/fallback | 新增（R2） | **採納**：§2.1（1065/1082）、§2.2（line 367）、§2.5（群組 y 2） |
| 雙重縮放／比例式風險 | PASS | 無；比例式全安全 |

## 9. agy review round 2（v1.2）最終判定

**GO**。C1–C8 複驗全 PASS（C1 動態量測正確、C2 `25=20+5` 正確、C3 `82=8+70+4` 正確、C7 `WM_GETMINMAXINFO` 置於 `V1WndProc`）；新增 C9–C12 已併入。無雙重縮放風險。
