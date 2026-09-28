# ROI Analyzer — v2.8 定稿（純旋轉＋未存檔防護版）

> 版本 v2.8 定稿 | 日期：2026-09-27 | 影像旋轉（90°／180°／270°／任意角度）＋未存檔詢問防護＋WIC PNG 覆寫存檔
> 相對 v2.8 草案：**刪除授權保護整段**（`license.dat` 多組 Key、4 種降級模式、硬編碼日期＋聯網對時 T1）。
> 刪除理由：本專案為自用影像分析工具，授權機制屬「給自己上紙鎖」；T1 聯網對時在防火牆環境不可靠，
> 硬編碼 60 天強制終止是定時炸彈（且 `ExitProcess` 與未存檔防護精神矛盾）；`license.dat` 明文 Key 比對
> 5 分鐘可繞過，真要做需簽章（另一個量級）；4 種降級模式是永久維護稅（每個新功能都要問「降級下呢？」，
> `NO_LAB` 會破壞 log 格式、`NO_RECALC` 與導覽 ROI 沿用邏輯糾纏）。如日後需發外部 trial 版，授權獨立分支處理，不進 master。

## 變更歷史

- **v2.8 定稿（2026-09-27）**：旋轉＋未存檔防護（本文件）。新增 `src/rotate.h/.c`、`src/image_save.h/.c`；
  `app.h` 加 `is_modified`；主選單加 `Image` 頂層選單（旋轉 4 項）＋File 加 `Save Image (Ctrl+S)`；
  6 個丟棄攔截點共用 `App_ConfirmDiscard()`；旋轉時 `Compare_CloseAll()`；標題列／狀態列 `*` 標記。

## 1. 目標與範圍

| # | 項目 | 內容 |
|---|------|------|
| 1 | 影像旋轉 | 90°／180°／270°（無損像素搬移）＋任意角度（雙線性插值，反向映射，背景填黑） |
| 2 | ROI 跟隨 | 正交旋轉對 ROI 矩形做座標變換＋`ROI_ReanalyzeAll()`；任意角度清空手動 ROI（矩形不再軸對齊），分區格線按新尺寸重建 |
| 3 | 未存檔防護 | `g_app.is_modified` 旗標；6 個入口共用 `App_ConfirmDiscard()`（Yes／No／Cancel） |
| 4 | 覆寫存檔 | `Image_SavePNG()`（WIC `CreateBitmapFromMemory`＋PNG 編碼），覆蓋原路徑，成功後 `is_modified = FALSE` |
| 5 | 比較視窗語義 | 旋轉時已開的 V1／V2 直接關閉（`Compare_CloseAll()`），不做副本同步 |

非目標：旋轉歷史／Undo；另存新檔（Save As）；JPG／BMP 編碼寫入（讀入非 PNG 者亦以 PNG 編碼覆蓋原路徑，副檔名不變——文件註明此限制）。

## 2. 新增與變更模組

```
src/
├── rotate.h/.c      # 旋轉核心＋ROI 矩形變換 helper（純計算，不碰 g_app）
├── image_save.h/.c  # Image_SavePNG：WIC 記憶體點陣 → PNG 覆寫（仿 compare_snap.c snap_encode）
├── app.h            # app_t 加 BOOL is_modified
├── main.c           # App_Rotate*、App_SaveImage、App_ConfirmDiscard、選單／加速鍵／6 攔截點／標題列標記
├── roi.c            # 不動（沿用 ROI_ClearSource／ROI_BuildGrid／ROI_ReanalyzeAll）
└── CMakeLists.txt   # 加 src/rotate.c、src/image_save.c（windowscodecs 已連結，不需加）
```

依賴方向：`main → rotate`，`main → image_save`，`rotate → image/roit`（僅型別＋`AnalyzeROI` 不需，見 §3）。

```c
/* File: src/rotate.h */
#pragma once
#include <windows.h>
#include "image.h"

/* In-place 旋轉：成功回 TRUE（配置失敗回 FALSE，原圖不變）。
   旋轉後 img->w/h 互換（90/270），pitch = w*4，path/decoder/valid 保留。 */
BOOL Image_Rotate90(image_t *img);    /* 順時針 90° */
BOOL Image_Rotate180(image_t *img);   /* 180° */
BOOL Image_Rotate270(image_t *img);   /* 順時針 270°（= 逆時針 90°） */
/* 任意角度（順時針 angle_deg）：畫布放大至容納全圖，雙線性插值，背景 RGB(0,0,0)。
   成功回 TRUE；失敗（配置／參數）回 FALSE，原圖不變。 */
BOOL Image_RotateArbitrary(image_t *img, double angle_deg);

/* ROI inclusive-rect 座標變換（old_w/old_h 為旋轉前尺寸；呼叫端保證 rect 已正規化）。
   公式見 §3，輸出必為正規 rect（left<=right， top<=bottom）。 */
void ROI_RotateRect90(RECT *rc, int old_w, int old_h);
void ROI_RotateRect180(RECT *rc, int old_w, int old_h);
void ROI_RotateRect270(RECT *rc, int old_w, int old_h);
```

```c
/* File: src/image_save.h */
#pragma once
#include "image.h"

/* 以 PNG 編碼覆寫 path（原子的先寫 temp＋MoveFileEx，仿 snap_encode 語義）。
   成功回 0，失敗回 -1（檔案不存在／WIC 失敗；不動原檔）。 */
int Image_SavePNG(const image_t *img, const char *path);
```

## 3. ROI 座標變換數學（inclusive rect）

ROI 以影像座標儲存（inclusive，`AnalyzeROI` 內含 clamp＋order，`ROI_Add` 回寫正規化座標）。
設原圖 `w×h`，舊 rect `(l,t,r,b)`（已 `l<=r`，`t<=b`），像素座標變換為：

| 旋轉 | 像素 `(x,y) → (x',y')` | 新尺寸 | 新 rect |
|------|------------------------|--------|---------|
| 90° CW | `(h-1-y, x)` | `w'=h, h'=w` | `l'=h-1-b, r'=h-1-t, t'=l, b'=r` |
| 180° | `(w-1-x, h-1-y)` | 同原 | `l'=w-1-r, r'=w-1-l, t'=h-1-b, b'=h-1-t` |
| 270° CW | `(y, w-1-x)` | `w'=h, h'=w` | `l'=t, r'=b, t'=w-1-r, b'=w-1-l` |

實作：`App_RotateOrthogonal(steps)` 先記 `old_w/old_h` → 調 `Image_Rotate*` → 對每個 `roi_item_t.rc`
調對應 `ROI_RotateRect*` → 手動框與 GRID3／GRID5 **全部直接變換**（格線旋轉後仍是均勻格線，無需重建；
但若旋轉後 `w'<n 或 h'<n`（極小圖），該格線源按既有「小圖格線清除」語義清除）→ `ROI_ReanalyzeAll()`。
任意角度：`ROI_ClearSource(MANUAL)` 清空手動框；格線源按新尺寸 `ROI_BuildGrid` 重建
（`w'<n||h'<n` 則清除並沿用既有提示訊息）→ `ROI_ReanalyzeAll()`。

## 4. 旋轉主流程 `App_RotateOrthogonal(int steps)`／`App_RotateArbitrary(double deg)`

```
1. 無有效影像 → 直接返回（選單項灰化，見 §8）。
2. Compare_CloseAll()（關閉已開 V1/V2，避免副本不同步）。
3. 記 old_w/old_h；調 Image_Rotate*（失敗 → MessageBox 錯誤，返回，原圖不變）。
4. ROI 變換（§3：正交變換／任意角清空＋格線重建）→ ROI_ReanalyzeAll()。
5. ViewPyr_Free + pyramid_attempted/pending=FALSE；View_Reset（目前 canvas 尺寸）。
6. g_app.img_gen++；analysis_stale=TRUE；is_modified=TRUE。
7. UpdateTitle()；App_RoiChanged()（表格＋狀態列）；App_UpdateHistogram()。
```

注意：旋轉不動 `files`／`file_idx`（仍是同一路徑）；`decoder` 欄位保留（描述原始解碼器）；
狀態列 `g_nav_status = "Rotated 90 CW; unsaved"` 類提示。

## 5. 未存檔防護 `App_ConfirmDiscard()`

```c
/* 回 TRUE = 可繼續（未修改／使用者選 Yes 且存檔成功／選 No）；FALSE = 中斷當前動作。 */
static BOOL App_ConfirmDiscard(void)
{
    int ret;
    if (!g_app.is_modified || !g_app.img.valid)
        return TRUE;
    ret = MessageBoxA(g_app.hwnd_main,
        "Image has been rotated but not saved. Save changes?",
        "ROI Analyzer", MB_YESNOCANCEL | MB_ICONWARNING);
    if (ret == IDYES)
        return App_SaveImage();   /* 成功 TRUE；失敗已彈錯 → FALSE（中斷） */
    return ret == IDNO;           /* CANCEL → FALSE */
}
```

6 個攔截點（全部調 `App_ConfirmDiscard()`，FALSE 即 `return`）：

| # | 入口 | 位置 | 插入點 |
|---|------|------|--------|
| 1 | 導覽上一張／下一張 | `App_Navigate()` 開頭 | `direction` 檢查後第一行 |
| 2 | 開檔對話框 | `OpenImageFile()` 開頭 | `App_FlushPending()` 之前 |
| 3 | 拖放 | `App_OnDropFiles()` 開頭 | `CmpDrop_Collect` 之前 |
| 4 | 比較視窗開啟 | `App_CompareOpenPaths()` 開頭 | `App_FlushPending()` 之前（涵蓋 `Compare Files…`、`Compare Current with Next`、拖放多檔三路；比較視窗讀磁碟檔，不會丟旋轉內容，但統一詢問避免語義混亂） |
| 5 | 關閉視窗 | 新增 `WM_CLOSE` handler | `if (!App_ConfirmDiscard()) return; DestroyWindow(hwnd);`（目前只有 `WM_DESTROY→PostQuitMessage`，無 `WM_CLOSE`，必須新增） |
| 6 | 選單 Exit | `IDM_EXIT` 分支 | `DestroyWindow` 改走 `SendMessage(hwnd, WM_CLOSE, 0, 0)`（目前直接 `DestroyWindow` 會繞過 `WM_CLOSE`） |

## 6. 存檔 `App_SaveImage()`

```c
/* 回 TRUE = 存檔成功（is_modified=FALSE）；FALSE = 失敗（已彈錯）。 */
static BOOL App_SaveImage(void)
{
    if (!g_app.img.valid)
        return FALSE;
    if (Image_SavePNG(&g_app.img, g_app.img.path) != 0) {
        MessageBoxA(g_app.hwnd_main, "Could not save image (PNG encode failed).",
                    "Save Image", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    g_app.is_modified = FALSE;
    _snprintf(g_nav_status, sizeof(g_nav_status), "Saved %s", image_basename(g_app.img.path));
    UpdateTitle(); App_UpdateStatus();
    return TRUE;
}
```

`Image_SavePNG` 實作要點（`src/image_save.c`，ANSI 全 A 版）：
`CoCreateInstance(CLSID_WICImagingFactory)` → `CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA,
pitch, size, px)`（`image_t.px` 為 32bpp BGRA，與 WIC BGRA 同序，可直接用）→ PNG encoder
（`GUID_ContainerFormatPng`＋`WICBitmapEncoderNoCache`＋`CreateNewFrame`＋`WriteSource`＋`Commit`），
經 temp 檔＋`MoveFileEx(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)` 原子覆寫
（仿 `compare_snap.c snap_encode`，該函式為 static 不共用，複製其模式約 60 行）。

## 7. `Ctrl+S` 語義區分

- 主視窗加速鍵加 `{ FVIRTKEY | FCONTROL, 'S', IDM_SAVE_IMAGE }` → 存原圖（`App_SaveImage()`）。
- 比較視窗 `Ctrl+S` 維持 Snapshot 存檔＋複製（V1／V2 為無 owner 獨立頂層視窗，
  `Compare_PreTranslate()` 先於主迴圈加速鍵攔截，且主迴圈有 `GA_ROOT==hwnd_main` guard（C6），
  兩者天然不衝突）。
- 文件寫明：主視窗 `Ctrl+S`＝覆寫原圖檔；比較視窗 `Ctrl+S`＝快照 PNG＋剪貼簿（S7）。

## 8. 選單／加速鍵／標記

新 ID（避開既有：File 101-105、Mode 111-114、Edit 121-123、Log 131-132、Zoom 141-142、
Hist 151／161-166、Compare 155-156）：

| ID | 值 | 選單位置 | 文字 |
|----|----|----------|------|
| `IDM_SAVE_IMAGE` | 106 | File（`IDM_OPEN` 之後） | `Save Image\tCtrl+S` |
| `IDM_ROT90` | 115 | Image（新頂層，View 之前） | `Rotate 90° Clockwise` |
| `IDM_ROT180` | 116 | Image | `Rotate 180°` |
| `IDM_ROT270` | 117 | Image | `Rotate 270° Clockwise` |
| `IDM_ROT_ANY` | 118 | Image | `Rotate Arbitrary…` |

- 新頂層 `Image` 選單（不擠 Edit）：4 個旋轉項。`Rotate Arbitrary…` 用簡易 modal 對話框取角度
  （`GetDlgItemInt` 式數字輸入，範圍 -360～360，0／360 視為無動作）。
- 灰化：無有效影像時 5 項（Save＋4 旋轉）全部 `EnableMenuItem(MF_GRAYED)`（在既有選單狀態更新處加）。
- 標題列：`UpdateTitle()` 檔名段加 `*`（如 `… [Drag] [Single] *`，`is_modified` 時）。
- 狀態列：`App_UpdateStatus()` 的 `mode` 欄尾加 ` *`（同條件）；`g_nav_status` 在旋轉／存檔後更新。

## 9. 驗收項目

| # | 情境 | 預期 |
|---|------|------|
| R1 | 90°／180°／270° 各一次 | 畫布適配新尺寸，ROI 框位置正確（與手算 §3 公式一致），表格數值更新，標題列＋狀態列出現 `*` |
| R2 | 90° 連續 4 次 | 與原圖像素級一致（可寫小工具比對 `px`；旋轉為無損搬移，4 次必回原） |
| R3 | 任意角度 30° | 畫布放大容納，背景黑邊，手動 ROI 清空，格線重建，表格更新 |
| R4 | 旋轉後按 `→`／開檔／拖放／關閉／開比較視窗 | 彈 Yes／No／Cancel：Yes 存檔後繼續（`*` 消失），No 直接繼續（`*` 保留與否依新圖重設），Cancel 留原地 |
| R5 | 旋轉後 `Ctrl+S` | 覆蓋原路徑 PNG，`*` 消失，狀態列 `Saved <檔名>` |
| R6 | 旋轉時開著 V1／V2 | 比較視窗自動關閉 |
| R7 | 存檔後再旋轉 | `*` 重新出現（is_modified 再次置 TRUE） |
| R8 | 空程式（未開圖）按旋轉／存檔 | 選單灰化不可點；加速鍵無動作 |

## 10. 建置調整

```cmake
add_executable(roi_analyzer
    # ... 既有檔案 ...
    src/rotate.c
    src/image_save.c
)
```

其餘不變（`-Wall -Wextra` 零警告；`windowscodecs`／`ole32` 已連結）。

## 11. 實作順序（派工用）

1. `rotate.h/.c`（正交搬移＋任意角雙線性＋3 個 rect helper，約 150 行）→ 單元可測（R2 四次回原）。
2. `image_save.h/.c`（WIC PNG 覆寫，約 70 行）。
3. `main.c`：`is_modified`（app.h）＋`App_Rotate*`＋`App_SaveImage`＋`App_ConfirmDiscard`＋
   選單／加速鍵／灰化／6 攔截點／`WM_CLOSE`／標題列狀態列標記。
4. Release clean 建置驗證（`rm -rf build` 重配，`PATH` 含 `/c/msys64/ucrt64/bin`，0 warning）。
   只建置，不 smoke、不 GUI、不 commit。
