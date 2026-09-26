

---

# Histogram 模組 — 架構設計書

> 版本 v1.1 | 2026-09-26 | 獨立模組,整合目標:ROI Analyzer v2.3

變更歷史:

- v1.0 (2026-09-26):新模組。提供類似 Photoshop 的 Histogram 面板。
  - 資料來源:沒有選取 ROI 時顯示整張影像,有選取時顯示該 ROI
  - 通道:RGB 疊合 / Y / R / G / B,可切換
  - 顯示 Mean / Std Dev / Median / Pixels 統計;滑鼠停留或拖曳時顯示 Level / Count / Percentile
- v1.1 (2026-09-26):整合 ROI Analyzer v2.3;來源標籤改由呼叫端傳入;快取鍵不含 ROI index;補充 ROI 選取同步方式、按鍵焦點差異及 H1/H3 誤差標準。

## 1. 目標

- 在主視窗右側提供 Histogram 面板,可隨時顯示或隱藏
- **資料來源自動切換**:
  - `rois.selected == -1` 或影像上沒有 ROI → 整張影像
  - `rois.selected >= 0` → 該 ROI 的矩形區域
- **通道選擇**:

| 通道 | 說明 | 繪製顏色 |
|------|------|----------|
| RGB | R/G/B 三通道疊合,與 Photoshop「Colors」相同 | 加色混合(重疊處為黃 / 青 / 洋紅 / 灰) |
| Y | 亮度,BT.601,與 Grid 表格使用相同公式 | 灰 |
| R | Red | 紅 |
| G | Green | 綠 |
| B | Blue | 藍 |

- 支援線性 / 對數縱軸
- 滑鼠停留時顯示該 Level 的 Count 與 Percentile;拖曳時選取 Level 範圍並顯示範圍統計

非目標:不做 Levels / Curves 調整、不做即時視訊直方圖、不做多 ROI 聯集直方圖、不寫入 log。

## 2. 新增檔案

```
roi-analyzer/src/
├── histogram.h/.c   # 純計算:直方圖統計、範圍統計(不依賴 GUI,可單獨測試)
└── histpanel.h/.c   # 面板子視窗:通道選單、繪圖、滑鼠互動
```

約增加 450 行 C。模組依賴方向如下:

```
histpanel ──> histogram ──> image (image_t 定義)
main ───────> histpanel             (只透過公開 API)
```

`histogram.c` 不 include `app.h`。`histpanel.c` 也不直接讀 `g_app`,資料一律由 `main.c` 推送進來,方便拿到其他專案整合。

## 3. 核心資料結構

```c
// File: src/histogram.h
#pragma once
#include <windows.h>
#include "image.h"

enum { HIST_R = 0, HIST_G = 1, HIST_B = 2, HIST_Y = 3, HIST_NCH = 4 };

typedef struct {
    unsigned int bin[HIST_NCH][256];  // 各通道各 Level 的像素數
    unsigned int max_bin[HIST_NCH];   // 各通道最大 bin(縱軸正規化用)
    double mean[HIST_NCH];
    double std[HIST_NCH];             // 母體標準差
    int median[HIST_NCH];
    unsigned int count;               // 像素總數
    RECT src;                         // 來源矩形(影像座標 inclusive)
    BOOL whole;                       // TRUE = 整張影像
    unsigned int img_gen;             // 影像世代編號(快取判斷用)
    BOOL valid;
} histogram_t;

typedef struct {
    unsigned int count;               // 範圍內像素數
    double mean, std;
    double percentile;                // 累積到 hi 的百分比(0~100)
} hist_range_t;

void Hist_Compute(const image_t *img, const RECT *rc, unsigned int img_gen,
                  histogram_t *out);
void Hist_RangeStats(const histogram_t *h, int ch, int lo, int hi, hist_range_t *out);
void Hist_Reset(histogram_t *h);
```

```c
// File: src/histpanel.h
#pragma once
#include <windows.h>
#include "histogram.h"

typedef enum { HCH_RGB, HCH_Y, HCH_R, HCH_G, HCH_B, HCH_COUNT } hist_channel_t;

// 面板通知父視窗(WM_NOTIFY, code 使用 NMHDR.code)
#define HPN_CHANNELCHANGED  (WM_APP + 0x100)

typedef struct {
    NMHDR hdr;
    hist_channel_t channel;
} nm_histpanel_t;

BOOL HistPanel_Register(HINSTANCE hinst);
HWND HistPanel_Create(HWND parent, int ctrl_id);

void HistPanel_SetSource(HWND hp, const image_t *img, const RECT *rc,
                         const wchar_t *label, unsigned int img_gen); // rc == NULL → 整張
void HistPanel_ClearSource(HWND hp);                            // 無影像
void HistPanel_SetChannel(HWND hp, hist_channel_t ch);
hist_channel_t HistPanel_GetChannel(HWND hp);
void HistPanel_SetLogScale(HWND hp, BOOL on);

#define HISTPANEL_DEF_WIDTH  300
#define HISTPANEL_MIN_WIDTH  220
```

面板內部狀態(只在 `histpanel.c` 內部使用,透過 `SetWindowLongPtr(GWLP_USERDATA)` 綁定到視窗):

```c
typedef struct {
    HWND hwnd, hwnd_combo, hwnd_chk_log;
    hist_channel_t channel;
    BOOL log_scale;
    histogram_t hist;
    int hover_level;          // -1 = 無
    BOOL selecting;           // 拖曳選取 Level 範圍中
    int sel_lo, sel_hi;       // 選取範圍;sel_lo < 0 表示無
    RECT rc_graph;            // 圖表區(面板 client 座標)
    RECT rc_ramp;             // 下方漸層條
    RECT rc_stats;            // 統計文字區
    HFONT font;
} hist_panel_t;
```

## 4. 計算規格(`histogram.c`)

### 4.1 單次掃描

走訪 `rc` 範圍內每個像素(BGRA,`px[y*pitch + x*4 + 0..2]` = B, G, R):

```
bin[HIST_R][R]++; bin[HIST_G][G]++; bin[HIST_B][B]++;
Yi = (299*R + 587*G + 114*B + 500) / 1000;   // 整數四捨五入,範圍 0..255
bin[HIST_Y][Yi]++;
```

- Y 採用 BT.601 係數,與 `analyze.c` 相同,但 bin 需要整數 Level,因此先四捨五入
- 使用 `unsigned int` 計數。32-bit 可容納約 42 億像素,足夠使用

### 4.2 統計值(由 bin 推導,不再掃描像素)

\[
\mu = \frac{1}{N}\sum_{l=0}^{255} l \cdot \text{bin}[l], \qquad
\sigma = \sqrt{\frac{1}{N}\sum_{l=0}^{255} l^2 \cdot \text{bin}[l] - \mu^2}
\]

- 使用 double 累加;若 \( \sigma^2 < 0 \) 則 clamp 為 0
- Median:從 Level 0 開始累加,第一個使累積值 \( \ge \lceil N/2 \rceil \) 的 Level
- 與 Grid 表格的一致性:
  - R/G/B 的 mean/std 與表格**完全一致**,因為來源都是整數
  - Y 因為先四捨五入,與表格 Y mean 最多相差 0.5。這是預期行為,面板上 Y 的統計以直方圖為準

### 4.3 範圍統計 `Hist_RangeStats(h, ch, lo, hi)`

- 只使用 \( l \in [lo, hi] \) 的 bin 計算 count / mean / std
- percentile = \( \sum_{l=0}^{hi} \text{bin}[l] / N \times 100 \)
- 停留單一 Level 時呼叫 `lo == hi`

### 4.4 效能與快取

- 4000×3000 影像單次掃描約 12M px,量級為 30ms,在 UI 執行緒同步執行
- 快取條件:`img_gen`、`src`、`whole` 都相同時,`HistPanel_SetSource` 不重算,只重繪,但仍更新呼叫端提供的 label(ROI 刪除後來源編號可能遞補)
- 切換通道、切換對數縱軸、改變視窗大小:**只重繪,不重算**(4 個通道在同一次掃描已全部算好)

## 5. 面板版面與繪製(`histpanel.c`)

### 5.1 面板內部配置

```
┌──────────────────────────────┐
│ Channel: [RGB        ▼] ☐ Log │ 控制列 26px
│ Source: Drag #2 (10,10)-(59,59)│ 來源標籤 18px
├──────────────────────────────┤
│                              │
│         ▁▂▅█▇▅▃▂▁            │ 圖表區(填滿剩餘,最小 100px)
│                              │
├──────────────────────────────┤
│ ■■■■■■■■ 0 → 255 漸層 ■■■■■■■ │ 漸層條 10px
├──────────────────────────────┤
│ Mean:  128.40   Level:  200  │
│ StdDev: 32.11   Count:  1532 │ 統計區 6 行 × 16px
│ Median:  127    Pct:  87.20% │
│ Pixels: 307200               │
└──────────────────────────────┘
```

- 下拉選單 `CBS_DROPDOWNLIST` 的項目順序:`RGB` / `Luminosity (Y)` / `Red` / `Green` / `Blue`,對應 `hist_channel_t`
- 來源標籤依狀態顯示以下其中一種:
  - `Source: Entire Image (640x480)`
  - `Source: Drag #2 (10,10)-(59,59)`
  - `Source: 3x3 #5 (0,0)-(212,159)` / `Source: 5x5 #13 (...)`
  - `No image`
- RGB 疊合模式下,統計區改為 3 列(R / G / B 各顯示 Mean / StdDev / Median);停留時顯示該 Level 的 R/G/B 三個 Count

### 5.2 圖表繪製

- 使用 32bpp DIB Section 作為圖表緩衝,大小等於 `rc_graph`。逐像素寫入後 `BitBlt`,不會閃爍
- 橫軸:圖表寬 \( W \) 像素,第 \( x \) 欄對應 Level 範圍 \( [\lfloor x \cdot 256 / W \rfloor,\ \lfloor (x+1) \cdot 256 / W \rfloor) \)
  - 範圍包含多個 bin 時取最大值;\( W > 256 \) 時多個欄位共用同一個 bin
- 縱軸:圖表高 \( H \),bin 值 \( v \) 的柱高為

\[
h_{\text{lin}} = \frac{v}{v_{\max}} H, \qquad
h_{\log} = \frac{\ln(1+v)}{\ln(1+v_{\max})} H
\]

  - 單一通道使用該通道的 `max_bin`;RGB 疊合模式使用三個通道 `max_bin` 的最大值,讓三條曲線使用同一比例尺
- RGB 疊合採用遮罩查表:對每個像素 \( (x, y) \),若 R/G/B 柱高覆蓋該點,則分別設定 bit 1 / 2 / 4:

| mask | 顏色 | mask | 顏色 |
|------|------|------|------|
| 0 | 背景 `RGB(40,40,40)` | 4 | 藍 `RGB(70,110,240)` |
| 1 | 紅 `RGB(230,60,60)` | 5 | 洋紅 `RGB(220,80,220)` |
| 2 | 綠 `RGB(60,200,60)` | 6 | 青 `RGB(60,210,220)` |
| 3 | 黃 `RGB(230,210,60)` | 7 | 灰 `RGB(200,200,200)` |

- 單一通道:柱體使用該通道顏色(Y 為灰 `RGB(200,200,200)`)
- 疊加層:
  - 選取範圍 `[sel_lo, sel_hi]`:背景改為 `RGB(70,70,70)`
  - 停留的 Level:白色 1px 垂直線
- 漸層條:依通道畫 0→255 漸層(Y / RGB 為黑到白,R/G/B 為黑到該色)

### 5.3 滑鼠互動(僅限圖表區)

```
Move            → hover_level = clamp(x * 256 / W, 0, 255) → 更新統計區
LDown           → SetCapture, selecting=TRUE, sel_lo=sel_hi=hover_level
Move(selecting) → sel_hi = hover_level
LUp             → ReleaseCapture;顯示時正規化 lo<=hi
WM_MOUSELEAVE   → hover_level=-1(使用 TrackMouseEvent)
右鍵 / 來源改變  → 清除選取範圍(sel_lo=-1)
```

統計區的 Level / Count / Pct 欄位依以下優先順序顯示:

1. 有選取範圍:`Level: lo..hi`,顯示範圍 count 與 percentile
2. 有停留:顯示單一 Level
3. 都沒有:空白

## 6. 整合規格(對 ROI Analyzer v2.3 的修改)

### 6.1 `app.h` 新增欄位

```c
// app_t 內新增
HWND hwnd_hist;          // Histogram 面板
BOOL show_hist;          // 面板是否顯示,預設 TRUE
unsigned int img_gen;    // 每次 Image_Load 成功就 +1
```

### 6.2 單一同步函式(新增於 `main.c`)

所有來源變動都只呼叫這一個函式,避免多個呼叫點各自組裝參數:

```c
// File: src/main.c
static void App_UpdateHistogram(void)
{
    if (!g_app.show_hist || !g_app.hwnd_hist) {
        return;
    }
    if (!g_app.img.valid) {
        HistPanel_ClearSource(g_app.hwnd_hist);
        return;
    }
    int sel = g_app.rois.selected;
    if (sel >= 0 && sel < g_app.rois.count) {
        const roi_item_t *item = &g_app.rois.items[sel];
        roi_mode_t mode = item->source == ROI_SRC_GRID3 ? MODE_GRID3 :
                          (item->source == ROI_SRC_GRID5 ? MODE_GRID5 : MODE_DRAG);
        wchar_t label[160];
        swprintf(label, 160, L"%hs #%d (%d,%d)-(%d,%d)",
                 ROI_ModeLabel(mode), ROI_SourceIndex(&g_app.rois, sel) + 1,
                 item->res.x0, item->res.y0, item->res.x1, item->res.y1);
        HistPanel_SetSource(g_app.hwnd_hist, &g_app.img,
                            &item->rc, label, g_app.img_gen);
    } else {
        wchar_t label[160];
        swprintf(label, 160, L"Entire Image (%dx%d)", g_app.img.w, g_app.img.h);
        HistPanel_SetSource(g_app.hwnd_hist, &g_app.img, NULL, label, g_app.img_gen);
    }
}
```

### 6.3 呼叫點

| 事件(v2.3 既有流程) | 動作 |
|------------------------|------|
| `Image_Load` 成功 | `img_gen++` → `App_UpdateHistogram()`(此時 selected = -1,顯示整張) |
| 拖曳產生新 ROI(selected 設為新 ROI) | `App_UpdateHistogram()` |
| 畫布點擊命中 ROI / 表格選取列 | `App_UpdateHistogram()` |
| `ROI_BuildGrid`(selected = -1) | `App_UpdateHistogram()`(顯示整張) |
| Delete 選取 ROI / Clear All | selected = -1 → `App_UpdateHistogram()` |
| 開啟面板(`show_hist` 由 FALSE 變 TRUE) | `App_UpdateHistogram()`(隱藏期間不計算) |
| 視窗 resize | 只由 `Layout()` 移動面板,面板自行重繪,不重算 |

`ROI_SetSelected(idx)` 是 `roi.c/.h` 的唯一選取值入口,只負責設定 `rois.selected`。依賴方向不允許 `roi.c` include `app.h`;因此由 `main.c` 的 `App_SelectROI()` 在呼叫 `ROI_SetSelected()` 後負責同步表格、畫布、狀態列與直方圖。ROI 模組內的建立、刪除與點選流程也只透過 `ROI_SetSelected()` 改值,再由其 Win32 呼叫方呼叫 `App_RoiChanged()` / `App_SelectROI()` 完成同步。

### 6.4 取消選取(回到整張影像)

v2.0 沒有明確定義「取消選取」,本模組需要補上以下規則:

| 操作 | 結果 |
|------|------|
| DRAG 模式點擊畫布空白處(沒有命中任何 ROI) | selected = -1 |
| 點擊影像外的畫布區域(任何模式) | selected = -1 |
| 非拖曳狀態按 `Esc` | selected = -1(拖曳中按 `Esc` 仍為取消橡皮筋) |
| 點擊表格空白處(`LVN_ITEMCHANGED` 之後沒有任何選取列) | selected = -1 |

GRID 模式下,影像內每一點都會命中某一格,因此要回到整張影像只能用影像外點擊或 `Esc`。切換 Drag / 3x3 / 5x5 表格頁籤時,也清除 selected 並使 Histogram 回到整張影像。

### 6.5 `Layout()` 修改

原本的「畫布佔剩餘上方區域」改為左右分割:

```
上方區域寬 = client_w
if show_hist:
    if client_w < 520: 隱藏面板(不改變 show_hist,空間足夠時自動恢復)
    hist_w = HISTPANEL_DEF_WIDTH
    if client_w - hist_w < 320: hist_w = max(HISTPANEL_MIN_WIDTH, client_w - 320)
canvas: (0, 0, client_w - hist_w, upper_h)
hist:   (client_w - hist_w, 0, hist_w, upper_h)
```

面板高度與畫布相同(按鈕列上方)。按鈕列、表格、狀態列的配置不變。

```
┌──────────────────────────────────┬──────────────┐
│                                  │ Channel [▼]  │
│          Canvas                  │ Source: ...  │
│                                  │  Histogram   │
│                                  │  Stats       │
├──────────────────────────────────┴──────────────┤
│ [Export] [Clear] ☐ 複選                          │
├─────────────────────────────────────────────────┤
│ Grid 表格                                        │
├─────────────────────────────────────────────────┤
│ 狀態列                                           │
└─────────────────────────────────────────────────┘
```

### 6.6 選單與快速鍵

新增 **View** 選單:

- Histogram Panel [H](打勾項)
- ─
- Channel: RGB [A] / Luminosity [Y] / Red [R] / Green [G] / Blue [B](單選打勾,`CheckMenuRadioItem`)
- Log Scale [L](打勾項)

| 按鍵 | 功能 | 與 v2.0 是否衝突 |
|------|------|------------------|
| `H` | 顯示 / 隱藏面板 | 無 |
| `A` / `Y` / `R` / `G` / `B` | 切換通道 | 無(v2.0 已使用 1/2/3/M/C/O/Del/Esc/Ctrl+E) |
| `L` | 線性 / 對數 | 無 |

- 新按鍵 `H/A/Y/R/G/B/L` 使用 Accelerator Table(`TranslateAccelerator` 放在訊息迴圈中),因此焦點在表格或下拉選單時仍生效。既有按鍵仍走 canvas `WM_KEYDOWN`;焦點在表格/下拉選單時,只有新按鍵生效,不搬移舊按鍵。
- 選單 → 面板:呼叫 `HistPanel_SetChannel()`
- 面板下拉選單 → 選單:面板送出 `WM_NOTIFY`(`HPN_CHANNELCHANGED`),`main.c` 收到後更新選單打勾

### 6.7 初始化

```c
// WinMain 內,在建立主視窗之前
HistPanel_Register(hinst);

// 主視窗 WM_CREATE 內
g_app.hwnd_hist = HistPanel_Create(hwnd, IDC_HISTPANEL);
g_app.show_hist = TRUE;
```

`InitCommonControlsEx` 已包含 `ICC_STANDARD_CLASSES` 時,下拉選單與勾選框不需要額外初始化。

### 6.8 CMake

```cmake
# File: CMakeLists.txt(add_executable 內新增兩行)
    src/histogram.c
    src/histpanel.c
```

不需新增連結庫(DIB Section、下拉選單都屬於 `gdi32` / `user32`)。

## 7. 驗收項目(補入 `02_verification.md`)

| # | 情境 | 預期結果 |
|---|------|----------|
| H1 | 載入純紅 640×480 影像,無 ROI | Source = Entire Image;R 通道只有 Level 255,Count = 307200;G/B 只有 Level 0;Y 只有 Level 76。表格 Y mean 76.24、直方圖 Y mean 76.00,差 0.245(容許差 ≤0.5) |
| H2 | 黑白各半影像,Y 通道 | Level 0 與 255 各 50%;Median = 0(由累積值 ≥ ⌈N/2⌉ 決定);Mean = 127.50 |
| H3 | 拖曳 ROI 後 | 面板切換為 `Drag #n`,Pixels 等於表格 Count;R/G/B Mean/Std 與表格完全一致;Y Mean 與表格差異 ≤0.5 |
| H4 | 點擊表格其他列 | 直方圖跟著切換 |
| H5 | DRAG 模式點擊空白處 / 按 Esc | 回到 Entire Image |
| H6 | 切換 GRID3 | selected = -1,顯示整張;點擊任一格後顯示該格 |
| H7 | Clear All | 回到 Entire Image |
| H8 | 切換通道 / Log / resize | 不重新掃描(可在 debug build 計數 `Hist_Compute` 呼叫次數驗證) |
| H9 | RGB 疊合模式,灰階影像 | 三通道完全重疊,全部顯示為灰色(mask = 7) |
| H10 | 圖表拖曳選取 0..127 | 顯示範圍 Count 與 Percentile;來源改變後選取範圍清除 |
| H11 | 視窗縮小到 < 520px 寬 | 面板自動隱藏;放大後自動恢復 |
| H12 | 編譯 | `-Wall -Wextra` 0 warning |

## 8. 待確認事項

1. **通道名稱「n」**:需求中寫的是「y r g n」,本文件判定「n」為「b」(Blue)的筆誤,並另外加入 RGB 疊合模式(Photoshop 的 Colors)。如果「n」另有所指(例如 Normalized),請告知。
2. **Y 的整數化**:直方圖的 Y 採用四捨五入後的 Level,與表格 Y mean 最多相差 0.5。如果要求完全一致,可以在面板上改為顯示 `analyze.c` 的 double Y mean,只把 bin 用於繪圖。
3. **多 ROI 直方圖**:目前表格為單選,因此直方圖只顯示單一 ROI。如果需要「所有 ROI 聯集」或「多個 ROI 曲線疊合比較」,需要另外定義。
4. **面板形式**:目前設計為主視窗右側嵌入面板。`HistPanel_Create` 只需要一個 parent HWND,如果要改成浮動工具視窗(`WS_POPUP | WS_CAPTION`),模組本身不需要修改。
5. **直方圖輸出**:目前不寫入 log。如果需要 Export 時一併輸出 256 bins(例如 CSV),可以在 `export.c` 增加 `Hist_WriteCSV()`。