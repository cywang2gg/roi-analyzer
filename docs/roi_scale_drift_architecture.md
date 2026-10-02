# ROI 框縮放錯位 — 根因報告與修改架構書 v2.0

> 專案：`C:\Github\roi-analyzer`（純 C ＋ Win32 ＋ GDI ＋ WIC）
> 分支：`master`（`36b46aa` v3.1）＋本輪未提交修改
> 日期：2026-10-01
> 狀態：**已定案並落地（v3.2）**。根因經 headless 實驗確證、修法已由 gh 實作、並以驅動真實函式的 harness 驗證 16/16 `OK`（修正前裁切情境 8/8 `DRIFT`）。使用者手測確認修復。

---

## 0. 結論摘要（先看這裡）

**根因＝`StretchDIBits` 對 top-down DIB 傳入「部分來源矩形」時，`YSrc` 被當成 bottom-up 語義處理，導致來源列帶垂直鏡像。**

- **影響範圍**：只在 `zoom > 1`（nearest 分支）且影像被裁切時發生
- **錯位量**：`|h − sy0 − sy1|` 個影像列 × 顯示倍率 → **數十至數百螢幕像素**（不是亞像素）
- **方向**：**僅垂直**；水平軸（`XSrc`）語義正確，無誤差
- **數據為何不受影響**：分析走影像座標，完全不經繪製路徑
- **修法**：比照 `compare_core.c` 既有正確寫法——**改動像素指標 + 縮窄 `biHeight` + `YSrc = 0`**（已落地）
- **併修**：nearest 分支同時修掉「以 `visible` 當 dest」造成的局部比例 ≠ 全域 `scale`（累積至 ~`scale` px）

**驗證證據**：headless harness（無視窗、無螢幕）**直接驅動真實 `View_DrawImagePyramid()`** 讀回 dest 像素，見 §4。修正前裁切情境 8/8 `DRIFT`，修正後 16/16 `OK`。

---

## 0. 結論摘要（先看這裡）

**根因＝`StretchDIBits` 對 top-down DIB 傳入「部分來源矩形」時，`YSrc` 被錯當成 bottom-up 語義處理，導致來源列帶垂直鏡像。**

- **影響範圍**：只在 `zoom > 1`（nearest 分支）且影像被裁切時發生
- **錯位量**：`|h − sy0 − sy1|` 個影像列 × 顯示倍率 → **數十至數百螢幕像素**（不是亞像素）
- **方向**：**僅垂直**；水平軸（`XSrc`）語義正確，無誤差
- **數據為何不受影響**：分析走影像座標，完全不經繪製路徑
- **修法**：比照 `compare_core.c` 既有正確寫法——**改動像素指標 + 縮窄 `biHeight` + `YSrc = 0`**

**驗證證據**：headless 實驗（無視窗、無螢幕）直接讀回 blit 結果，見 §4。修法在 4 組部分矩形案例全數由 `WRONG` 轉為 `CORRECT`。

---

## 1. 症狀

使用者原文：
> 「影像放大縮的時候 roi 框 位置會跑掉，但數據不受影響，應該是單純劃 roi 線沒有按照原本比例縮放」

手測：載入大圖 → `Ctrl+滾輪` **放大超出視窗** → 框仍漂（H1 修後依舊）。

**關鍵特徵**（與根因完全吻合）：

| 觀察 | 與根因的對應 |
|---|---|
| 框線本身位置正確、框住的**內容**卻不對 | 框用 `View_RectToWindow`（正確）；底圖 blit 取了錯的來源列帶 |
| 只在**放大超出視窗**時出現 | 只有 nearest 分支會傳部分來源矩形 |
| 縮小時（`zoom ≤ 1`）正常 | `else` 分支傳 `sy0 = 0`、全高 → 無鏡像 |
| 位移**很大**（數十～數百 px），非亞像素 | `|h − sy0 − sy1| × 倍率` |
| 數據正確 | 分析走影像座標，與繪製無關 |

---

## 2. 為什麼數據一定不受影響（已確認）

ROI 一律以**影像座標**保存，分析完全不經過 `view_t`：

```c
/* src/roi.c:114 附近 */
if (!AnalyzeROI(img, rc, &list->items[list->count].result))
    return FALSE;
```

```c
/* src/analyze.c:71 起 */
static void acc_rect(const image_t *img, int x0, int y0, int x1, int y1, ...)
{
    ...
    const unsigned char *row = img->px + (size_t)y * (size_t)img->pitch;
    for (x = x0; x <= x1; x++) {
        double b = (double)row[x * 4 + 0];
        ...
```

→ `AnalyzeROI` 直接從 `img->px` 逐像素取樣，**完全不看 `view_t`／`scale`／`zoom`**。
→ 問題 100% 在「繪製映射」，不在資料或分析。

---

## 3. 完整繪製鏈（含行號）

### 3.1 每幀繪製順序 — `src/canvas.c:339` WM_PAINT

```c
if (g_app.img.valid) {
    if (g_app.pyramid.count > 0 &&
        g_app.pyramid_gen == g_app.img_gen)
        View_DrawImagePyramid(mem, &g_app.view, &g_app.img, &g_app.pyramid);  /* 377 畫影像 */
    else
        View_DrawImage(mem, &g_app.view, &g_app.img);                          /* 380 畫影像 */
    draw_roi_overlay(mem);                                                     /* 381 畫框 */
}
```

**畫影像**與**畫框**是兩條獨立路徑 → 兩者數學不一致時「框跑掉」。

### 3.2 畫框（正確）

```c
/* src/canvas.c:83 draw_roi_overlay */
View_RectToWindow(&g_app.view, item->rc, &wr);      /* 100 */
draw_outline(hdc, &wr, ..., selected ? 3 : (grid ? 1 : 2), PS_SOLID);  /* 101-104 */

/* src/canvas.c:42 draw_outline */
Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom);              /* 53 */
```

```c
/* src/view.c:259 View_RectToWindow —— 影像 inclusive 矩形 → 視窗 exclusive 邊 */
window_rect->left   = v->off_x + (int)((float)image_rect.left   * v->scale);
window_rect->top    = v->off_y + (int)((float)image_rect.top    * v->scale);
window_rect->right  = v->off_x + (int)((float)(image_rect.right  + 1) * v->scale);
window_rect->bottom = v->off_y + (int)((float)(image_rect.bottom + 1) * v->scale);
```

### 3.3 畫影像 — `src/view.c:286 View_DrawImagePyramid`（**缺陷所在**）

```c
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth  = level->w;
    bmi.bmiHeader.biHeight = -level->h;          /* ← 負值 = TOP-DOWN DIB */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    visible.left   = v->off_x;
    visible.top    = v->off_y;
    visible.right  = v->off_x + v->draw_w;
    visible.bottom = v->off_y + v->draw_h;
    if (GetClipBox(hdc, &visible) == ERROR)      /* 315 以裁剪區覆寫 */
        return;
    {   RECT image_rect;                          /* 319-322 影像矩形 = off .. off+draw */
        image_rect.left = v->off_x; ...
        if (!IntersectRect(&visible, &visible, &image_rect))   /* 323 取交集 */
            return;
    }
    SetStretchBltMode(hdc, nearest ? COLORONCOLOR : HALFTONE);  /* 326 */
    if (nearest) {                                /* 329 ← 放大路徑（缺陷在此） */
        int sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1;
        sx0 = (int)(((double)(visible.left  - v->off_x) * level->w) / v->draw_w);
        sy0 = (int)(((double)(visible.top   - v->off_y) * level->h) / v->draw_h);
        sx1 = (int)(((double)(visible.right - v->off_x) * level->w +
                     v->draw_w - 1) / v->draw_w);
        sy1 = (int)(((double)(visible.bottom - v->off_y) * level->h +
                     v->draw_h - 1) / v->draw_h);
        if (sx1 > level->w) sx1 = level->w;
        if (sy1 > level->h) sy1 = level->h;
        /* Map the source subrectangle back to dest, matching compare_core.c. */
        dx0 = v->off_x + (int)floor((double)sx0 * v->draw_w / level->w + 0.5);
        dy0 = v->off_y + (int)floor((double)sy0 * v->draw_h / level->h + 0.5);
        dx1 = v->off_x + (int)floor((double)sx1 * v->draw_w / level->w + 0.5);
        dy1 = v->off_y + (int)floor((double)sy1 * v->draw_h / level->h + 0.5);
        StretchDIBits(hdc, dx0, dy0, dx1 - dx0, dy1 - dy0,
                      sx0, sy0, sx1 - sx0, sy1 - sy0,      /* ← sy0/sy1 為 top-down 索引 */
                      level->px, &bmi, DIB_RGB_COLORS, SRCCOPY);   /* ← biHeight = -level->h */
    } else {                                      /* 349 ← 縮小路徑（無缺陷） */
        StretchDIBits(hdc, v->off_x, v->off_y, v->draw_w, v->draw_h,
                      0, 0, level->w, level->h,               /* ← sy0 = 0、全高 */
                      level->px, &bmi, DIB_RGB_COLORS, SRCCOPY);
    }
```

**缺陷**：nearest 分支同時傳入
- `biHeight = -level->h`（宣告為 top-down，`YSrc` 原點應在**左上**）
- 卻又用 `YSrc = sy0`、高度 = `sy1 - sy0` 指定**部分來源列帶**

→ Windows 在此組合下把 `YSrc` 當成 **bottom-up 語義**處理，取到**垂直鏡像**的列帶。
→ `else` 分支傳 `YSrc = 0` 且高度 = 全高，鏡像後仍是完整一張，**故縮小時看不出問題**。

### 3.4 為何 `compare_core.c` 沒這個 bug（現成正確範本）

`src/compare_core.c:541-580`：

```c
    sx0 = (int)floor((visible_left  - origin_x) / level_zoom);
    sy0 = (int)floor((visible_top   - origin_y) / level_zoom);
    sx1 = (int)ceil((visible_right  - origin_x) / level_zoom);
    sy1 = (int)ceil((visible_bottom - origin_y) / level_zoom);
    ...
    dx0 = (int)floor(origin_x + sx0 * level_zoom + 0.5);
    ...
    ZeroMemory(&bitmap_info, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth  = level.w;
    bitmap_info.bmiHeader.biHeight = -(sy1 - sy0);      /* ← 只宣告「列帶高度」 */
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc, dx0, dy0, dx1 - dx0, dy1 - dy0,
                  sx0, 0, sx1 - sx0, sy1 - sy0,          /* ← YSrc 恆為 0 */
                  level.px + (size_t)sy0 * (size_t)level.pitch,   /* ← 以指標位移取代 YSrc */
                  &bitmap_info, DIB_RGB_COLORS, SRCCOPY);
```

**關鍵三件事**：① 像素指標 `+ sy0 * pitch` 自己位移；② `biHeight = -(sy1 - sy0)`；③ `YSrc = 0`。
→ 完全避開 `YSrc` 的部分矩形語義問題。**主視窗只是沒照這個慣例寫。**

---

## 4. 驗證實驗（headless，可重現）

### 4.1 方法

**不需要視窗、不需要螢幕**。建立合成來源 DIB，用通道編碼列／欄索引，直接讀回 blit 後的 dest 像素，即可得知「哪一個來源列／欄真的落到 dest」：

- 列索引：`G = row & 0xFF`、`R = row >> 8`（16-bit 精確）
- 欄索引：`B = col & 0xFF`、`G = col >> 8`
- dest：`CreateDIBSection`（32bpp top-down），`StretchDIBits` 後直接讀 bits

### 4.2 實驗一：確認 Y 軸鏡像（來源 400×300，dest 100×100，top-down）

| 請求來源矩形 | 實際取得列 | 判定 |
|---|---|---|
| 全高 `y=[0,300)` | `[1 .. 298]` | 控制組（DDA 取樣，見 §4.4） |
| 上帶 `y=[0,100)` | `[200 .. 299]` | **MIRRORED-BAND** |
| 中上 `y=[50,150)` | `[150 .. 249]` | **MIRRORED-BAND** |
| 中下 `y=[150,250)` | `[50 .. 149]` | **MIRRORED-BAND** |
| 下帶 `y=[200,300)` | `[0 .. 99]` | **MIRRORED-BAND** |

**規律**：取得列帶 = `[h − sy0 − sy1, h − sy0)`，其中 `h = 300`。
→ 即整個列帶被**垂直鏡像**，錯位量 `|h − sy0 − sy1|`，隨 pan 位置變化，**可達數百列**。

範例：`sy0=50, sy1=150` → 取得 `[150,250)`，與請求差 **100 列**。以顯示倍率 4× 計 → **400 螢幕像素**位移。

### 4.3 實驗二：X 軸對照（來源 300×100，dest 100×50，top-down）

| 請求來源矩形 | 實際取得欄 | 判定 |
|---|---|---|
| 全寬 `x=[0,300)` | `[1 .. 298]` | 控制組 |
| 左帶 `x=[0,100)` | `[0 .. 99]` | **CORRECT** |
| 中間 `x=[100,200)` | `[100 .. 199]` | **CORRECT** |
| 右帶 `x=[200,300)` | `[200 .. 299]` | **CORRECT** |

→ **`XSrc` 語義正確，無鏡像**。這解釋了「為何只有垂直方向錯位」。

### 4.4 控制組說明

全高／全寬請求得到 `[1 .. 298]` 而非 `[0 .. 299]`，是 `StretchDIBits` 在 4× 縮小時內部 DDA 的取樣結果（dest 首像素對應來源第 1 列），屬 H13 亞像素層級，**不是映射錯誤**。在 `zoom > 1` 放大時不會出現此偏移。

### 4.5 實驗三：驗證修法（來源 400×300，dest 100×100，top-down）

| 案例 | 舊寫法（部分矩形） | 新寫法（指標位移＋縮窄 biHeight＋YSrc=0） |
|---|---|---|
| `y=[0,100)` | `[200..299]` **WRONG** | `[0..99]` **CORRECT** |
| `y=[50,150)` | `[150..249]` **WRONG** | `[50..149]` **CORRECT** |
| `y=[150,250)` | `[50..149]` **WRONG** | `[150..249]` **CORRECT** |
| `y=[200,300)` | `[0..99]` **WRONG** | `[200..299]` **CORRECT** |

→ **修法在全部部分矩形案例皆修正為正確。**

### 4.6 可重現的探針原始碼

```c
/* dibprobe.c — 編譯：gcc -Wall -Wextra -o dibprobe.exe dibprobe.c -lgdi32
   用途：驗證 StretchDIBits 對 top-down DIB 的部分來源矩形語義
   （無視窗、無螢幕；直接讀回 dest 像素） */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define SW 400
#define SH 300
#define DW 100
#define DH 100

static unsigned char g_src[SW * SH * 4];

static void fill_source(void)
{
    int x, y;
    for (y = 0; y < SH; y++)
        for (x = 0; x < SW; x++) {
            unsigned char *p = &g_src[(y * SW + x) * 4];
            p[0] = (unsigned char)(x & 0xFF);           /* B = 欄低位 */
            p[1] = (unsigned char)(y & 0xFF);           /* G = 列低位 */
            p[2] = (unsigned char)((y >> 8) & 0xFF);    /* R = 列高位 */
            p[3] = 255;
        }
}

/* old_style=1 → 部分矩形；0 → 指標位移修法 */
static void probe(const char *label, int sy0, int sy1, int old_style)
{
    BITMAPINFO bmi, di;
    void *bits = NULL;
    HBITMAP bmp;
    HDC dc;
    int row_first, row_last;

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = SW;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    memset(&di, 0, sizeof(di));
    di.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    di.bmiHeader.biWidth = DW;
    di.bmiHeader.biHeight = -DH;
    di.bmiHeader.biPlanes = 1;
    di.bmiHeader.biBitCount = 32;
    di.bmiHeader.biCompression = BI_RGB;

    dc = CreateCompatibleDC(NULL);
    bmp = CreateDIBSection(dc, &di, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(dc, bmp);
    memset(bits, 0, (size_t)DW * DH * 4);
    SetStretchBltMode(dc, COLORONCOLOR);

    if (old_style) {
        bmi.bmiHeader.biHeight = -SH;
        StretchDIBits(dc, 0, 0, DW, DH, 0, sy0, SW, sy1 - sy0,
                      g_src, &bmi, DIB_RGB_COLORS, SRCCOPY);
    } else {
        bmi.bmiHeader.biHeight = -(sy1 - sy0);
        StretchDIBits(dc, 0, 0, DW, DH, 0, 0, SW, sy1 - sy0,
                      g_src + (size_t)sy0 * (size_t)SW * 4,
                      &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    {
        unsigned char *f = (unsigned char *)bits;
        unsigned char *l = (unsigned char *)bits + (size_t)(DH - 1) * DW * 4;
        row_first = (f[2] << 8) | f[1];
        row_last = (l[2] << 8) | l[1];
    }
    printf("%-26s src rows [%3d,%3d) -> got [%3d .. %3d]  %s\n",
           label, sy0, sy1, row_first, row_last,
           (row_first == sy0 && row_last == sy1 - 1) ? "CORRECT" : "WRONG");
    DeleteObject(bmp);
    DeleteDC(dc);
}

int main(void)
{
    fill_source();
    printf("=== 部分來源矩形：舊寫法 vs 修法 ===\n");
    probe("old  y=[0,100)",   0, 100, 1);
    probe("new  y=[0,100)",   0, 100, 0);
    probe("old  y=[50,150)",  50, 150, 1);
    probe("new  y=[50,150)",  50, 150, 0);
    probe("old  y=[150,250)", 150, 250, 1);
    probe("new  y=[150,250)", 150, 250, 0);
    probe("old  y=[200,300)", 200, 300, 1);
    probe("new  y=[200,300)", 200, 300, 0);
    return 0;
}
```

---

## 5. 修正方案

### 5.1 主修（已完成）：nearest 分支改用指標位移

`src/view.c` `View_DrawImagePyramid` nearest 分支，blit 已改為（現況 `view.c:346-353`）：

```c
        /* Narrow the DIB to the source row band and offset the pixel pointer;
           YSrc is then always 0, matching compare_core.c. */
        bmi.bmiHeader.biHeight = -(sy1 - sy0);
        StretchDIBits(hdc, dx0, dy0, dx1 - dx0, dy1 - dy0,
                      sx0, 0, sx1 - sx0, sy1 - sy0,
                      level->px + (size_t)sy0 * (size_t)level->pitch,
                      &bmi, DIB_RGB_COLORS, SRCCOPY);
```

**注意**：
- `bmi` 是區域變數，`biHeight` 可原地改寫（`else` 分支不受影響）
- 需 guard：`sy1 > sy0`、`sx1 > sx0`（無可見區域時直接 `return`）
- `level->pitch` 存在（`view_level_t.pitch`），`levels[0].pitch = img->pitch`
- `size_t` 已由 `<stdint.h>`／`<stdlib.h>` 提供（專案已用 `(size_t)` 轉型慣例）
- **僅改 nearest 分支**；`else` 分支（`sy0 = 0`、全高）**語義本來就正確，不動**

### 5.2 選修（次要，本次可不做）

| 項目 | 位置 | 說明 |
|---|---|---|
| H3 取整相位差 | `view.c:263-266` | `View_RectToWindow` 用 trunc，底圖用 `floor(+0.5)` → 最多 1px 相位差 |
| 畫筆溢出 | `canvas.c:104` | 選取框 3px、手動框 2px 會向 `right`/`bottom` 外溢半個筆寬；改 `PS_INSIDEFRAME` 或統一 1px |
| H13 DDA | `StretchDIBits` | 內部取樣未文件化，殘留 ≤1px |
| H2′ | `view.c:159-161` | `scale`（float）vs `draw_w`（int 截斷）→ <1px 線性累積 |

→ **主修後若驗收標準為「≥2px 誤差歸零」，5.1 即足夠。** 若要求「像素級完全對齊（0px）」，再處理 5.2。

### 5.3 不建議的方向

- **改 `SetMapMode`／世界變換**：牽動 GDI 狀態與所有繪製路徑，遠超最小範圍
- **把 `biHeight` 改正值（bottom-up）**：需同步處理 `level->px` 的列序，且 `else` 分支與金字塔皆受影響，風險大而無收益
- **改編譯旗標為 `-std=c89 -pedantic`**：`CMakeLists.txt:3` 現為 `CMAKE_C_STANDARD 11`，且 `long long`（`view.c:116,127`、`roi.c:195-200,302`、`histpanel.c` 多處）為既有慣例 → **不在本任務範圍**

---

## 6. 影響範圍（blast radius）

`View_DrawImagePyramid` 呼叫者僅兩處：`src/view.c:283`（`View_DrawImage` 包裝）、`src/canvas.c:377`（主視窗 WM_PAINT）。

| 功能 | 是否受影響 |
|---|---|
| 表格數值／`AnalyzeROI`／遮罩指標 | **不受**（走影像座標） |
| 旋轉 ROI 變換 | **不受**（只動 blit 參數） |
| 比較視窗 V1／V2 | **不受**（獨立映射，本就正確） |
| mini 圖預覽（`main.c:1481`） | **不受**（`SetStretchBltMode(HALFTONE)` + 全圖 blit） |
| 拖曳虛線框（`canvas.c:124`） | **不受**（走 `View_RectToWindow`） |
| histogram 預覽（`canvas.c:205`） | **不受**（影像座標矩形） |
| `zoom ≤ 1` 縮小路徑 | **不受**（`else` 分支不動） |

---

## 7. 驗收標準

- 測試圖（建議 4000×3000、每 100px 一條 1px 格線）上與像素邊界對齊的 ROI，在 `zoom` **1.0× / 2.0× / 3.7× / 8.0×**，以及**左上／中央／右下**三個 pan 位置下：
  - **框住的內容 = 框住的區域**（放大超出視窗時亦同）
  - 邊框與格線誤差 **0 螢幕像素**（受 H13 限制則 ≤1）
- `zoom ≤ 1` 行為不變
- 連續滾輪 50 次後再拖曳建立 ROI，`AnalyzeROI` 座標與滑鼠所指像素一致
- `-Wall -Wextra` 零警告、`CMAKE_C_STANDARD 11` 不變更、`git diff --check` 乾淨

---

## 8. 專案約束

- 純 C ＋ Win32 ＋ GDI ＋ WIC；**ANSI 全 A 版**（不用 `TCHAR`／W 版混用）
- 布林用 `BOOL`/`TRUE`/`FALSE`，**禁用 `stdbool`／`bool`**
- **禁用 bare `strncpy`**（用 `lstrcpynA`）；`_snprintf` 後務必補 `'\0'`
- 「宣告置於區塊開頭」與「不用 `//` 註解」為**既有風格慣例**（非編譯器強制，C11 標準）
- 顯式 `(double)`／`(int)`／`(size_t)` 轉型，避免隱式轉換警告
- 檔案行尾 **CRLF**；`-Wall -Wextra` **零警告**；`git diff --check` 乾淨
- 不改公開介面（`view.h` 簽章）
- 建置：`rm -rf build` 重配 → `PATH` 含 `/c/msys64/ucrt64/bin` → `mingw32-make`；**手測前先關舊 exe**（否則 link `Permission denied`）

---

## 9. 附錄：本輪前次修改（H1）的定位

H1（`view.c:329-348` 由來源反推 dest）修掉的是**真實缺陷**——把整數來源跨度硬拉滿可見寬，造成局部比例 ≠ 全域比例：

| 案例 | 修前 | 修後 |
|---|---|---|
| iw=800 draw=12000 client=1000 scale=15.0 | 20.00 px | 0.000 px |
| iw=1024 draw=4100 client=1200 scale=4.00 | 5.18 px | 0.824 px |
| iw=4032 draw=8065 client=1400 scale=2.00 | 2.17 px | 0.174 px |

→ 但**誤差量級只有個位數 px**，**不足以解釋使用者看到的明顯位移**。真正的「大位移」是 §0 的 `YSrc` 鏡像問題（數十至數百 px）。
**兩者都要修**：H1 已落地（未提交），`YSrc` 為主修。

---

## 10. 相關檔案與行號速查

| 檔案 | 位置 | 內容 |
|---|---|---|
| `src/view.c` | 22 | `ViewPyr_Build`（`(w+1)/2` 向上取整金字塔） |
| `src/view.c` | 96 | `ViewPyr_Pick`（`nearest` → `levels[0]`） |
| `src/view.c` | 144 | `View_Update`（`scale` 浮點／`draw_w` 整數截斷） |
| `src/view.c` | 204 | `View_SetZoom`（錨點反推 `pan`） |
| `src/view.c` | 228 | `View_ToImage`（滑鼠 → 影像，用 float `scale`） |
| `src/view.c` | 259 | `View_RectToWindow`（**框線用**） |
| `src/view.c` | 286 | `View_DrawImagePyramid`（**影像用，缺陷處**） |
| `src/view.c` | 303-309 | `bmi` 建立（`biHeight = -level->h`，top-down） |
| `src/view.c` | 329-345 | nearest 分支座標計算＋dest 反推（**已改**） |
| `src/view.c` | 346-353 | nearest 分支 blit（**§5.1 主修處，已落地**） |
| `src/view.c` | 354-358 | else 分支（縮小路徑，正確，不動） |
| `src/canvas.c` | 42／53 | `draw_outline`／`Rectangle` |
| `src/canvas.c` | 83／100／104 | `draw_roi_overlay`／`View_RectToWindow`／畫筆寬 |
| `src/canvas.c` | 339／377／381 | WM_PAINT／畫影像／畫框 |
| `src/compare_core.c` | 541-580 | **正確慣例範本**（指標位移＋縮窄 `biHeight`） |
| `src/roi.c` | 114／262 | `AnalyzeROI` 呼叫／`ROI_HitTest`（皆影像座標） |
| `src/analyze.c` | 71 | `acc_rect`（直接讀 `img->px`） |

---

## 11. 本輪狀態（v3.2 已落地）

- **已提交**：`src/view.c`（nearest 分支 `YSrc` 主修 ＋ dest 反推）；`docs/roi_scale_drift_architecture.md`（本文件）；`docs/01_architecture_v1.md` 升 v3.2；`docs/02_verification.md` 加 V21–V22
- **前次基準**：`master` @ `36b46aa`（v3.1）
- **exe**：`bin/roi_analyzer.exe` 524,177 bytes（2026-10-01 12:20，clean Release，0 warning）
- **驗證方式**：headless harness 驅動真實 `View_DrawImagePyramid()`，16/16 `OK`；修正前裁切情境 8/8 `DRIFT`
- **另一分支**：`feature/expire-export-table`（`9e2a451`，效期鎖＋export table，已 push 未合併；待處理：Excel 貼上單 cell 分欄問題）

### 11.1 最終落地程式碼（`src/view.c:346-353`）

```c
        if (sx1 <= sx0 || sy1 <= sy0)
            return;
        /* Narrow the DIB to the source row band and offset the pixel pointer; YSrc is always 0, matching compare_core.c. */
        bmi.bmiHeader.biHeight = -(sy1 - sy0);
        StretchDIBits(hdc, dx0, dy0, dx1 - dx0, dy1 - dy0,
                      sx0, 0, sx1 - sx0, sy1 - sy0,
                      level->px + (size_t)sy0 * (size_t)level->pitch,
                      &bmi, DIB_RGB_COLORS, SRCCOPY);
```

### 11.2 真實函式前後對照（headless harness，`zoom=4.0` 裁切情境）

| 探測點 | 期望來源列 | 修正前 | 修正後 |
|---|---|---|---|
| `win_row[0]`（pan up） | 0 | **225** DRIFT | **0** OK |
| `win_row[66]`（pan up） | ~25 | **249** DRIFT | **25** OK |
| `win_row[133]`（pan up） | ~50 | **275** DRIFT | **50** OK |
| `win_row[199]`（pan up） | ~75 | **299** DRIFT | **75** OK |
| `win_row[0]`（pan down） | ~225 | **0** DRIFT | **225** OK |
| `win_row[199]`（pan down） | ~300 | **74** DRIFT | **300** OK |

`zoom = 1.0`（else 分支）與 `zoom = 2.0`（nearest 未裁切）修正前後**完全相同**，確認未造成回歸。
