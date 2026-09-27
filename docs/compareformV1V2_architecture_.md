

---

# CompareForm 移植架構書（roi_analyzer v2.7 增補，2026-09-27）

> 將 C# `CompareForm`（V1 並排）與 `CompareFormV2`（V2 分割線疊加）移植到 roi_analyzer。維持純 C＋Win32＋GDI、ANSI 建置、無第三方依賴。
> 實作約 3200 行（compare.h 123／compare_image.c 155／compare_core.c 437／compare_snap.c 492／compare_v1.c 1009／compare_v2.c 1017，另 main.c 增補約 340、export.c +19）。與主程式共用 `Image_Load`（WIC→GDI+）與 `view_pyr` 金字塔，其餘自成模組。
> v2.7 定稿修正 C1～C10（見 §13 定稿附錄）取代草案對應段落；v2.7 增補（§14 主視窗多檔拖放＋Snapshot、拍板 S1～S7、T1～T3、C11）。`Report` 延後（P6 保留設計）。

## 變更歷史

- **v2.7 增補（2026-09-27）**：主視窗多檔拖放（§14.1；`CmpDrop_Collect`、`cmp_open_mode_t`、S1＋S2＋S3、T2 附註、T3 目前）與 Snapshot（§14.2；`compare_snap.c`、S4～S7、T1 `Info bar`）。C11 路徑拆解 DBCS 安全（`export.c` 一併修正）。增補行數以實作檔案為準（見上）。

- **v2.7 定稿（2026-09-27）**：P0～P4 實作完成。`Image_Clone`；V1（2～4 張並排，Lock 同步）；V2（疊加分割線、`Swap`、各自倍率、像素讀值、雙擊重設）。影像上限 `CMP_MAX_LIVE_IMG=8` 預先檢查；拖放用 `DragQueryFileW`＋嚴格 ACP；主迴圈加速鍵包覆 `root==main`（C6）；IDM 155／156；直方圖 ID 移至 161～166。V2 控制列改為比例式（Zoom 佔半、Pan／Actions 平分剩餘）。
- **v2.7（本草案）**：新增比較視窗 V1（2～4 張並排，Lock 同步）與 V2（兩張疊加，可拖曳分割線、`Swap`、各自倍率）。影像以參考計數共享。統一「視口中心影像座標」模型。修正 C# 版縮放量化卡死、fit 溢出、分割線不隨縮放等問題。`Report` 延後。

---

## 0. 決策摘要

第 1 輪提出的決策尚未回覆，以下採建議方案，並全部列入 §12 待確認。

| # | 議題 | 本版採用 |
|---|---|---|
| D1 | 進入點 | `View > Compare Files…`（`Ctrl+K`，自動含目前影像，開 V1）；`View > Compare Current with Next`（`K`，開 V2）；V1 內 `V2 ▶` 按鈕 |
| D2 | `Report` 銳利度分析 | 不納入本版，§10 Phase 5 保留設計 |
| D3 | V1→V2 影像交付 | 參考計數 `cmp_image_t`，零複製；目前影像以 `Image_Clone` 記憶體複製（不重新解碼，也不觸發雲端重新下載） |
| D4 | 縮放 | V1／V2 統一：乘法 ×1.15 與 ÷1.15，量化 1%，範圍 0.02～8，跨越 1.0 時吸附至 1.0 |
| D5 | ROI 疊加 | 非目標 |
| D6 | 座標模型 | 統一存「視口中心對應的影像座標 \((u,v)\)＋倍率」，不沿用 C# 的左上角制或中心制 |
| D7 | 視窗關係 | 無 owner 的獨立頂層視窗（有自己的工作列按鈕，Z 順序不壓住主視窗）；主視窗關閉時統一關閉 |

---

## 1. 目標與非目標

**目標**

- V1：2～4 張並排，版面 1×2／3×1／2×2。Lock 開啟時縮放與平移全體同步，以游標錨點縮放、拖曳平移、雙擊回到 fit。每格底部顯示狀態帶，右上角有關閉鈕。可拖放加圖，剩餘少於 2 張時關閉視窗。
- V2：兩張圖畫在同一畫布，分割線左側顯示左圖、右側顯示右圖。可拖曳分割線、切換移線／平移模式、同步平移、選擇平移目標、`Swap`、`Reset All`，左右倍率可用滑桿分別調整，滾輪為雙圖同步錨點縮放。
- 放大檢查像素時與主程式一致：縮小走金字塔＋`HALFTONE`，放大只取可見子矩形並以最近鄰繪製。

**非目標**：`Report`（本版）、ROI 疊加、比較籃、比較視窗內的分析或直方圖、V1 超過 4 張、V2 超過 2 張、截圖或匯出、閃爍比較、差異圖。

---

## 2. C# → Win32 對照與修正項

### 2.1 機制對照

| C# | roi_analyzer | 說明 |
|---|---|---|
| `ImagePanel` 物件 | `cmp_cell_t` 結構 | 主視窗只管理 `cell[4]`＋`n` |
| 每格一個 `PictureBox` | V1 **單一** grid 子視窗自繪所有格 | 跨格拖曳、滾輪路由、閃爍問題一次消除 |
| `TableLayoutPanel` 三態 | `WM_SIZE` 內手算格子矩形 | 格線 1px 自繪 |
| 每格 `StatusStrip`＋`X` 標籤 | 自繪狀態帶＋關閉鈕命中區 | 不另建 HWND |
| `Image.FromFile`／`Clone()` | `CmpImage_Load`／`CmpImage_Ref` | 參考計數，誰 Ref 誰 Unref |
| `ImageLocation`（左上）／`_panLocation`（中心） | `cmp_view_t{zoom,u,v}` | §4.2 |
| Paint 內箝制並寫回狀態 | 狀態變更點呼叫 `CmpView_ClampEdges` | 繪製函式不修改狀態 |
| `DrawImage`＋插值切換 | `Cmp_Blit`（金字塔＋可見子矩形） | §5.2 |
| `SetClip` | `SaveDC`＋`IntersectClipRect`＋`RestoreDC` | 分割線在 `RestoreDC` 之後畫 |
| `GroupBox` 內含控制項 | 群組框只當外框，控制項與它是兄弟視窗 | Win32 群組框不轉發 `WM_COMMAND`／`WM_HSCROLL` |
| `TrackBar.ValueChanged` | `WM_HSCROLL` | `TBM_SETPOS` 不會送出 `WM_HSCROLL`，不會回授迴圈 |
| `BeginInvoke` 延後 fit | 首次收到非零尺寸 `WM_SIZE` 時 fit | `need_fit` 旗標 |
| `Screen.FromPoint(Cursor)` | `MonitorFromPoint`＋`GetMonitorInfoA` 的 `rcWork` | |
| `Form` 釋放鏈 | `WM_DESTROY` 內 Unref，`WM_NCDESTROY` 內釋放狀態 | |

### 2.2 C# 版問題與本版修正

| # | C# 行為 | 問題 | 本版處理 |
|---|---|---|---|
| F1 | 滾輪 ×1.15／×0.85 後量化到 5%，差值小於 0.001 時直接返回 | **會卡死**。0.15×0.85=0.1275→0.15、0.15×1.15=0.1725→0.15，雙向都卡；0.10×1.15=0.115→0.10 無法放大；0.05 同理。下限 0 時一旦到 0 就永遠卡住 | 量化改為 1%；量化後若未變，強制前進 ±0.01 |
| F2 | fit 倍率四捨五入到 5% | 例如 0.09→0.10 會讓圖溢出格子 | fit 倍率改為**無條件捨去**到 1% |
| F3 | `TrackBar` 最小值 0 | 0 倍會得到零尺寸繪製 | 範圍改為 2～800（%） |
| F4 | ×1.15 與 ×0.85 不對稱 | 1.15×0.85=0.9775，來回滾動會漂移 | 改用 ×1.15 與 ÷1.15 |
| F5 | V2 最大 2 倍 | 不足以檢查像素，與 V1 的 10 倍不一致 | 統一為 0.02～8（與主程式上限相同） |
| F6 | `Resize` 不更新分割線與平移中心 | 視窗縮小後分割線可能落在畫布外 | 分割線以比例 `split_frac` 保存；視圖以中心影像座標保存，縮放視窗時內容自然保持置中 |
| F7 | `_panSourcePanel` 用來源格座標與別格事件座標相減 | 混用座標系 | 單一 grid HWND＋`SetCapture`，所有座標同屬一個座標系 |
| F8 | V1＋V2 同時存在 4 份點陣圖 | 記憶體暴增 | 參考計數共享 |
| F9 | §3.5 對 `Swap` 的說明自相矛盾：決定式寫「`rbPanLeft` 時正常為 Image1、對調後為 Image2」（目標跟著**畫面位置**），`Swap` 項目卻寫「操作目標跟著圖走」 | 兩種解讀結果相反 | 採用決定式（程式層級）：**選項按鈕永遠代表畫面左／右**，目標 = `pan_side ^ swap` |
| F10 | V2 左右倍率不同時，滾輪以哪一邊為基準未定義 | | 以**畫面左側**倍率為基準，兩張圖各自以自己的舊倍率計算錨點 |
| F11 | V1 從 Lock 關閉切到開啟時的行為未定義 | 各格倍率不同時，第一次滾動會跳動 | 切到開啟時執行 `FitAll`（取共同倍率） |
| F12 | `Report` 需先寫暫存 BMP 再分析 | 多餘的磁碟 I/O | 若日後移植，直接從記憶體緩衝分析 |

### 2.3 與 C# 行為刻意不同之處

- **滑桿縮放的錨點**：C# 固定圖心；本版固定「視口中心所看到的影像點」，也就是目前正在看的位置保持不動。初始狀態兩者一致。
- **右鍵拖曳一律平移**（V1、V2 皆是），與主程式的右鍵平移慣例一致；V2 中右鍵平移不受移線模式影響。
- 滾輪依 120 累加，觸控板的高解析度滾動不會一次跳多格。

---

## 3. 檔案與建置

### 3.1 新增與修改的檔案

| 檔案 | 行數（實際） | 內容 |
|---|---|---|
| `compare.h` | 72 | 型別、常數、公開 API |
| `compare_image.c` | 147 | `cmp_image_t` 參考計數、金字塔轉接層 |
| `compare_core.c` | 262 | 縮放與視圖數學、`Cmp_Blit`、視窗登錄表、`Compare_PreTranslate` |
| `compare_v1.c` | 857 | V1 頂層視窗＋grid 子視窗＋`Compare_Init` |
| `compare_v2.c` | 835 | V2 頂層視窗＋overlay 子視窗 |
| `image.c`／`image.h`（修改） | +27／+1 | `Image_Clone` |
| `main.c`（修改） | +304／-22 | 選單、按鍵、訊息迴圈掛勾、多選開檔、關閉時清理 |

### 3.2 CMake

所需函式庫（`user32 gdi32 comctl32 comdlg32 shell32`）都已連結，只需在既有 `add_executable` 清單中加入原始檔：

```cmake
# File: CMakeLists.txt
# 在既有 add_executable(roi_analyzer ...) 的原始檔清單追加：
    compare_image.c
    compare_core.c
    compare_v1.c
    compare_v2.c
```

建置流程不變，仍須零警告。

### 3.3 對既有程式碼的假設（**v2.7 已完成逐項核對**）

| 項目 | 假設 | 核對結果 |
|---|---|---|
| `image_t` | 欄位有 `px`（BGRA）、`w`、`h`、`pitch=w*4`、`char path[]`、`valid` | ✅ 相符（`image.h:9`） |
| `Image_Load` | `BOOL Image_Load(image_t *img, const char *path)`，入口會 `ZeroMemory` | ✅ 相符 |
| `Image_Free` | 對全零結構呼叫是安全的 | ✅ 相符 |
| 像素配置器 | `Image_Free` 以 `free()` 釋放 | ✅ `Image_Clone` 寫在 `image.c` 內，使用 `malloc` |
| `view_pyr_t` | 可用 `ViewPyr_Build` 建立、`ViewPyr_Free` 釋放，並可取得第 k 層 BGRA 緩衝 | ⚠️ `view_level_t{px,w,h,pitch,owned}`＋`view_pyr_t{levels[6],count}`；`Build` 回傳 `BOOL`，`levels[0]` 是原圖參照（見 C2） |
| 主視窗 HWND | 本文以 `g_app.hwnd_main` 代稱 | ✅ 欄位名正確 |
| 方向鍵過濾 | 以 `GetAncestor(msg.hwnd, GA_ROOT)` 判斷是否為主視窗 | ✅ `App_NavKeyAllowed` 已有此檢查（C4） |
| 中文字串 | ANSI 建置下的中文訊息沿用主程式既有做法 | ✅ 比較視窗控制項文字一律英文 |
| DPI | 未知 manifest 是否宣告 `dpiAware` | ✅ 確認**未宣告**（Q4）；150% 縮放下 100% 非真正 1:1 |
| 加速鍵作用域 | 未預期 | ❌ **`TranslateAcceleratorA` 不檢查 `msg.hwnd`**，須自行包覆（C6，實作時修正） |

---

## 4. 核心資料

### 4.1 `cmp_image_t`：參考計數的共享影像

```c
/* File: compare.h */
#ifndef COMPARE_H
#define COMPARE_H

#include <windows.h>
#include "image.h"
#include "view.h"

#define CMP_ZOOM_MIN      0.02
#define CMP_ZOOM_MAX      8.0
#define CMP_ZOOM_STEP     1.15
#define CMP_MAX_CELLS     4
#define CMP_MAX_WINDOWS   8
/* CMP_MAX_LIVE_IMG=8（定稿 Q11）；比較命令 ID 在 app.h（155／156），詳見 §13 C1～C3 */

#define CMPM_KEY          (WM_APP + 0x40)

typedef struct cmp_image {
    image_t        img;
    view_pyr_t     pyr;
    BOOL           pyr_built;
    BOOL           pyr_failed;
    volatile LONG  refs;
    char           name[MAX_PATH];
} cmp_image_t;

typedef struct {
    double zoom;
    double u;
    double v;
} cmp_view_t;

typedef struct {
    const BYTE *px;
    int         w;
    int         h;
    int         pitch;
    int         shift;
} cmp_level_t;

/* compare_image.c */
cmp_image_t *CmpImage_Load(const char *path);
cmp_image_t *CmpImage_FromImage(const image_t *src);
cmp_image_t *CmpImage_Ref(cmp_image_t *ci);
void         CmpImage_Unref(cmp_image_t *ci);
LONG         CmpImage_LiveCount(void);
int          CmpImage_Levels(cmp_image_t *ci);
BOOL         CmpImage_Level(cmp_image_t *ci, int k, cmp_level_t *out);

/* compare_core.c：數學與繪製 */
double CmpZoom_Quantize(double z);
double CmpZoom_Step(double z, int dir);
void   CmpView_Fit(cmp_view_t *v, int W, int H, int vw, int vh, double margin);
void   CmpView_ZoomAt(cmp_view_t *v, double znew, double mx, double my, int vw, int vh);
void   CmpView_Pan(cmp_view_t *v, double dx, double dy);
void   CmpView_ClampEdges(cmp_view_t *v, int W, int H, int vw, int vh);
void   CmpView_ScreenToImage(const cmp_view_t *v, int vw, int vh,
                             double sx, double sy, double *ix, double *iy);
void   Cmp_Blit(HDC hdc, cmp_image_t *ci, const cmp_view_t *v,
                const RECT *vp, const RECT *clip);

/* compare_core.c：視窗登錄表 */
BOOL   CmpReg_Add(HWND hwnd);
void   CmpReg_Remove(HWND hwnd);
BOOL   Compare_PreTranslate(MSG *msg);
void   Compare_CloseAll(void);

/* compare_v1.c / compare_v2.c */
BOOL   Compare_Init(HINSTANCE hinst, HFONT ui_font);  /* 註冊兩個視窗類別＋保存字型 */
HWND   CompareV1_Open(cmp_image_t **imgs, int n);
HWND   CompareV2_Open(cmp_image_t *a, cmp_image_t *b);

#endif
```

**所有權規則**：

1. `CmpImage_Load`／`CmpImage_FromImage` 回傳時 `refs=1`，由呼叫端持有。
2. `CompareV1_Open`／`CompareV2_Open` 內部**自行** `Ref` 每一張圖；呼叫端開啟後照常 `Unref` 自己持有的那份。
3. 視窗在 `WM_DESTROY` 中 `Unref` 自己持有的所有圖。
4. V1 按下 `V2 ▶` 時直接把 `cell[0].ci`／`cell[1].ci` 傳給 `CompareV2_Open`，由 V2 自行 Ref。V1 先關閉也不影響 V2。
5. `refs` 使用 `Interlocked*` 操作，為日後 Phase 5 背景執行緒預留。

```c
/* File: compare_image.c */
#include <stdlib.h>
#include <string.h>
#include "compare.h"

static volatile LONG s_live = 0;

static cmp_image_t *alloc_ci(void)
{
    cmp_image_t *ci = (cmp_image_t *)calloc(1, sizeof(*ci));
    if (ci) {
        ci->refs = 1;
        InterlockedIncrement(&s_live);
    }
    return ci;
}

static void set_name(cmp_image_t *ci, const char *path)
{
    const char *a = strrchr(path, '\\');
    const char *b = strrchr(path, '/');
    const char *p = (a > b) ? a : b;
    lstrcpynA(ci->name, p ? p + 1 : path, MAX_PATH);
}

cmp_image_t *CmpImage_Load(const char *path)
{
    cmp_image_t *ci;
    if (!path || !path[0])
        return NULL;
    ci = alloc_ci();
    if (!ci)
        return NULL;
    if (!Image_Load(&ci->img, path) || !ci->img.valid) {
        CmpImage_Unref(ci);
        return NULL;
    }
    set_name(ci, path);
    return ci;
}

cmp_image_t *CmpImage_FromImage(const image_t *src)
{
    cmp_image_t *ci = alloc_ci();
    if (!ci)
        return NULL;
    if (!Image_Clone(&ci->img, src)) {
        CmpImage_Unref(ci);
        return NULL;
    }
    set_name(ci, src->path);
    return ci;
}

cmp_image_t *CmpImage_Ref(cmp_image_t *ci)
{
    if (ci)
        InterlockedIncrement(&ci->refs);
    return ci;
}

void CmpImage_Unref(cmp_image_t *ci)
{
    if (!ci)
        return;
    if (InterlockedDecrement(&ci->refs) == 0) {
        ViewPyr_Free(&ci->pyr);
        Image_Free(&ci->img);
        free(ci);
        InterlockedDecrement(&s_live);
    }
}

LONG CmpImage_LiveCount(void)
{
    return s_live;
}

/* ---- 金字塔轉接層：只有這裡碰 view_pyr_t 內部，請依實際 view.h 調整 ---- */

static void pyr_build(cmp_image_t *ci)
{
    if (ci->pyr_built || ci->pyr_failed)
        return;
    if (ViewPyr_Build(&ci->pyr, &ci->img))
        ci->pyr_built = TRUE;
    else
        ci->pyr_failed = TRUE;
}

int CmpImage_Levels(cmp_image_t *ci)
{
    pyr_build(ci);
    /* 第 0 層為原圖；第 k>=1 層為金字塔第 k-1 層 */
    return ci->pyr_built ? 1 + ci->pyr.count : 1;
}

BOOL CmpImage_Level(cmp_image_t *ci, int k, cmp_level_t *out)
{
    const image_t *src;
    if (k <= 0) {
        src = &ci->img;
    } else {
        if (!ci->pyr_built || k - 1 >= ci->pyr.count)
            return FALSE;
        src = &ci->pyr.level[k - 1];
    }
    if (!src->px || src->w <= 0 || src->h <= 0)
        return FALSE;
    out->px    = (const BYTE *)src->px;
    out->w     = src->w;
    out->h     = src->h;
    out->pitch = src->pitch;
    out->shift = (k <= 0) ? 0 : k;
    return TRUE;
}
```

`ci->pyr.count`／`ci->pyr.level[]` 是假設的欄位名稱。若主程式的 `ViewPyr_Build` 需要世代號參數，可固定傳入 1，因為 `cmp_image_t` 建立後內容不會改變。

```c
/* File: image.c */
/* 追加；必須使用與 Image_Free 相同的配置器 */
BOOL Image_Clone(image_t *dst, const image_t *src)
{
    size_t bytes;
    ZeroMemory(dst, sizeof(*dst));
    if (!src || !src->valid || !src->px || src->w <= 0 || src->h <= 0)
        return FALSE;
    bytes = (size_t)src->pitch * (size_t)src->h;
    dst->px = malloc(bytes);
    if (!dst->px)
        return FALSE;
    memcpy(dst->px, src->px, bytes);
    dst->w     = src->w;
    dst->h     = src->h;
    dst->pitch = src->pitch;
    lstrcpynA(dst->path, src->path, (int)sizeof(dst->path));
    dst->valid = TRUE;
    return TRUE;
}
```

### 4.2 `cmp_view_t`：統一座標模型

C# 的 V1 以左上角為基準、V2 以圖心為基準，原文件也警告兩者混用會偏移半個畫面。本版兩個視窗只用一種表示法：

- \(z\)：倍率（1 個影像像素等於 \(z\) 個螢幕像素）
- \((u, v)\)：**視口中心**對應的影像座標（浮點像素）

設視口寬高為 \(v_w, v_h\)，則換算公式為：

\[
s_x = \frac{v_w}{2} + (p_x - u)\,z, \qquad o_x = \frac{v_w}{2} - u\,z
\]

其中 \(o_x\) 為影像左上角的螢幕座標，\(y\) 軸同理。

| 操作 | 公式 | 對應 C# |
|---|---|---|
| fit | \(z=\lfloor 100\cdot m\cdot\min(v_w/W, v_h/H)\rfloor/100\)，\(u=W/2, v=H/2\) | `FitImageToPanel`（\(m=1\)）；V2 `Reset`（\(m=0.95\)） |
| 錨點縮放 | 游標相對視口中心的偏移 \(d=m_x - v_w/2\)；游標下的影像點 \(p=u+d/z\)；縮放後 \(u'=p-d/z'\) | 與 V1 左上角公式、V2 中心公式等價 |
| 平移 | \(u \mathrel{-}= \Delta x / z\) | `ImageLocation += Δ` |
| V1 邊界 | 若 \(Wz \le v_w\) 則 \(u=W/2\)；否則 \(u \in [\frac{v_w}{2z}, W-\frac{v_w}{2z}]\) | 即 C# 的 \(o_x \in [v_w - Wz, 0]\) |
| 視窗縮放 | 不需任何處理，視口中心看到的內容不變；V1 只需重新箝制 | C# 不處理 |
| V2 邊界 | 不箝制（與 C# 相同） | |

---

## 5. 共用數學與繪製（`compare_core.c`）

### 5.1 縮放與視圖

```c
/* File: compare_core.c */
#include <math.h>
#include "compare.h"

static double dmin(double a, double b) { return a < b ? a : b; }
static double dmax(double a, double b) { return a > b ? a : b; }

double CmpZoom_Quantize(double z)
{
    z = floor(z * 100.0 + 0.5) / 100.0;
    if (z < CMP_ZOOM_MIN) z = CMP_ZOOM_MIN;
    if (z > CMP_ZOOM_MAX) z = CMP_ZOOM_MAX;
    return z;
}

/* dir > 0 放大，dir < 0 縮小；回傳值等於 z 表示已達邊界 */
double CmpZoom_Step(double z, int dir)
{
    double n = CmpZoom_Quantize(dir > 0 ? z * CMP_ZOOM_STEP : z / CMP_ZOOM_STEP);
    if (fabs(n - z) < 1e-9)
        n = CmpZoom_Quantize(z + (dir > 0 ? 0.01 : -0.01));
    if ((z < 1.0 && n > 1.0) || (z > 1.0 && n < 1.0))
        n = 1.0;
    return n;
}

void CmpView_Fit(cmp_view_t *v, int W, int H, int vw, int vh, double margin)
{
    double z = 1.0;
    if (W > 0 && H > 0 && vw > 0 && vh > 0) {
        z = dmin((double)vw / W, (double)vh / H) * margin;
        z = floor(z * 100.0) / 100.0;
    }
    if (z < CMP_ZOOM_MIN) z = CMP_ZOOM_MIN;
    if (z > CMP_ZOOM_MAX) z = CMP_ZOOM_MAX;
    v->zoom = z;
    v->u = W * 0.5;
    v->v = H * 0.5;
}

/* mx, my：相對於視口左上角 */
void CmpView_ZoomAt(cmp_view_t *v, double znew, double mx, double my, int vw, int vh)
{
    double dx = mx - vw * 0.5;
    double dy = my - vh * 0.5;
    double px = v->u + dx / v->zoom;
    double py = v->v + dy / v->zoom;
    v->zoom = znew;
    v->u = px - dx / znew;
    v->v = py - dy / znew;
}

void CmpView_Pan(cmp_view_t *v, double dx, double dy)
{
    v->u -= dx / v->zoom;
    v->v -= dy / v->zoom;
}

static double clamp_axis(double u, int W, int vw, double z)
{
    double half = vw * 0.5 / z;
    if (W * z <= vw)
        return W * 0.5;
    if (u < half)
        return half;
    if (u > W - half)
        return W - half;
    return u;
}

void CmpView_ClampEdges(cmp_view_t *v, int W, int H, int vw, int vh)
{
    v->u = clamp_axis(v->u, W, vw, v->zoom);
    v->v = clamp_axis(v->v, H, vh, v->zoom);
}

void CmpView_ScreenToImage(const cmp_view_t *v, int vw, int vh,
                           double sx, double sy, double *ix, double *iy)
{
    *ix = v->u + (sx - vw * 0.5) / v->zoom;
    *iy = v->v + (sy - vh * 0.5) / v->zoom;
}
```

### 5.2 `Cmp_Blit`：可見子矩形＋金字塔

V1 每一格、V2 左右兩半都呼叫同一個函式。`vp` 是決定視口中心的視口矩形，`clip` 是實際可畫的區域：V1 兩者相同；V2 的 `vp` 為整個畫布，`clip` 為左半或右半。

選層規則與主程式一致：\(z<1\) 時選擇**仍大於等於顯示尺寸的最小一層**，使該層到螢幕的縮小倍率 \(l_z \in (0.5, 1]\)，以 `HALFTONE` 繪製；\(z \ge 1\) 時使用原圖，以 `COLORONCOLOR` 最近鄰繪製。來源矩形向外取整數像素，目的矩形依精確映射計算後可能超出 `clip`，由 `IntersectClipRect` 裁掉，因此放大到 800% 時邊緣像素仍然完整。

```c
/* File: compare_core.c */
/* 接續上段 */

void Cmp_Blit(HDC hdc, cmp_image_t *ci, const cmp_view_t *v,
              const RECT *vp, const RECT *clip)
{
    cmp_level_t lv;
    BITMAPINFO bi;
    double z = v->zoom, lz, ox, oy;
    double vx0, vy0, vx1, vy1;
    int k = 0, nlev, saved;
    int lx0, ly0, lx1, ly1, dx0, dy0, dx1, dy1;

    if (!ci || !ci->img.valid || z <= 0.0)
        return;

    ox = vp->left + (vp->right - vp->left) * 0.5 - v->u * z;
    oy = vp->top + (vp->bottom - vp->top) * 0.5 - v->v * z;

    vx0 = dmax((double)clip->left, ox);
    vy0 = dmax((double)clip->top, oy);
    vx1 = dmin((double)clip->right, ox + ci->img.w * z);
    vy1 = dmin((double)clip->bottom, oy + ci->img.h * z);
    if (vx1 <= vx0 || vy1 <= vy0)
        return;

    if (z < 1.0) {
        nlev = CmpImage_Levels(ci);
        while (k + 1 < nlev && z * (double)(1 << (k + 1)) <= 1.0)
            k++;
    }
    if (!CmpImage_Level(ci, k, &lv)) {
        if (!CmpImage_Level(ci, 0, &lv))
            return;
    }
    lz = z * (double)(1 << lv.shift);

    lx0 = (int)floor((vx0 - ox) / lz);
    ly0 = (int)floor((vy0 - oy) / lz);
    lx1 = (int)ceil((vx1 - ox) / lz);
    ly1 = (int)ceil((vy1 - oy) / lz);
    if (lx0 < 0) lx0 = 0;
    if (ly0 < 0) ly0 = 0;
    if (lx1 > lv.w) lx1 = lv.w;
    if (ly1 > lv.h) ly1 = lv.h;
    if (lx1 <= lx0 || ly1 <= ly0)
        return;

    dx0 = (int)floor(ox + lx0 * lz + 0.5);
    dy0 = (int)floor(oy + ly0 * lz + 0.5);
    dx1 = (int)floor(ox + lx1 * lz + 0.5);
    dy1 = (int)floor(oy + ly1 * lz + 0.5);
    if (dx1 <= dx0) dx1 = dx0 + 1;
    if (dy1 <= dy0) dy1 = dy0 + 1;

    saved = SaveDC(hdc);
    IntersectClipRect(hdc, clip->left, clip->top, clip->right, clip->bottom);
    if (lz < 1.0) {
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, NULL);
    } else {
        SetStretchBltMode(hdc, COLORONCOLOR);
    }

    /* 只把 ly0..ly1 列當成一張 top-down DIB，避開 top-down 時 ySrc 語意的歧義 */
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = lv.pitch / 4;
    bi.bmiHeader.biHeight      = -(ly1 - ly0);
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(hdc,
                  dx0, dy0, dx1 - dx0, dy1 - dy0,
                  lx0, 0, lx1 - lx0, ly1 - ly0,
                  lv.px + (size_t)ly0 * (size_t)lv.pitch,
                  &bi, DIB_RGB_COLORS, SRCCOPY);

    RestoreDC(hdc, saved);
}
```

說明：金字塔的奇數邊長會被截去，使右／下邊緣最多偏差 \(2^k\) 個影像像素，在顯示倍率 \(z \cdot 2^k \le 1\) 下不到 1 個螢幕像素，可忽略。若主程式 canvas 已有等價的繪製函式，可改為共用。

### 5.3 視窗登錄表與按鍵前置處理

```c
/* File: compare_core.c */
/* 接續上段 */

static HWND s_win[CMP_MAX_WINDOWS];
static int  s_nwin = 0;

BOOL CmpReg_Add(HWND hwnd)
{
    if (s_nwin >= CMP_MAX_WINDOWS)
        return FALSE;
    s_win[s_nwin++] = hwnd;
    return TRUE;
}

void CmpReg_Remove(HWND hwnd)
{
    int i;
    for (i = 0; i < s_nwin; i++) {
        if (s_win[i] == hwnd) {
            s_win[i] = s_win[--s_nwin];
            return;
        }
    }
}

/* 在主訊息迴圈 TranslateMessage 之前呼叫；回傳 TRUE 表示已處理 */
BOOL Compare_PreTranslate(MSG *msg)
{
    HWND root;
    int i;
    if (msg->message != WM_KEYDOWN || !msg->hwnd)
        return FALSE;
    root = GetAncestor(msg->hwnd, GA_ROOT);
    for (i = 0; i < s_nwin; i++) {
        if (s_win[i] == root)
            return SendMessageA(root, CMPM_KEY, msg->wParam, (LPARAM)msg->hwnd) != 0;
    }
    return FALSE;
}

void Compare_CloseAll(void)
{
    int guard = CMP_MAX_WINDOWS * 2;
    while (s_nwin > 0 && guard-- > 0)
        DestroyWindow(s_win[s_nwin - 1]);
}
```

只攔截 `WM_KEYDOWN`，`Alt+F4` 等系統按鍵維持預設行為。各視窗的 `CMPM_KEY` 處理函式可從 `lParam` 取得焦點控制項；若焦點在滑桿上且按鍵是方向鍵、`Home`／`End`／`PgUp`／`PgDn`，則回傳 0，讓滑桿自行處理。

---

## 6. V1 並排視窗（`compare_v1.c`）

### 6.1 視窗類別與版面

| 類別 | 樣式 | 用途 |
|---|---|---|
| `RoiCmpV1` | `WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN`，`WS_EX_ACCEPTFILES` | 頂層視窗：工具列＋grid |
| `RoiCmpGrid` | `WS_CHILD|WS_VISIBLE`，`CS_DBLCLKS`，`hbrBackground=NULL` | 自繪所有格子，雙緩衝 |

```
┌───────────────────────────────────────────────────────────────┐
│ [x] Lock   [V2 ▶]   2 images | Lock | paint 11.8 ms           │  工具列（高度約為字高×2.2）
├───────────────────────────────┬───────────────────────────────┤
│                           [X] │                           [X] │
│           格 0                │           格 1                │  1×2 / 3×1 / 2×2
│                               │                               │  格線 1px
│ a.jpg | 35% | 1472×1092 | 4208×3120  b.jpg | ...              │  狀態帶（字高＋6）
└───────────────────────────────┴───────────────────────────────┘
```

- 開啟時大小為游標所在螢幕 `rcWork` 的 80%，置中。
- `Report` 按鈕本版不建立。

### 6.2 狀態

```c
/* File: compare_v1.c */
typedef struct {
    cmp_image_t *ci;
    cmp_view_t   view;
    RECT         rc_cell;
    RECT         rc_img;
    RECT         rc_status;
    RECT         rc_close;
} cmp_cell_t;

typedef struct {
    HWND        hwnd;
    HWND        grid;
    HWND        chk_lock;
    HWND        btn_v2;
    HWND        lbl_msg;
    cmp_cell_t  cell[CMP_MAX_CELLS];
    int         n;
    BOOL        lock;
    BOOL        need_fit;
    int         pan_src;
    int         close_down;
    int         hover;
    POINT       last;
    int         wheel_acc;
    HDC         mem_dc;
    HBITMAP     mem_bmp;
    HBITMAP     mem_old;
    int         mem_w;
    int         mem_h;
    double      paint_ms;
} cmp_v1_t;
```

狀態在 `CompareV1_Open` 內以 `calloc` 配置，透過 `CreateWindowExA` 的 `lpParam` 傳入，於 `WM_NCCREATE` 存到 `GWLP_USERDATA`；grid 子視窗也用同一個指標。`pan_src=-1`、`close_down=-1` 表示沒有進行中的動作。

### 6.3 格子幾何（`v1_layout`）

- n=2：1 列 2 欄；n=3：3 列 1 欄（刻意不做 2＋1，保持每格同大小）；n=4：2×2。
- 每格：`rc_status` 在底部，高度為字高＋6；`rc_img` 為其餘區域；`rc_close` 為 `rc_img` 右上角內縮 4px 的 18×18 方塊。
- 格子之間保留 1px 格線。
- 在 `WM_SIZE` 中呼叫 `v1_layout`。若 `need_fit` 且尺寸不為零，執行 `FitAll` 並清除旗標；否則只對每格執行 `CmpView_ClampEdges`。

### 6.4 `FitAll` 與 Lock

- **Lock 開啟且 n>1**：對每格計算 fit 倍率，取**最小值**作為共同倍率，每格 `u=W/2, v=H/2` 後再箝制。
- 否則每格各自 `CmpView_Fit(..., 1.0)`。
- 觸發時機：首次尺寸確定、格數改變（加圖、關閉）、Lock 從關閉切到開啟（F11）、Lock 狀態下雙擊、按下 `0`。

### 6.5 滑鼠

| 事件 | 處理 |
|---|---|
| `WM_LBUTTONDOWN` | `SetFocus(grid)`。命中 `rc_close` 時記錄 `close_down=i`，否則命中 `rc_img` 時開始平移：`pan_src=i`、`last=pt`、`SetCapture` |
| `WM_RBUTTONDOWN` | 命中 `rc_img` 時開始平移（同上） |
| `WM_MOUSEMOVE` | 更新 `hover`。平移中：取 \(\Delta\)＝目前點減 `last`，Lock 時所有格 `CmpView_Pan`，否則只移 `pan_src`；逐格箝制後 `last=pt`，重繪 |
| `WM_LBUTTONUP` | 若 `close_down==i` 且仍在同一個 `rc_close` 內，移除第 i 格；結束平移並 `ReleaseCapture` |
| `WM_RBUTTONUP` | 結束平移 |
| `WM_CAPTURECHANGED` | `pan_src=-1`（Alt+Tab 等情況下的保險） |
| `WM_LBUTTONDBLCLK` | Lock 時 `FitAll`，否則只對該格 fit |
| `WM_MOUSEWHEEL` | 見 §6.6；頂層視窗收到時以 `SendMessageA` 轉給 grid |

所有座標都在同一個 grid 客戶區內，搭配 capture 後滑鼠移出格子甚至移出視窗，座標仍然一致，不需要 C# 的 `_panSourcePanel` 補救。

### 6.6 滾輪縮放

`wheel_acc += GET_WHEEL_DELTA_WPARAM(wParam)`，每滿 ±120 前進一步，餘數保留。滾輪訊息的座標為螢幕座標，先以 `ScreenToClient(grid)` 轉換，再找出 `rc_img` 包含該點的格子；找不到則忽略。

- **非 Lock**：`zn = CmpZoom_Step(z, dir)`，若不變則返回；否則 `CmpView_ZoomAt`，箝制後重繪該格。
- **Lock**：以第 0 格的倍率為基準（Lock 狀態下所有格倍率相同），並以「游標所指內容點佔影像寬高的比例」套用到每一格。即使各格影像尺寸不同，各格也會對齊到相同的相對位置，與 C# 的 `anchorFraction` 語意相同：

```c
/* File: compare_v1.c */
static void v1_wheel_lock(cmp_v1_t *s, int src, POINT pc, int dir)
{
    cmp_cell_t *c = &s->cell[src];
    double z0 = s->cell[0].view.zoom;
    double zn = CmpZoom_Step(z0, dir);
    int vw = c->rc_img.right - c->rc_img.left;
    int vh = c->rc_img.bottom - c->rc_img.top;
    double offx = pc.x - (c->rc_img.left + vw * 0.5);
    double offy = pc.y - (c->rc_img.top + vh * 0.5);
    double fx = (c->view.u + offx / c->view.zoom) / c->ci->img.w;
    double fy = (c->view.v + offy / c->view.zoom) / c->ci->img.h;
    int i;

    if (fabs(zn - z0) < 1e-9)
        return;
    for (i = 0; i < s->n; i++) {
        cmp_cell_t *d = &s->cell[i];
        int dw = d->rc_img.right - d->rc_img.left;
        int dh = d->rc_img.bottom - d->rc_img.top;
        d->view.zoom = zn;
        d->view.u = fx * d->ci->img.w - offx / zn;
        d->view.v = fy * d->ci->img.h - offy / zn;
        CmpView_ClampEdges(&d->view, d->ci->img.w, d->ci->img.h, dw, dh);
    }
    InvalidateRect(s->grid, NULL, FALSE);
}
```

### 6.7 繪製（grid 的 `WM_PAINT`）

1. 確保記憶體 DC 與客戶區同尺寸，尺寸改變時重建點陣圖。
2. 整片填入與主畫布相同的深灰底色。
3. 逐格：`Cmp_Blit(mem, ci, &view, &rc_img, &rc_img)`；狀態帶填黑、白字，以 `DrawTextA` 加 `DT_END_ELLIPSIS` 顯示 `名稱 | 35% | 顯示寬×高 | 原始寬×高`；關閉鈕為紅底白色 ×。
4. 以 1px 灰線繪製格線。
5. `BitBlt` 到畫面，並以 `QueryPerformanceCounter` 記錄 `paint_ms` 顯示在工具列訊息欄。
6. `WM_ERASEBKGND` 回傳 1。

### 6.8 加圖、移除與進入 V2

- **拖放**：`WM_DROPFILES` 中以 `DragQueryFileA` 取出路徑。只接受 `.png`／`.jpg`／`.jpeg`／`.bmp`（與主程式相同），以 `lstrcmpiA` 對 `ci->img.path` 去重。路徑含 `?` 視為非 ACP 檔名並略過（Windows 檔名不允許 `?`，出現代表 ANSI 轉換失敗）。載入期間顯示等待游標與「Loading i/n…」；超過 4 張時提示並忽略多餘的檔案。成功加入後 `need_fit=TRUE`，重新配置版面，最後 `DragFinish`。
- **移除**：`CmpImage_Unref`，後面的格子往前移。若剩下少於 2 張，`DestroyWindow`；否則重新配置版面並 `FitAll`。
- **`V2 ▶`**：n≥2 時啟用，呼叫 `CompareV2_Open(cell[0].ci, cell[1].ci)`。
- 任何按鈕的 `BN_CLICKED` 處理完後都 `SetFocus(grid)`，避免空白鍵重複觸發按鈕，並確保快捷鍵與滾輪可用。

### 6.9 按鍵（`CMPM_KEY`）

| 按鍵 | 動作 |
|---|---|
| `0` | Lock 時 `FitAll`，否則對 `hover` 格（沒有時為第 0 格）fit |
| `+`／`-` | 以格子中心為錨點縮放；Lock 時全部同步 |
| `L` | 切換 Lock（同步勾選框狀態） |
| `V` | 開啟 V2 |
| `Ctrl+W`／`Esc` | 關閉視窗 |

### 6.10 生命週期

- `WM_DESTROY`：所有格子 `CmpImage_Unref`，釋放記憶體 DC，`CmpReg_Remove`。
- `WM_NCDESTROY`：`free(state)`，清除 `GWLP_USERDATA`。

---

## 7. V2 分割線視窗（`compare_v2.c`）

### 7.1 視窗類別與版面

| 類別 | 樣式 | 用途 |
|---|---|---|
| `RoiCmpV2` | `WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN` | 頂層：控制列＋overlay＋狀態列 |
| `RoiCmpOverlay` | `WS_CHILD|WS_VISIBLE`，`CS_DBLCLKS`，`hbrBackground=NULL`，`hCursor=NULL` | 疊加畫布；游標由 `WM_SETCURSOR` 決定 |

```
┌ Zoom ───────────────────────┐┌ Pan & Sync ────────┐┌ Actions ──────────────────────────┐
│ L 35%  [──────●──────────]  ││ [ ] Sync pan       ││ [Swap] [Split: ON] [Reset All]    │
│ R 35%  [──────●──────────]  ││ (•) Left  ( ) Right││                                   │
└─────────────────────────────┘└────────────────────┘└───────────────────────────────────┘
┌──────────────────────────────────┃────────────────────────────────────┐
│ A: a.jpg                         ┃                           B: b.jpg │
│            左圖                  ┃              右圖                  │  黑底
│                                  ┃ ← 黃線 2px                         │
└──────────────────────────────────┃────────────────────────────────────┘
 L 35% | R 35% | split 50% | pan: Left(A) | sync off | paint 8.4 ms          狀態列
```

- 控制項（群組框、標籤、滑桿、勾選框、選項按鈕、按鈕）**全部是頂層視窗的直接子視窗**，群組框只是視覺外框，因此 `WM_COMMAND` 與 `WM_HSCROLL` 都直接送到 `RoiCmpV2`。
- 滑桿：`TRACKBAR_CLASSA`，`TBS_HORZ|TBS_AUTOTICKS`，範圍 2～800，`TBM_SETTICFREQ 50`、`TBM_SETLINESIZE 1`、`TBM_SETPAGESIZE 10`。
- `Split` 按鈕為 `BS_AUTOCHECKBOX|BS_PUSHLIKE`，以按下狀態表示 ON，文字同步為 `Split: ON`／`Split: OFF`（C# 以綠色表示；Win32 按鈕變色需要 owner-draw，本版不做）。
- `Left` 選項按鈕加上 `WS_GROUP`。
- 狀態列為 `STATUSCLASSNAMEA`（C# 版沒有，用於驗證效能）。畫布左上與右上角的 `A:`／`B:` 檔名標籤也是補充，用來確認 `Swap` 之後各邊是哪張圖。
- 開啟時大小為游標所在螢幕的 `rcWork`；`WM_GETMINMAXINFO` 限制最小寬度 760（控制列為比例式版面：Zoom 佔 1/2、Pan Sync 與 Actions 各佔 1/4，寬度隨視窗縮放）。

### 7.2 狀態

```c
/* File: compare_v2.c */
enum { V2_DRAG_NONE = 0, V2_DRAG_SPLIT, V2_DRAG_PAN };

typedef struct {
    HWND         hwnd;
    HWND         overlay;
    HWND         status;
    HWND         tb[2];
    HWND         lbl[2];
    HWND         chk_sync;
    HWND         rb_left;
    HWND         rb_right;
    HWND         btn_swap;
    HWND         chk_split;
    HWND         btn_reset;
    cmp_image_t *img[2];
    cmp_view_t   view[2];
    BOOL         swap;
    BOOL         split_mode;
    BOOL         sync_pan;
    int          pan_side;
    double       split_frac;
    BOOL         need_fit;
    int          drag;
    POINT        last;
    int          wheel_acc;
    HDC          mem_dc;
    HBITMAP      mem_bmp;
    HBITMAP      mem_old;
    int          mem_w;
    int          mem_h;
    double       paint_ms;
} cmp_v2_t;
```

**資料層與顯示層分離**：`img[]` 與 `view[]` 以「哪張底片」為索引（A=0、B=1），倍率與平移都跟著圖走。畫面位置則由以下運算決定：

\[
\text{idx}(\text{side}) = \text{side} \oplus \text{swap}
\]

- 畫面左側顯示 `img[idx(0)]`，右側顯示 `img[idx(1)]`。
- `tb[0]` 永遠代表畫面左側，控制 `view[idx(0)].zoom`。
- 平移目標 = `idx(pan_side)`（F9 的決定）。
- 因此 `Swap` 只需翻轉一個旗標，是零複製的純視圖操作。

### 7.3 滑鼠（overlay）

| 事件 | 處理 |
|---|---|
| `WM_LBUTTONDOWN` | `SetFocus`、`SetCapture`。`split_mode` 時 `drag=SPLIT`，分割線立即移到游標位置；否則 `drag=PAN`、`last=pt` |
| `WM_RBUTTONDOWN` | 一律 `drag=PAN`（與主程式一致） |
| `WM_MOUSEMOVE` | SPLIT：`split_frac = clamp(x / 寬, 0, 1)`；PAN：\(\Delta\)＝目前點減 `last`，`sync_pan` 時兩個 `view` 都 `CmpView_Pan`（各自除以自己的倍率，螢幕上移動量相同），否則只移 `view[idx(pan_side)]`。之後重繪並更新狀態列 |
| 左右鍵放開、`WM_CAPTURECHANGED` | `drag=NONE`，`ReleaseCapture` |
| `WM_SETCURSOR` | 客戶區內：拖曳中或平移模式為 `IDC_HAND`，移線模式為 `IDC_SIZEWE` |
| `WM_MOUSEWHEEL` | 見 §7.4；頂層視窗收到時轉給 overlay |
| `WM_LBUTTONDBLCLK` | 定稿 Q5：只重設兩個 `view`（fit 0.95＋置中），不動 `swap`／`split_mode`／`sync_pan`／`pan_side`／`split_frac`（C10） |

### 7.4 縮放

- **滾輪**（雙圖同步，F10）：以畫面左側倍率 \(z_L\) 為基準，`zn = CmpZoom_Step(z_L, dir)`；若不變則返回。兩個 `view` 都執行 `CmpView_ZoomAt(view, zn, mx, my, 寬, 高)`，視口一律使用整個 overlay。每張圖以**自己的舊倍率**計算錨點，所以即使原本倍率不同，游標下的內容點也都保持不動。之後呼叫 `v2_sync_sliders`。
- **滑桿**（`WM_HSCROLL`，`lParam==tb[side]`）：`view[idx(side)].zoom = CmpZoom_Quantize(TBM_GETPOS / 100.0)`，`u`、`v` 不變（以視口中心為錨點），更新標籤並重繪。
- **`v2_sync_sliders`**：對兩側執行 `TBM_SETPOS(TRUE, round(zoom×100))` 並更新標籤。`TBM_SETPOS` 不會產生 `WM_HSCROLL`，不會形成回授迴圈。
- 畫布視口只有一個，兩張圖在 V2 中都不做邊界箝制，只要按 `Reset All` 就能回到初始狀態。

### 7.5 `Swap`／`Reset All`／初始化

| 動作 | 步驟 |
|---|---|
| `Swap` | `swap = !swap` → 更新標題 `Compare — L: a.jpg | R: b.jpg` → `v2_sync_sliders` → 更新狀態列 → 重繪。平移目標由 `idx(pan_side)` 自動更新 |
| 選項按鈕 | `pan_side = BST_CHECKED(rb_right) ? 1 : 0` |
| `Sync pan` | 讀取勾選狀態寫入 `sync_pan` |
| `Split` | 讀取按鈕狀態寫入 `split_mode`，更新按鈕文字 |
| `Reset All` | 兩個 `view` 以 `CmpView_Fit(..., 0.95)` 重設（視口為整個 overlay）；`split_frac=0.5`；`swap=FALSE`；`split_mode=TRUE`（同步按鈕）；`sync_pan` 與 `pan_side` 維持不變；最後同步滑桿、標題、狀態列並重繪 |
| 初始化 | 首次收到非零尺寸 `WM_SIZE` 時執行與 `Reset All` 相同的 fit，但不改變 `swap` 與 `split_mode`（初值分別為 FALSE 與 TRUE） |
| 視窗縮放 | 只重新配置控制項並重繪；分割線依比例自動跟隨，內容以視口中心為錨點保持不動（F6） |

所有 `BN_CLICKED` 處理完後都 `SetFocus(overlay)`。

### 7.6 繪製（overlay 的 `WM_PAINT`）

1. 確保記憶體 DC 尺寸正確，整片填黑。
2. 計算 `sx = round(split_frac × 寬)` 並箝制在 \([0, 寬]\)；`vp` 為整個客戶區。
3. `Cmp_Blit(mem, img[idx(0)], &view[idx(0)], &vp, &{0, 0, sx, 高})`。
4. `Cmp_Blit(mem, img[idx(1)], &view[idx(1)], &vp, &{sx, 0, 寬, 高})`。
5. `Cmp_Blit` 內部已在每次呼叫後 `RestoreDC`，接著以黃色 `PatBlt` 畫出 \([sx-1, sx+1)\) 的 2px 分割線，確保線不會被裁掉一半。
6. 在左上與右上角繪製 `A: 檔名`／`B: 檔名` 標籤（黑底白字）。
7. `BitBlt` 到畫面並記錄 `paint_ms`。

兩次 `Cmp_Blit` 都只處理各自裁切區內的可見子矩形，總取樣量約等於一張全畫布的量，不會把兩張圖各自整張縮放一次。

### 7.7 按鍵（`CMPM_KEY`）

| 按鍵 | 動作 |
|---|---|
| `S` | Swap |
| `M` | 切換移線模式 |
| `P` | 切換同步平移 |
| `[`／`]` | 平移目標切到左／右（同步選項按鈕） |
| `+`／`-` | 以畫布中心為錨點，雙圖同步縮放 |
| `0` | Reset All |
| `Ctrl+W`／`Esc` | 關閉視窗 |

焦點在滑桿上時，方向鍵與 `Home`／`End`／`PgUp`／`PgDn` 交給滑桿處理（回傳 0）。

### 7.8 生命週期

- `WM_DESTROY`：`CmpImage_Unref(img[0])`、`CmpImage_Unref(img[1])`，釋放記憶體 DC，`CmpReg_Remove`。
- `WM_NCDESTROY`：`free(state)`。

---

## 8. 主程式整合（`main.c`）

### 8.1 選單與按鍵

| 位置 | 項目 | 快捷鍵 | 命令 |
|---|---|---|---|
| `View` 選單末端（先加分隔線） | `Compare Files...` | `Ctrl+K` | `IDM_COMPARE_FILES` |
| 同上 | `Compare Current with Next` | `K` | `IDM_COMPARE_NEXT` |

- 兩個命令 ID 與既有 ID 已核對（155／156 無衝突；直方圖 152～154 移至 161～166）。
- 快捷鍵以主程式 `accelerators[]` 註冊（`Ctrl+K`／`K`），僅在 `root == hwnd_main` 時處理（C6）。
- 主程式的「所有 `WM_COMMAND` 前先 `Flush`」規則在這裡是期望的行為：開啟比較視窗前，長按瀏覽延後的分析會先完成。
- `Compare_Init(hInstance, ui_font)` 在 WinMain 中與其他視窗類別一起註冊並初始化。

**實作掛勾總表（v2.7）**

| 項目 | 內容 | 狀態 |
|---|---|---|
| `IDM_COMPARE_FILES 155`／`IDM_COMPARE_NEXT 156` | 定義於 `app.h`；加速鍵 `Ctrl+K`／`K`；直方圖 ID 同步移至 161～166 | 已實作（C3） |
| `Compare_Init(hInstance, ui_font)` | WinMain 呼叫；字型 NULL 時用 `DEFAULT_GUI_FONT` | 已實作（C1） |
| `Compare_CloseAll()` 先於主視窗清理 | `WM_DESTROY` | 已實作（§8.5） |
| 訊息迴圈 `root == hwnd_main` 包覆 | 導覽＋repeat 丟棄＋加速鍵 | 已實作（C4／C6） |
| `CMP_MAX_LIVE_IMG=8`＋`Compare_CanOpen` 預檢 | 各開啟入口 | 已實作（C7／Q11） |
| V1 拖放 `DragQueryFileW`＋嚴格 ACP | `WC_NO_BEST_FIT_CHARS`＋`lpUsedDefaultChar` | 已實作（C8） |
| V2 讀值欄、雙擊重設 | `v2_update_status`／`WM_LBUTTONDBLCLK` | 已實作（C9／Q5／C10） |
| `Report`（P5） | 未實作，設計保留 | 延後 |

### 8.2 `Compare Files…`（開 V1）

1. 若 `CmpImage_LiveCount()` 已達 `CMP_MAX_LIVE_IMG`，提示後返回。
2. `GetOpenFileNameA` 搭配 `OFN_ALLOWMULTISELECT|OFN_EXPLORER|OFN_FILEMUSTEXIST`，緩衝區 32KB，篩選條件與主程式開檔相同，初始目錄為目前影像所在資料夾。遇到 `FNERR_BUFFERTOOSMALL` 時提示選取過多。
3. 若有目前影像，`imgs[0] = CmpImage_FromImage(&g_app.img)`（記憶體複製約 10～20ms，不重新解碼，也不重新觸發雲端下載）。
4. 解析多選結果（見下方程式碼），略過與目前影像路徑相同者（`lstrcmpiA`）及含 `?` 者，依序 `CmpImage_Load` 直到總數達 4 張。載入期間使用等待游標，失敗時分別計算「解碼失敗」與「非 ACP」數量。
5. 總數少於 2 張時提示並中止；否則 `CompareV1_Open(imgs, n)`。
6. 無論成功與否，最後都對 `imgs[]` 全部 `CmpImage_Unref`。

```c
/* File: main.c */
/* 解析 OFN_ALLOWMULTISELECT|OFN_EXPLORER 的結果；回傳路徑數 */
static int parse_multisel(const char *buf, char out[][MAX_PATH], int maxn)
{
    const char *dir = buf;
    const char *p = buf + lstrlenA(buf) + 1;
    int dirlen = lstrlenA(dir);
    const char *sep = (dirlen > 0 && dir[dirlen - 1] == '\\') ? "" : "\\";
    int n = 0;

    if (maxn <= 0 || !buf[0])
        return 0;
    if (*p == '\0') {
        lstrcpynA(out[0], dir, MAX_PATH);
        return 1;
    }
    while (*p && n < maxn) {
        int r = snprintf(out[n], MAX_PATH, "%s%s%s", dir, sep, p);
        if (r > 0 && r < MAX_PATH)
            n++;
        p += lstrlenA(p) + 1;
    }
    return n;
}
```

需要 `#include <stdio.h>`。這裡使用 `snprintf` 的回傳值判斷是否截斷，因此在 `-Wall` 下不會觸發 `-Wformat-truncation` 警告。

### 8.3 `Compare Current with Next`（開 V2）

1. 前置檢查：有影像、非拖曳中、未達影像數上限。
2. `Refresh` 檔案清單並 `Find` 目前影像（沿用導覽的定位規則）。目標為 `idx+1`；若目前已是最後一張則改用 `idx-1`；清單只有 1 張時 Beep 並提示。
3. `a = CmpImage_FromImage(&g_app.img)`；`b = CmpImage_Load(Path(target))`。若目標為雲端佔位檔，先在狀態列顯示「下載中…」並 `UpdateWindow`（與導覽相同）。`b` 載入失敗時提示並 `Unref(a)`。
4. `CompareV2_Open(a, b)`，然後 `Unref(a)`、`Unref(b)`。

### 8.4 訊息迴圈（定稿 C4＋C6，已實作）

```c
/* File: main.c */
while (GetMessageA(&msg, NULL, 0, 0) > 0) {
    HWND root;
    if (Compare_PreTranslate(&msg))
        continue;
    root = msg.hwnd ? GetAncestor(msg.hwnd, GA_ROOT) : NULL;
    if (root == g_app.hwnd_main) {
        /* 方向鍵導覽過濾、長按 repeat 以 msg.time 丟棄、加速鍵 */
        if (TranslateAcceleratorA(g_app.hwnd_main, g_accelerators, &msg))
            continue;
    }
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
}
```

比較視窗沒有 owner，`GA_ROOT` 會回傳比較視窗本身，不會等於主視窗，故方向鍵導覽天然免疫（C4）。加速鍵必須包在 `root == hwnd_main` 內，否則比較視窗的按鍵會觸發主視窗命令（C6，詳見 §13）。

### 8.5 關閉順序

主視窗 `WM_DESTROY`：**先** `Compare_CloseAll()`，再執行既有清理，最後 `PostQuitMessage`。因為比較視窗沒有 owner，不會隨主視窗自動銷毀。Debug 建置可在結束前檢查 `CmpImage_LiveCount()==0`，用於偵測參考計數洩漏。

### 8.6 與主程式狀態的互動

| 情境 | 結果 |
|---|---|
| 比較視窗開著時主視窗導覽到下一張 | 比較視窗持有的是複製品，不受影響 |
| 主視窗失去啟用而觸發長按 `Flush` | 開啟比較視窗時會觸發一次，行為與既有規則一致 |
| COM／WIC | 比較視窗與主程式在同一個 STA 執行緒，`Image_Load` 可直接使用 |
| 字型 | 控制項送出 `WM_SETFONT`，沿用主程式 UI 字型；若主程式沒有提供，使用 `DEFAULT_GUI_FONT` |

---

## 9. 記憶體與效能預算

### 9.1 記憶體（13MP，4208×3120）

| 項目 | 大小 |
|---|---|
| 單張 BGRA | \\(4208 \\times 3120 \\times 4 \\approx 52.5\\) MB |
| 金字塔（+33%） | 約 17.5 MB |
| 每個 `cmp_image_t` | 約 70 MB |
| V1 4 張 | 約 280 MB |
| 從 V1 開 V2 | **+0**（共享） |
| `Compare Current with Next` | 約 140 MB |
| `CMP_MAX_LIVE_IMG=8` 上限（定稿 Q11） | 約 630MB（主程式 70MB＋8×70MB）；達上限時拒絕開啟新比較 |

### 9.2 時間（估計值，須實測）

| 項目 | 目標／估計 |
|---|---|
| 目前影像複製 | 10～20 ms |
| 每張 WIC 解碼 | 約 91 ms（JPEG）、約 226 ms（PNG） |
| 開啟 V1 4 張 JPEG | 約 300 ms（目前影像複製＋3 張解碼），同步執行並顯示等待游標 |
| 首次繪製 | 需加上每張的金字塔建置時間，須實測 |
| V1 4 格繪製（金字塔已建立） | ≤ 20 ms |
| V2 繪製 | ≤ 15 ms（拖曳滑桿或分割線時可達 60fps） |
| 800% 放大繪製 | 只處理可見子矩形，與影像大小無關 |

---

## 10. 實作階段與驗收

| 階段 | 內容 | 驗收條件 |
|---|---|---|
| P1 | `compare.h`、`compare_image.c`、`compare_core.c`、`Image_Clone`；V1 固定 2 格：繪製、fit、滾輪、左右鍵拖曳、雙擊 | fit 不溢出；800% 時像素邊界清楚且與主畫布一致；拖曳到最邊緣不露出底色；工具列顯示繪製時間 |
| P2 | V1 3／4 格版面、Lock（共同倍率、比例錨點、同步平移）、關閉鈕、拖放、狀態帶、按鍵；主選單 `Compare Files…` | 不同解析度的圖在 Lock 下縮放後對齊相同相對位置；關閉到剩 1 張時視窗自動關閉 |
| P3 | V2 畫布：分區裁切、分割線、移線／平移模式、同步平移、平移目標、滾輪 | 分割線移到兩端時不殘留畫面；黃線完整 2px；左右倍率不同時滾輪錨點仍正確 |
| P4 | V2 控制列：滑桿、`Swap`、`Split`、`Reset All`、狀態列、按鍵；`V2 ▶`；`Compare Current with Next` | `Swap` 後滑桿、標題、平移目標三者一致；拖曳滑桿時不發生回授 |
| P5（選用） | `Report`：以 \((u,v,z)\) 反推可見區域 \(x_0 = u - \frac{v_w}{2z}\)，寬 \(= v_w / z\)，並箝制在影像範圍內；以 `CreateThread` 在背景直接分析記憶體緩衝（例如 Y 通道 3×3 拉普拉斯的變異數），分析期間持有參考計數，結果以 `PostMessage` 送回 UI 執行緒並輸出 HTML | 分析期間關閉視窗也不會崩潰；UI 不凍結 |

---

## 11. 測試清單

1. **縮放邊界**：分別從 0.02、0.10、0.15、1.00、8.00 連續滾動，確認不會卡住（F1），且跨越 100% 時會停在 100%。
2. **fit**：3×1 版面搭配極寬影像（例如 8000×500），以及小於格子的影像，確認不溢出也不過度放大（上限 8）。
3. **參考計數**：開 V1 → 開 V2 → 關閉 V1 → 在 V2 中繼續縮放與平移 → 關閉 V2。確認沒有崩潰，且 `LiveCount` 回到 0。
4. **記憶體**：反覆開關 V1（4 張）20 次，工作管理員中的私人位元組應回到基準值。
5. **主視窗互動**：比較視窗開啟時在主視窗長按 `←`／`→`，比較內容不變；在比較視窗中按方向鍵，主視窗不會導覽。
6. **焦點**：點擊 V2 的 `Swap` 後按空白鍵，不會再次觸發；拖曳滑桿後按 `S`，仍會執行 Swap。
7. **`Swap` 一致性**：選擇平移目標 Left → Swap → 拖曳，確認移動的是畫面左側那張圖（F9）。
8. **視窗縮放**：將 V2 從全螢幕縮到最小寬度，分割線維持相同比例，畫面中心內容不變。
9. **多螢幕**：游標位於副螢幕時開啟，視窗應出現在副螢幕且不超出 `rcWork`。
10. **檔名與來源**：非 ACP 檔名被略過並正確計數；雲端佔位檔顯示下載提示；混合選取 PNG／JPG／BMP 均可開啟。
11. **DPI**：在 150% 系統縮放下以 100% 顯示棋盤格測試圖，記錄並非像素對像素，作為已知限制（定稿 Q4）。
12. **觸控板**：高解析度滾動時縮放平順，不會一次跳好幾階。
13. **關閉順序**：同時開著 V1 與 V2 時直接關閉主視窗，行程正常結束，Debug 建置沒有洩漏警告。

---

## 12. 待確認

| # | 項目 | 本版採用 | 替代方案 |
|---|---|---|---|
| Q1 | 進入點 | `Ctrl+K` 開檔對話框（自動含目前影像）；`K` 目前影像 vs 下一張（V2），最後一張時對上一張；V1 內 `V2 ▶`。`Shift+K` 不做 | 比較籃 |
| Q2 | `Report` | 不納入（P5 保留設計） | 改為輸出可見區域的 RGB／Y 統計（可重用 `AnalyzeROI`） |
| Q3 | 縮放規格 | ×1.15 與 ÷1.15、量化 1%、0.02～8、吸附 1.0 | 完全照 C#（須接受 F1 的卡死問題） |
| Q4 | DPI 感知 | 沿用主程式 manifest 現況（**不宣告** `dpiAware`）；150% 等非 100% 系統縮放下，比較視窗與主畫布一樣經系統點陣放大，100% 不是真正的像素對像素（已知限制） | 在 manifest 宣告 `dpiAware`（影響主程式所有版面） |
| Q5 | V2 雙擊 | **做**：只重設兩個 `view`（fit 0.95＋置中），不動 `swap`／`split_mode`／`sync_pan`／`pan_side`／`split_frac`（C10） | 不處理 |
| Q6 | `Esc` 關閉比較視窗 | **是** | 只保留 `Ctrl+W` |
| Q7 | 可接受的格式 | PNG／JPG／BMP（與主程式相同） | 加入 GIF／TIFF（主程式需同步） |
| Q8 | V2 像素讀值 | **做**（C9）：狀態列顯示游標下兩張圖各自的影像座標與 RGB | 不做 |
| Q9 | V2 單側滾輪 | 延後（不做） | `Shift+滾輪` 只縮放平移目標那一側 |
| Q10 | 外部介面核對 | 已完成，依 C1～C3 對齊實際 `image.h`／`view.h`／`app.h` | — |
| Q11 | 影像上限 | `CMP_MAX_LIVE_IMG=8`，改為**開啟前預先檢查**（C7）。峰值約主程式 70MB＋8×70MB ≈ 630MB | 沿用 16（約 1.1GB） |

---

## 13. 定稿附錄（C1～C10，取代對應段落）

### C1：`compare.h` 不引用 `app.h`

`compare.h` 只 include `image.h`＋`view.h`；`App` 層 ID 定義留在 `app.h`。比較模組不需 `hwnd_main`（視窗無 owner，關閉由 `main.c` 呼叫 `Compare_CloseAll()`），唯一從主程式取得的是 UI 字型。

```c
/* File: compare.h */
#ifndef COMPARE_H
#define COMPARE_H

#include <windows.h>
#include "image.h"
#include "view.h"

#define CMP_ZOOM_MIN      0.02
#define CMP_ZOOM_MAX      8.0
#define CMP_ZOOM_STEP     1.15
#define CMP_MAX_CELLS     4
#define CMP_MAX_WINDOWS   8
#define CMP_MAX_LIVE_IMG  8

#define CMPM_KEY          (WM_APP + 0x40)   /* 已確認主程式未使用 */

BOOL Compare_Init(HINSTANCE hinst, HFONT ui_font);   /* 實作於 compare_v1.c */
BOOL Compare_CanOpen(int need);
#endif
```

`Compare_Init` 以 `static HFONT` 保存字型，NULL 時使用 `DEFAULT_GUI_FONT`；`main.c` 的 WinMain 註冊類別後呼叫一次。

### C2：金字塔轉接層對齊 `view_level_t`

`view.c` 的實際配置：`ViewPyr_Build` 回傳 `BOOL`，`levels[0]` 為**原圖參照**（`owned=FALSE`，`px/w/h/pitch` 直接指向 `img`），`levels[1..]` 才是 2×2 平均縮小層，`count` 含原圖層。因此 `pyr_has_full()` 走 TRUE 分支，`CmpImage_Level` 的 `idx = k`。為相容另一種配置仍保留尺寸判斷：

```c
/* File: compare_image.c */
static BOOL pyr_has_full(const cmp_image_t *ci)
{
    return ci->pyr.count > 0 &&
           ci->pyr.levels[0].w == ci->img.w &&
           ci->pyr.levels[0].h == ci->img.h;
}

int CmpImage_Levels(cmp_image_t *ci)
{
    pyr_build(ci);
    if (!ci->pyr_built)
        return 1;
    return pyr_has_full(ci) ? ci->pyr.count : 1 + ci->pyr.count;
}

/* k=0 為原圖，k 層相對原圖縮小 2^k 倍 */
BOOL CmpImage_Level(cmp_image_t *ci, int k, cmp_level_t *out)
{
    const view_level_t *lv;
    int idx;
    if (k <= 0) { /* 原圖 */ }
    if (!ci->pyr_built) return FALSE;
    idx = pyr_has_full(ci) ? k : k - 1;
    if (idx < 0 || idx >= ci->pyr.count) return FALSE;
    lv = &ci->pyr.levels[idx];
    /* out->px/w/h/pitch = lv->...；out->shift = k */
}
```

`CmpImage_Unref` 維持「先 `ViewPyr_Free` 再 `Image_Free`」；`cmp_image_t` 一律以指標傳遞，不做結構複製。

### C3：選單 ID 與加速鍵

```c
/* File: app.h —— 接在既有 IDM_*（101～154）之後 */
#define IDM_COMPARE_FILES 155
#define IDM_COMPARE_NEXT  156

/* File: main.c —— 追加到 accelerators[] */
{ FVIRTKEY | FCONTROL, 'K', IDM_COMPARE_FILES },
{ FVIRTKEY,            'K', IDM_COMPARE_NEXT  },
```

實作時另將直方圖命令 ID 由 152～154 移至 **161～166**（`IDM_HIST_RGB` 161、`_Y` 162、`_R` 163、`_G` 164、`_B` 165、`_LOG` 166），避免與 155／156 相鄰混淆；`CheckMenuRadioItem`、`accelerators[]` 與 View 選單已同步更新。

### C6：主加速鍵必須限定在主視窗（實作新增）

**問題**：`TranslateAcceleratorA(hwnd, ...)` **不檢查** `msg.hwnd` 是否屬於該視窗——只要按鍵符合表格就把 `WM_COMMAND` 送給主視窗。比較視窗的 `CMPM_KEY` 對未處理按鍵回傳 0、`Compare_PreTranslate` 回傳 FALSE，訊息會繼續走到主加速鍵表，導致在比較視窗按 `H`／`A`／`Y`／`R`／`G`／`B`／`L`／`C`／`O`／`Ctrl+E` 操作到主視窗。過去只有模態對話框（自有訊息迴圈）故未出現，加入非模態比較視窗後才產生。

**修正**（`main.c`，已實作於 1591～1612 行）：

```c
while (GetMessageA(&msg, NULL, 0, 0) > 0) {
    HWND root;
    if (Compare_PreTranslate(&msg))
        continue;
    root = msg.hwnd ? GetAncestor(msg.hwnd, GA_ROOT) : NULL;
    if (root == g_app.hwnd_main) {
        /* 方向鍵導覽過濾、長按 repeat 以 msg.time 丟棄、加速鍵 */
        if (TranslateAcceleratorA(g_app.hwnd_main, g_accelerators, &msg))
            continue;
    }
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
}
```

此修正同時涵蓋 `WM_SYSKEYDOWN`（`Alt` 組合鍵），比只在 `Compare_PreTranslate` 吞掉 `WM_KEYDOWN` 更完整。

### C4：方向鍵過濾與長按 repeat

`App_NavKeyAllowed` 已有 `GetAncestor(msg.hwnd, GA_ROOT) == g_app.hwnd_main` 檢查，比較視窗（無 owner，`GA_ROOT` 為自身）天然免疫。長按 repeat 以 `msg.time` 丟棄的邏輯**已一併置於 `root == hwnd_main` 條件內**。

### C7：影像上限改為預先檢查

```c
BOOL Compare_CanOpen(int need) { return CmpImage_LiveCount() + need <= CMP_MAX_LIVE_IMG; }
```

| 呼叫點 | `need` |
|---|---|
| `Compare Files…` | 目前影像（1）＋選取檔案數，上限合計 4 |
| `Compare Current with Next` | 2 |
| V1 拖放加圖 | 實際要加入的張數 |
| V1 `V2 ▶` | 0（共享，不檢查） |

超出時提示「比較視窗影像已達上限（8），請先關閉部分比較視窗」。

### C8：非 ACP 檔名與拖放

`DragQueryFileA` 的 `?` 判斷不可靠（A 版允許 best-fit，會產生看似合法卻指向別處的路徑）。V1 拖放改用 `DragQueryFileW`，再以與 `filelist` 相同的嚴格轉換：

```c
static BOOL wide_to_acp_strict(const WCHAR *w, char *out, int cap)
{
    BOOL used = FALSE;
    int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
                                w, -1, out, cap, NULL, &used);
    return n > 0 && !used;
}
```

`Compare Files…` 的開檔對話框與主程式 `O` 開檔使用同一種 API（`GetOpenFileNameA`），確保兩個入口對同一檔名得到相同結果。

### C9：V2 像素讀值規格

- 狀態列（`v2_update_status`）先輸出基本欄：`L z% | R z% | split x% | pan Left/Right(A/B) | sync on/off`，再附上讀值欄 `| L (x,y) RGB(r,g,b) | R (x,y) RGB(r,g,b)`。
- 兩張圖**都顯示**，即使其中一張在分割線另一側看不到——這正是比對用途。
- 座標：各自以自己的 `view` 與整個 overlay 視口計算 `CmpView_ScreenToImage`，取 `floor`；超出影像範圍顯示 `--`。
- 一律讀 `ci->img.px`（原圖），不讀金字塔。BGRA 順序：`px[0]=B`、`px[1]=G`、`px[2]=R`。
- 更新時機：`WM_MOUSEMOVE`（拖曳中也更新）；離開客戶區由 `have_pointer` 清除並回到不含讀值的基本欄。

### C10：V2 雙擊的副作用

`WM_LBUTTONDBLCLK` 只呼叫 `v2_fit_views`（fit 0.95＋置中），不動 `swap`／`split_mode`／`sync_pan`／`pan_side`／`split_frac`。移線模式下雙擊的第一下 `WM_LBUTTONDOWN` 已把分割線移到游標位置，因此雙擊後分割線會停在點擊處，屬預期行為（已於程式註解說明，不另做補償）。
---

## 14. v2.7 增補：主視窗多檔拖放＋Snapshot（已實作）

沿用 v2.7 定稿與 C1～C10；拍板 S1～S7、T1～T3；新增 C11。驗收以 ad-hoc 檢查為準（25/27 通過，2 項為檢查字串與實作等價做法的差異：剪貼簿用 `CF_BITMAP` 而非 `CF_DIB`，WIC 用 `WICBitmapIgnoreAlpha` 而非逐像素補 0xFF）。

### 14.1 主視窗多檔拖放（S1＋S2＋S3、T2、T3）

- 分流（`main.c` `App_OnDropFiles`）：0 張→狀態列提示各類略過數；1 張（不按 `Ctrl`）→既有單檔載入；≥2 張（不按 `Ctrl`）→主視窗以既有完整流程載入自然排序第一張（清除 ROI、切換檔案清單；若第一張即目前影像則不重載、ROI 保留），V1 開啟全部拖入檔案（T3 目前：先載主視窗再開 V1）；按住 `Ctrl` 放開滑鼠＋有目前影像→`CMP_OPEN_WITH_CURRENT`（V1 含目前影像，主視窗不變；1 張也開 V1）。
- `Ctrl` 用 `GetAsyncKeyState` 判斷（拖放時前景是檔案總管，`GetKeyState` 不準）；上限檢查在主視窗載入前進行（上限到達仍載入第一張＋提示）；T2 替代：不按 `Ctrl` 拖入多張後訊息附註「Ctrl+drop to include current image」。
- 共用收集 `CmpDrop_Collect`（`compare_core.c`，`Ctrl+K`、主視窗拖放、V1 拖放三入口共用；`wide_to_acp_strict` 由 V1 移至此處並加 `GetACP()==CP_UTF8` 分支）：W 版取路徑→略過目錄→副檔名過濾（png/jpg/jpeg/bmp）→自然排序（`StrCmpLogicalW`）→去重→嚴格 ACP→取前 4，統計 `cmp_drop_stats_t`（total/dirs/bad_ext/non_acp/dup/over/truncated）。`GetFileAttributesW` 不觸發雲端下載，真正下載在 `CmpImage_Load`（狀態列 `Loading i/n…`）。S1 不重複解碼：V1 第一格以 `CmpImage_FromImage` 從主視窗複製。退回：只有 1 張成功時該張顯示在主視窗。

### 14.2 Snapshot（S4～S7、T1）

- 繪製重構：V1／V2 的 `WM_PAINT` 拆為 `v1_render(s,dc,w,h,flags)`／`v2_render(…)`＋貼上畫面；Snapshot 呼叫同一渲染函式（`CMP_RENDER_SNAPSHOT` 不畫關閉鈕與 hover），僅限參數 `w×h` 範圍填底色，不可用 `GetClientRect`（否則蓋掉資訊列區）。V1 Snapshot 含格線＋每格狀態帶（加格號 `[1]`～`[4]`，與資訊列對應），V2 含分割線＋A/B 標籤（含倍率如 `A: a.jpg 35%`）；像素讀值（狀態列）不入圖。
- `compare_snap.c`（492 行）：`CmpSnap_Begin`（`CreateCompatibleBitmap` 相容點陣）、`CmpSnap_Finalize`（僅 deselect＋DeleteDC）、`CmpSnap_End`、`snap_encode`（WIC `CreateBitmapFromHBITMAP`＋`WICBitmapIgnoreAlpha`＋PNG 編碼；實作等價於逐像素補 alpha，輸出不透明）、`CmpSnap_CopyToClipboard`（`CopyImage(LR_CREATEDIBSECTION)`＋`CF_BITMAP`，`OpenClipboard` 重試 5 次；小畫家／Word 可貼，實作等價於 `CF_DIB`）、`CmpSnap_MakePath`／`CmpSnap_SavePng`／`CmpSnap_Deliver`／`cmp_save_as_dialog`、`CmpInfo_Add/Height/Draw`、`CmpView_VisibleRect` 在 `compare_core.c`、`Cmp_UiFont`。
- S4 目前：位置＝影像所在資料夾（`snap_ref_directory`：`PathRemoveFileSpecA`＋目錄屬性驗證；無效時 fallback 既有 Pictures 邏輯），V1 取第 0 格、V2 取畫面左側；檔名 `snap_<A>_vs_<B>_yyyymmdd-hhmmss[_k].png`（A/B 主檔名以 `CharNextA` 逐字截 32 位元組，DBCS 安全；`_k` 防覆寫，沿用既有 suffix 迴圈語意）。
- S5 目前：畫布客戶區 1:1；S6 替代（資訊列，不寫 `tEXt`——`tEXt` 僅 Latin-1，中文 ANSI 會違規）：底部資訊列（字高＋4／行＋8 邊距），只有輸出圖有；V1 約 5 行（標題：程式名｜時間｜Lock｜張數｜畫布尺寸；每格 `[i] 倍率｜src 可見區｜原尺寸｜路徑`）、V2 約 3 行（split／swap／sync／pan target；`L = A`／`R = B` 兩行）；`src` 可見區由 `CmpView_VisibleRect`（與 `Cmp_Blit` 同算法）得出，可對應主視窗 ROI；路徑放行尾以 `DT_PATH_ELLIPSIS` 省略中段；尺寸用 ASCII `x`；時間與檔名同一 `SYSTEMTIME`。T1 替代：V1／V2 工具列 `Info bar` 核取方塊（預設勾選；關閉時 Snapshot 尺寸＝畫布尺寸）。
- S7 替代：按鈕＋`Ctrl+S` 存檔的同時複製（先複製後存檔；對話框取消剪貼簿仍有圖）；`Ctrl+Shift+S` 另存＋複製；`Ctrl+C` 只複製。成功訊息寫既有訊息欄（V1 `state->message`、V2 狀態列：`Saved <檔名> + clipboard`），不彈成功窗；存檔與複製都失敗才 MessageBox。`CMPM_KEY` 開頭取 `ctrl/shift`，單鍵 `S/M/P/L/V/0/+/-` 只在無 `Ctrl` 時生效（C6 保證不落到主視窗）。

### 14.3 C11：DBCS 路徑的反斜線判斷

ANSI 建置下 Big5（CP950）／Shift-JIS 雙位元組字元的第二位元組可為 0x5C（如「許功蓋」），`strrchr(path,'\')` 會在字元中間切斷。比較模組路徑拆解一律改用 shlwapi（`PathFindFileNameA`／`PathFindExtensionA`／`PathRemoveFileSpecA`），截斷以 `CharNextA` 逐字前進；`export.c` 的 `<主檔>_…log` 命名同步修正（`strrchr`→shlwapi）。
