# 比較視窗分析模式（Sharp / Texture / Neutral）架構書 v1.0（可執行版）

> 專案：`C:\Github\roi-analyzer`（純 C ＋ Win32 ＋ GDI，ANSI 全 A 版，`-Wall -Wextra` 零警告，C11，宣告置頂，不用 `//` 註解）
> 基準：`master`（v2.7 CompareForm V1/V2＋Snapshot＋Metrics Report 已合併）
> 來源提案：`@file:pasted_content_2026-10-05_06-01-25-037_6637d2.txt`（以下簡稱「提案」；§0–§2、§4 概念沿用，§5–§9 已按實碼重寫為可執行規格）
> 實碼錨點：`src/compare.h`、`compare_image.c`、`compare_core.c`（`CmpView_VisibleRect`＋`Cmp_Blit`）、`compare_v1.c`（`cmp_cell_t`／`cmp_v1_t`／`v1_render`／`v1_visible_roi`／`v1_open_v2`／工具列座標）、`compare_v2.c`（`cmp_v2_t`／`v2_render`／`v2_image_index`／`v2_layout`）、`src/image.h`（`image_t` 含 `decoder[8]`）、`src/analyze.c`（`rgb_to_lab_f`）、`CMakeLists.txt`（已連結 `m`）
> 日期：2026-10-05
> 狀態：**v1.0 可執行定稿 → 發包 gh 實作**

## 變更歷史

| 版本 | 日期 | 內容 |
|---|---|---|
| v1.0 | 2026-10-05 | 提案進版。概念（D8–D12、演算法、UI）沿用；以下按實碼修正為可執行規格：R1 `CompareV2_Open` 簽名（新增 `CompareV2_OpenMode` 包裝，不動既有 2 參）；R2 來源矩形改用既有 `CmpView_VisibleRect`＋`ScreenToImage`（提案 `CmpView_VisibleSrc` 不存在）；R3 全部 struct 追加欄位按實碼欄位名重寫；R4 `alloc_like` 補 `decoder[8]`；R5 量測前向 Lab 改用 `analyze.c rgb_to_lab_f`（單一真實源）；R6 V1/V2 版面給出精確座標（V1 工具列移位、V2 第 4 群組＋最小寬 900）；R7 同分 tie 規則與驗收對齊；R8 Snapshot／Metrics Report 共存說明 |

---

## 0. 需求解讀（沿用提案）

| 項目 | 採用 |
|---|---|
| 按鈕數 | 3 個：Sharp、Texture、Neutral |
| 語義 | 分析顯示模式：畫面換成分析圖，狀態帶／標籤顯示數值並標最佳者 |
| 範圍 | 畫面以原生解析度算整張分析圖（縮放平移 Lock 照常）；數值只統計目前可見影像區域 |
| Sharp | Sobel 梯度圖，指標 P99 邊緣陡度＋平均梯度 |
| Texture | 5×5 高通殘差，排除強邊，只看低反差細節 |
| Neutral | 中性像素 a*/b* 放大 4 倍顯示，非中性塗黑；指標為平均 a*/b*、色偏量與方向 |

## 1. 新增決策 D8–D12（沿用提案）

| # | 議題 | 採用 |
|---|---|---|
| D8 | 模式 | `cmp_ana_t` 四選一（NONE/SHARP/TEXTURE/NEUTRAL），V1、V2 都有；三顆 PUSHLIKE 互斥，再按回 NONE；按鍵 E/T/N（無 Ctrl/Alt 時；V1 既有 0/＋/－/L/V，V2 既有 S/M/P/[/]/0 皆無 E/T/N；C6 保證不落主視窗） |
| D9 | 分析圖儲存 | 衍生圖 BGRA `image_t`，`CmpImage_Adopt` 搬移包成 `cmp_image_t`（`is_derived`，不計 `LiveCount`），直接走既有金字塔與 `Cmp_Blit`；快取於原圖 `ana[mode]`，`ana_users` 計數，V1→V2 共用免重算 |
| D10 | 數值範圍 | 可見來源矩形（原生像素，§7 作法），取樣上限 2^20（stride 抽樣，每樣本原生 3×3／5×5 鄰域）；V1 各格自己的可見區；V2 兩圖都算整個畫布視口（與分割線無關，比較才公平） |
| D11 | 更新時機 | 切換模式立即算；視圖變更 120 ms 去彈跳（同 ID `SetTimer` 重設）延後算，來源矩形未變跳過；繪製不計算 |
| D12 | 公平性 | 固定增益不逐圖正規化；V1 解析度不一致時 Sharp/Texture 不標最佳（`best=-1`），Neutral 照常標 |

非目標：直方圖、差異圖、閃爍、匯出、Report（D2 維持不做）。`CmpAna_Measure` 留給 P5 Report 用（F12）。

## 2. 演算法規格（沿用提案，修正排版筆誤）

共用亮度：`Y = (77R + 150G + 29B + 128) >> 8`，sRGB 8-bit。分析圖與數值用完全相同的核心與門檻。

| 模式 | 分析圖 | 視覺化 | 數值 | 最佳 |
|---|---|---|---|---|
| Sharp | Sobel L1：`g=\|Gx\|+\|Gy\| ∈ [0,2040]` | 灰階 `255√(min(g,1020)/1020)`，黑底白線 | P99(g)、mean(g) | P99 最大，同分比 mean |
| Texture | `a = \|25Y − Σ5×5Y\|`；`g ≥ 160` 強邊排除 | 非邊灰階 `255√(min(a/25,32)/32)`；強邊暗藍 BGR(70,30,0) | 非邊區平均 `a/25`＋非邊覆蓋率 | 平均最大 |
| Neutral | sRGB→線性→XYZ(D65)→Lab，`C=√(a²+b²)` | `C<10` 且 `8≤L≤96`：`(L,4a,4b)` 轉回 sRGB；其餘塗黑 | 中性覆蓋率、`(ā,b̄)`、色偏量 `√(ā²+b̄²)`、色相角 | 色偏量最小 |

色相 8 分區：0° Red、45° Orange、90° Yellow、135° Yel-Grn、180° Green、225° Cyan、270° Blue、315° Magenta；色偏量 < 1.0 顯示 `none`。

使用注意：數值原生解析度；畫面 <100% 是 HALFTONE 縮圖，判讀分析圖請放大到 100% 以上；Texture 單張無法區分雜訊與細節，須同時看圖（結構化才算細節）。

## 3. 架構變更（修正版）

| 檔案 | 變更 |
|---|---|
| `compare.h` | ＋約 70 行：`cmp_ana_t`、`cmp_metric_t`、常數、API（§5）；`cmp_image_t` 加 `ana[]/ana_users[]/ana_failed[]/is_derived`；新增 `CompareV2_OpenMode`（R1） |
| `compare_image.c` | ＋約 35 行：`CmpImage_Adopt`；`Unref` 連帶釋放 `ana[]`；衍生圖不計 `LiveCount`（§6） |
| `compare_analysis.c` | 新檔約 430 行：LUT、三種分析圖、取樣量測、格式化、最佳判定、`CmpAna_VisibleSrcImage`（§7；R2：不用提案的 `CmpView_VisibleSrc`，改組既有函式） |
| `compare_v1.c` | ＋約 150 行：3 顆按鈕（R6 座標）、模式切換、計時器、狀態帶、`v1_disp`（含 snapshot render）、`v1_open_v2` 傳 mode（§8） |
| `compare_v2.c` | ＋約 170 行：Analyze 第 4 群組＋版面重排（R6）、模式切換、計時器、A:/B: 標籤、`v2_disp`（§9） |
| `main.c` | **0 行**（R1：既有 `CompareV2_Open(a,b)` 呼叫點不動，預設 NONE） |
| CMake | 追加 `compare_analysis.c`（`m` 已連結，`cbrtf/powf/atan2f/ceil/sqrtf` 可用） |

所有權（沿用提案）：`Acquire` 首建 `ana_users=1`、命中＋1；`Release` −1，歸零釋放；換模式／移除格／`WM_DESTROY` 先 Release 再 Unref；原圖釋放殘留強制釋放，Debug 查 `ana_users==0`。

記憶體：每圖每模式約 `4WH×1.33`（＋金字塔），24MP 約 128MB；`CMP_ANA_MAX_PIXELS` 80MP 上限，`malloc` 失敗整批回退＋提示。耗時（24MP 單執行緒估計）：Sharp 約 60ms、Texture 約 100ms、Neutral 約 300ms；執行中等待游標＋`Analyzing i/n`。

程式風格：宣告置頂、C89 相容（本專案慣例）、禁 `//` 註解；字串用 `_snprintf`＋手動 `'\0'`（與 `v1_render`／`v2_render` 一致）；`-Wall -Wextra` 零警告；CRLF。

---

## R1. `CompareV2_Open` 簽名（實碼 2 參，提案 3 參）

實碼（`compare.h:73`）：`HWND CompareV2_Open(cmp_image_t *left, cmp_image_t *right);`
呼叫點：`main.c:1459`、`main.c:1783`、`compare_v1.c:742`（`v1_open_v2`）。

**定案：不改既有簽名**，新增包裝（呼叫點只動 1 處）：

```c
/* File: compare.h（追加） */
HWND CompareV2_OpenMode(cmp_image_t *left, cmp_image_t *right, cmp_ana_t mode);
```

```c
/* File: compare_v2.c */
HWND CompareV2_Open(cmp_image_t *left, cmp_image_t *right)
{
    return CompareV2_OpenMode(left, right, CMP_ANA_NONE);
}
```

`v1_open_v2` 改調 `CompareV2_OpenMode(cell[0], cell[1], state->mode)`；`main.c` 兩處不動。
`mode` 在視窗建立完成、首次 `WM_SIZE` fit 之後套用（呼叫內部 `v2_set_mode`，快取命中只＋1，無等待游標）。

## R2. 來源矩形（提案 `CmpView_VisibleSrc` 不存在）

實碼只有螢幕空間的 `CmpView_VisibleRect(view, view_w, view_h, image_w, image_h, &visible)`（`compare.h:97`）。
**定案：新增 `CmpAna_VisibleSrcImage`**，組既有函式（與 `v1_visible_roi` 同算法）：

```c
/* File: compare_analysis.c */
BOOL CmpAna_VisibleSrcImage(const cmp_image_t *ci, const cmp_view_t *view,
                            int view_w, int view_h, RECT *out)
{
    RECT visible;
    double x0, y0, x1, y1;

    if (!ci || !view || !out) {
        return FALSE;
    }
    if (!CmpView_VisibleRect(view, view_w, view_h,
                             ci->img.w, ci->img.h, &visible)) {
        return FALSE;
    }
    CmpView_ScreenToImage(view, view_w, view_h,
                          (double)visible.left, (double)visible.top,
                          &x0, &y0);
    CmpView_ScreenToImage(view, view_w, view_h,
                          (double)visible.right, (double)visible.bottom,
                          &x1, &y1);
    out->left = (LONG)floor(x0);
    out->top = (LONG)floor(y0);
    out->right = (LONG)ceil(x1);
    out->bottom = (LONG)ceil(y1);
    if (out->left < 0) {
        out->left = 0;
    }
    if (out->top < 0) {
        out->top = 0;
    }
    if (out->right > ci->img.w) {
        out->right = ci->img.w;
    }
    if (out->bottom > ci->img.h) {
        out->bottom = ci->img.h;
    }
    return out->right > out->left && out->bottom > out->top;
}
```

呼叫端：
- V1：`view_w/h` 取 `cell->image_rect` 寬高（與傳給 `Cmp_Blit` 的 `vp` 一致）。
- V2：`view_w/h` 取 overlay client 寬高，`vp=clip=full`（D10：兩圖都量整個畫布）。

## R3. struct 欄位名（提案與實碼全脫節）

實碼（`compare_v1.c:21-59`、`compare_v2.c:30-70`）：

- `cmp_cell_t`：`image`（非 `ci`）、`view`、`cell`、`image_rect`、`status_rect`、`close_rect`。
- `cmp_v1_t`：`hwnd`、`grid`、`lock`、`v2`、`snapshot`、`metrics`、`progress`、`info_bar`、`message`、`tooltip`、`cells[]`、`count`（非 `n`）、`metrics_job`、`locked`（非 `lock`）、`show_info`、`need_fit`…
- `cmp_v2_t`：`hwnd`、`overlay`、`status`、`track[2]`、`label[2]`、`grp[3]`（→改 `[4]`）、`sync`、`pan_left`、`pan_right`、`swap_button`、`split_button`、`reset_button`、`snapshot_button`、`metrics_button`、`info_bar`、`progress`、`tooltip`、`image[2]`（非 `img`）、`metrics_job`、`view[2]`、`swapped`（非 `swap`）、`split_mode`、`sync_pan`、`pan_side`、`split_fraction`…

追加欄位（精確名）：

```c
/* cmp_cell_t 追加 */
cmp_image_t *ana;
cmp_metric_t met;
RECT met_src;

/* cmp_v1_t 追加 */
cmp_ana_t mode;
HWND btn_ana[3];
int best;
BOOL res_mixed;

/* cmp_v2_t 追加（grp 由 [3] 改 [4]） */
cmp_ana_t mode;
HWND btn_ana[3];
cmp_image_t *ana[2];
cmp_metric_t met[2];
RECT met_src[2];
int best;
```

`§8/§9` 片段一律用以上欄位名；`swapped` 經 `v2_image_index(state, side)`（`side ^ swapped`）換算，數值跟底片走（`met[0]=A`、`met[1]=B`），Swap 不重算。

## R4. `alloc_like` 漏 `decoder[8]`

實碼 `image_t` 有 `char decoder[8]`（`image.h:11`），且 `Image_Clone` 有拷貝（`image.c:127`）。
`alloc_like` 必須補 `lstrcpynA(dst->decoder, src->decoder, sizeof(dst->decoder))`，
否則衍生圖走 Snapshot WIC 路徑時 `decoder` 為空。

## R5. Lab 單一真實源（提案自帶 Lab，與 `analyze.c` 係數有差）

實碼 `rgb_to_lab_f`（`analyze.c:48-69`，D65 sRGB）矩陣為 `0.4124/0.3576/0.1805…`，
提案 `rgb_to_lab` 為 `0.4124564/0.3575761/0.1804375…`——同圖同像素 Neutral 數值會差約 0.1–0.3，
與主程式／Metrics Report 不一致（`compare_metrics_architecture.md` §1.5 明定色彩單一真實源）。

**定案：**
- `CmpAna_Measure` 的 Neutral 前向轉換一律調 `rgb_to_lab_f`（`#include "analyze.h"`）。
- `build_neutral` 顯示用逆轉換：實碼無 `lab_to_rgb`，提案 `lab_to_rgb`＋`gam` LUT 保留（顯示用途，±1 LSB 不影響數值）。
- 提案 `rgb_to_lab`／`flab`／`s_lin`／`s_flab` 前向 LUT 刪除（只留 `s_gam` 逆向 LUT＋`finv`）。
- 若日後 Neutral 數值要與 Report 對賬，以 `rgb_to_lab_f` 為準。

## R6. 版面精確座標

### V1 工具列（實碼 `compare_v1.c:938-955`）

現況：Lock(7,67) V2(77,45) Snapshot(126,82) Metrics(304,94) Info(402,86) message(494,560)。
Snapshot 結束於 x=208，Metrics 起於 304——中間 96px 不夠放 3 鈕，必須右移。

定案（y=7,h=24 不變；新鈕 `BS_AUTOCHECKBOX|BS_PUSHLIKE`，字型 `Compare_Font()`，tooltip 比照 snapshot）：

| 控制項 | x | 寬 |
|---|---|---|
| Sharp | 212 | 62 |
| Texture | 278 | 70 |
| Neutral | 352 | 66 |
| Metrics Report（右移） | 424 | 94 |
| Info bar（右移） | 522 | 86 |
| message STATIC（右移縮短） | 614 | 440 |

`v1_layout` 不動（工具列高度 `V1_TOOLBAR_H=40` 不變）。

### V2 控制列（實碼 `compare_v2.c:613-661`，`V2_TOP_H=76`，`V2_GROUP_H=64`）

現況 3 群組：Zoom 佔半、PanSync／Actions 平分另半；Actions 下排已滿（Snapshot／Info／Metrics 三鈕）。
定案：`grp[3]`→`grp[4]`，新增 `grp[3]` 標題 `"Analyze"`；比例改為 Zoom 2/5、PanSync 1/5、Actions 1/5、Analyze 1/5；
`ptMinTrackSize.x` 760→900（`WM_GETMINMAXINFO`，`compare_v2.c:947`）；
新鈕 ID 見 §5（`V2_ID_ANA_*` 4211–4213），三鈕在 Analyze 群組內縱向沿用 Actions 下排樣式
（上排 3 鈕 h=22、下排沿用；`v2_layout` 按新比例重算四段 `gx`，其餘控制項公式不變）。

`V2_TOP_H`、`V2_GROUP_H`、`V2_BOTTOM_H` 不變；overlay／status 位置公式不變。

## R7. 同分 tie（提案 §10 與 `better` 矛盾）

提案驗收寫「同一張圖開兩格…`best=-1`」，但括號又說同分 `better` 回 FALSE、最佳為第一格——兩者矛盾
（`valid>=2` 時程式必回第一格 index，不會回 −1）。

**定案：全等時 `best` 取第一格（確定性行為）**，驗收改為：

| 項目 | 預期（修正） |
|---|---|
| 同一張圖開兩格 | 數值完全相同，`best=0`（全等取首格；`*` 標第一格） |

其餘驗收沿用提案 §10（模糊版 Sharp/Texture 低、灰卡色偏方向、120ms 局部更新、V1→V2 免重算、20 次切換無洩漏、800%／HALFTONE／分割線一致、100MP 回退、`res_mixed` 時 Sharp/Texture 不標 `*`）。

## R8. Snapshot／Metrics Report 共存

- `v1_render`（`compare_v1.c:315`）與 `v1_render_snapshot`（`:405`）的 `Cmp_Blit` 影像參數改 `v1_disp(state,i)`；
  `v2_render`（`compare_v2.c:414,416`）改 `v2_disp(state, v2_image_index(state, side))`——
  Snapshot 擷取含分析圖的當前畫面（與「當下看到的圖」一致）；資訊列沿用既有 `CmpInfo_*`（倍率顯示原 `view.zoom`，不顯示內部放大，沿用 §14.4 慣例）。
- Metrics Report（`v1_run_metrics`，`Ctrl+M`）走既有 `metrics_async`，與本模式互不干擾；
  分析模式下按 Metrics Report，量的是**原圖**可見區（`state->cells[].image`，不用 `ana`），兩者正交。
- `WM_DESTROY`：先 `KillTimer(CMP_TID_METRIC)` → 各格 `Release` → 既有 `Unref`（V1 迴圈、`state->cells[i].image`；V2 `image[2]`）。
- V2 `Reset All` 不改 mode（沿用提案），但 views 已變→調 `v2_metrics_schedule` 重算。

---

## 5. `compare.h` 增補（定案版）

```c
/* File: compare.h */
/* ---- Analysis modes (D8-D12)：追加於既有宣告之後 ---- */
typedef enum {
    CMP_ANA_NONE = 0,
    CMP_ANA_SHARP,
    CMP_ANA_TEXTURE,
    CMP_ANA_NEUTRAL,
    CMP_ANA_COUNT
} cmp_ana_t;

typedef struct {
    cmp_ana_t mode;
    BOOL      valid;
    long      samples;
    float     sharp_p99;
    float     sharp_mean;
    float     tex_mean;
    float     tex_cover;
    float     neu_cover;
    float     neu_a;
    float     neu_b;
    float     neu_cast;
    float     neu_hue;
} cmp_metric_t;

#define CMP_ANA_MAX_SAMPLES   (1 << 20)
#define CMP_ANA_MAX_PIXELS    (80.0 * 1000.0 * 1000.0)
#define CMP_ANA_DEBOUNCE_MS   120
#define CMP_TID_METRIC        0x4D45
#define V1_ID_ANA_SHARP       4105
#define V1_ID_ANA_TEXTURE     4106
#define V1_ID_ANA_NEUTRAL     4107
#define V2_ID_ANA_SHARP       4211
#define V2_ID_ANA_TEXTURE     4212
#define V2_ID_ANA_NEUTRAL     4213

/* cmp_image_t 追加欄位（放入既有 struct 內）：
 *   struct cmp_image_s *ana[CMP_ANA_COUNT];
 *   int                 ana_users[CMP_ANA_COUNT];
 *   BYTE                ana_failed[CMP_ANA_COUNT];
 *   BOOL                is_derived;
 */

HWND CompareV2_OpenMode(cmp_image_t *left, cmp_image_t *right,
                        cmp_ana_t mode);

cmp_image_t *CmpImage_Adopt(image_t *src, const char *name);

cmp_image_t *CmpAna_Acquire(cmp_image_t *ci, cmp_ana_t mode);
void         CmpAna_Release(cmp_image_t *ci, cmp_ana_t mode);
BOOL         CmpAna_Measure(const cmp_image_t *ci, cmp_ana_t mode,
                            const RECT *src, cmp_metric_t *out);
int          CmpAna_Format(const cmp_metric_t *m, char *buf, int cch);
int          CmpAna_BestIndex(const cmp_metric_t *m, int n);
const char  *CmpAna_HueName(float deg);
BOOL         CmpAna_VisibleSrcImage(const cmp_image_t *ci,
                                    const cmp_view_t *view,
                                    int view_w, int view_h, RECT *out);
```

ID 依據：V1 既用 4101–4104（`compare_v1.c:15-19`），V2 既用 4201–4210（`compare_v2.c:16-25`），
提案 `0x0710`（1808）與既有號段風格不一，改用順延號；`CMP_TID_METRIC` 比較視窗內無既有 Timer（全檔無 `SetTimer`），安全。
`V1_ID_METRICS`／`V2_ID_METRICS`（=109）是 Metrics Report，共存不動。

## 6. `compare_image.c` 變更（定案版）

```c
/* File: compare_image.c */
/* 搬移所有權（不複製）：成功後 *src 清零 */
cmp_image_t *CmpImage_Adopt(image_t *src, const char *name)
{
    cmp_image_t *ci;

    if (!src || !src->valid || !src->px) {
        return NULL;
    }
    ci = (cmp_image_t *)calloc(1, sizeof(*ci));
    if (!ci) {
        return NULL;
    }
    ci->img = *src;
    ZeroMemory(src, sizeof(*src));
    ci->refs = 1;
    ci->is_derived = TRUE;
    lstrcpynA(ci->name, name ? name : "", MAX_PATH);
    return ci;
}

void CmpImage_Unref(cmp_image_t *ci)
{
    int m;

    if (!ci) {
        return;
    }
    if (InterlockedDecrement(&ci->refs) != 0) {
        return;
    }
    for (m = CMP_ANA_NONE + 1; m < CMP_ANA_COUNT; m++) {
#ifdef _DEBUG
        if (ci->ana_users[m] != 0) {
            OutputDebugStringA("CmpImage_Unref: ana_users leak\n");
        }
#endif
        if (ci->ana[m]) {
            CmpImage_Unref(ci->ana[m]);
            ci->ana[m] = NULL;
        }
    }
    /* …既有：ViewPyr_Free、Image_Free… */
    if (!ci->is_derived) {
        InterlockedDecrement(&s_live);
    }
    free(ci);
}
```

（`…既有…` 按現檔補齊；`live` 計數器名以現檔為準。）

## 7. `compare_analysis.c`（定案版說明）

提案 §7 全份採用，**强制修改 4 處**，其餘照抄：

1. 刪除 `rgb_to_lab`／`flab`／`s_lin`／`s_flab`／`LAB_LUT`；`#include "analyze.h"`；
   `CmpAna_Measure` 內 Neutral 分支調 `rgb_to_lab_f(p[2], p[1], p[0], &L, &A, &B)`（R5）。
   `build_neutral` 第一行前向判斷改調 `rgb_to_lab_f`，逆向 `lab_to_rgb`＋`s_gam` 保留。
2. `alloc_like` 補 `lstrcpynA(dst->decoder, src->decoder, sizeof(dst->decoder))`（R4）。
3. 刪除 `CmpView_VisibleSrc`，改為 `CmpAna_VisibleSrcImage`（R2，本書 §R2 程式為準）。
4. `is_neutral` 門檻、`TEX_EDGE_T`、`NEU_GAIN` 等常數照提案（待確認 Q3 是否可調，見 §11）。

其餘（`make_luma_padded`、`sobel_l1`、`build_sharp`、`build_texture` 滾動欄和、
`fetch5x5`／`sobel_k` 取樣、`hist[2041]` P99、`CmpAna_Format`、`better`、`CmpAna_BestIndex`、
`CmpAna_HueName`）照提案實作。注意：
- `build_texture` 的 `WORD col[]` 上限 1275（5×255），無溢位；`s` 用 `int`。
- `hist` 8KB 區域變數：改為 `static DWORD` 或 `calloc`（避免小堆疊視窗執行緒風險；二選一，建議 `calloc`＋`free`）。
- `snprintf`→`_snprintf`＋手動封口（本專案風格；`v1_render` 前例）。
- `log` 行數：提案估 420 行，＋R5 刪除約 −40 行，＋可執行註解，實作以檔案為準。

## 8. V1 整合（定案版，所有識別字按實碼）

```c
/* File: compare_v1.c */
/* cmp_cell_t 追加：cmp_image_t *ana; cmp_metric_t met; RECT met_src; */
/* cmp_v1_t 追加：cmp_ana_t mode; HWND btn_ana[3]; int best; BOOL res_mixed; */

static cmp_image_t *v1_disp(const cmp_v1_t *state, int i)
{
    const cmp_cell_t *cell = &state->cells[i];
    return (state->mode != CMP_ANA_NONE && cell->ana) ? cell->ana : cell->image;
}

static void v1_sync_ana_buttons(cmp_v1_t *state)
{
    int k;
    for (k = 0; k < 3; k++) {
        SendMessageA(state->btn_ana[k], BM_SETCHECK,
                     (state->mode == (cmp_ana_t)(CMP_ANA_SHARP + k)) ?
                     BST_CHECKED : BST_UNCHECKED, 0);
    }
}

static void v1_metrics_update(cmp_v1_t *state)
{
    cmp_metric_t all[CMP_MAX_CELLS];
    int i;

    state->res_mixed = FALSE;
    for (i = 0; i < state->count; i++) {
        cmp_cell_t *cell = &state->cells[i];
        int view_w = cell->image_rect.right - cell->image_rect.left;
        int view_h = cell->image_rect.bottom - cell->image_rect.top;
        RECT src;

        if (cell->image->img.w != state->cells[0].image->img.w ||
            cell->image->img.h != state->cells[0].image->img.h) {
            state->res_mixed = TRUE;
        }
        if (state->mode == CMP_ANA_NONE ||
            !CmpAna_VisibleSrcImage(cell->image, &cell->view,
                                    view_w, view_h, &src)) {
            ZeroMemory(&cell->met, sizeof(cell->met));
            SetRectEmpty(&cell->met_src);
        } else if (cell->met.mode != state->mode ||
                   !EqualRect(&src, &cell->met_src)) {
            CmpAna_Measure(cell->image, state->mode, &src, &cell->met);
            cell->met_src = src;
        }
        all[i] = cell->met;
    }
    state->best = CmpAna_BestIndex(all, state->count);
    if (state->res_mixed && state->mode != CMP_ANA_NEUTRAL) {
        state->best = -1;
    }
}

static void v1_metrics_schedule(cmp_v1_t *state)
{
    if (state->mode != CMP_ANA_NONE) {
        SetTimer(state->hwnd, CMP_TID_METRIC, CMP_ANA_DEBOUNCE_MS, NULL);
    }
}
```

`v1_set_mode` 照提案（`imgs`→`state->cells[i].image`、`lbl_msg`→`state->message`、`n`→`state->count`；
失敗回退＋`SetCursor`＋訊息欄英文；成功後 `v1_metrics_update`＋`InvalidateRect(state->grid,…)`）。

WndProc 接線（實碼位置）：
- `WM_CREATE`（`compare_v1.c:936`）：建三鈕（R6 座標）＋`WM_SETFONT(Compare_Font())`＋`*_create_tooltip` 比照；失敗回 `-1`。
- `WM_COMMAND`：`V1_ID_ANA_*` → `v1_set_mode(SHARP + id − V1_ID_ANA_SHARP)`＋`SetFocus(state->grid)`。
- `WM_TIMER`：`wParam == CMP_TID_METRIC` → `KillTimer`＋`v1_metrics_update`＋重繪 grid。
- `CMPM_KEY`（`compare_v1.c:1033`，`!ctrl` 區，`'L'`／`'V'` 旁）：`'E'/'T'/'N'` → `v1_set_mode`＋`return 1`。
- 視圖變更集中點（pan／滾輪／fit／按鍵／grid `WM_SIZE`）調 `v1_metrics_schedule`。
- 加圖（DROPFILES 成功載入後）：`if (mode) cell->ana = CmpAna_Acquire(...)`，NULL 則顯示原圖、`met` 留 `n/a`。
- 移除格：Release 該格舊模式 ana（在 `Unref(state->cells[i].image)` 之前；陣列前移搬動 struct 無需另處理 `ana_users`）；
  `WM_DESTROY`：`KillTimer` → 逐格 Release → 既有釋放（`compare_v1.c:100-108` 前）。
- 繪製：`v1_render:315`、`v1_render_snapshot:405` 影像參數改 `v1_disp(state, i)`；
  狀態帶（`:319`）：mode!=NONE 時 `txt = "[* ]Metric | name | %"`（`*`＋黃字 `RGB(255,220,0)` 為 best，
  指標放最前防 `DT_END_ELLIPSIS` 截斷；`_snprintf`）。
- `v1_open_v2`（`:739`）：改 `CompareV2_OpenMode(cells[0].image, cells[1].image, state->mode)`。

## 9. V2 整合（定案版）

```c
/* File: compare_v2.c */
/* cmp_v2_t 追加：cmp_ana_t mode; HWND btn_ana[3]; cmp_image_t *ana[2];
 *               cmp_metric_t met[2]; RECT met_src[2]; int best;
 *   grp 由 [3] 改 [4]。 */

static cmp_image_t *v2_disp(const cmp_v2_t *state, int index)
{
    return (state->mode != CMP_ANA_NONE && state->ana[index]) ?
           state->ana[index] : state->image[index];
}

static void v2_metrics_update(cmp_v2_t *state)
{
    RECT full;
    int k;

    GetClientRect(state->overlay, &full);
    for (k = 0; k < 2; k++) {
        RECT src;
        if (state->mode == CMP_ANA_NONE ||
            !CmpAna_VisibleSrcImage(state->image[k], &state->view[k],
                                    full.right, full.bottom, &src)) {
            ZeroMemory(&state->met[k], sizeof(state->met[k]));
            SetRectEmpty(&state->met_src[k]);
        } else if (state->met[k].mode != state->mode ||
                   !EqualRect(&src, &state->met_src[k])) {
            CmpAna_Measure(state->image[k], state->mode, &src,
                           &state->met[k]);
            state->met_src[k] = src;
        }
    }
    state->best = CmpAna_BestIndex(state->met, 2);
}
```

其餘比照 §8（`sync`／`pan_left`…命名按實碼；`swapped` 經 `v2_image_index`）：
- `v2_set_mode`：先全 Acquire，失敗回退，成功 Release 舊模式。
- `v2_metrics_schedule` 接滾輪、`WM_HSCROLL`（`track[side]`）、平移、Reset、`WM_SIZE`；移線／Swap 不重算。
- `v2_render:414,416` 影像改 `v2_disp(state, v2_image_index(state, side))`（左右＝side 0/1）。
- A:/B: 標籤（`:452`）改 `A: <Metric>`（`_snprintf`，沿用 `%.0f%%` 格式9905），best 對應底片黃字。
- `CompareV2_OpenMode` 建完 fit 後 `mode != NONE` 即 `v2_set_mode`（V1 過來快取命中＋1）。
- `WM_DESTROY`：`KillTimer` → Release×2 → Unref×2。
- `v2_layout`（`:613`）按 R6 四段重算；`WM_GETMINMAXINFO`（`:947`）`ptMinTrackSize.x` 760→900。
- `CMPM_KEY`（`compare_v2.c:1052` 區，`'S'/'M'/'P'` 旁）：`'E'/'T'/'N'`（`!ctrl`）→ `v2_set_mode`。

## 10. 驗收清單（定案版，R7 已修正）

| 項目 | 預期 |
|---|---|
| 同一張圖開兩格 | 數值完全相同，`best=0`（全等取首格） |
| 原圖與高斯模糊版 | Sharp P99 與 Texture 皆原圖較高 |
| 灰卡照片加色偏 | Neutral 方向正確（偏黃 Yellow／偏藍 Blue），高彩度區全黑 |
| Lock 下放大到局部 | 120 ms 後數值變局部結果；拖曳中數值不閃 |
| V1 開 Sharp 後按 V2▶ | 不重算（無等待游標），關 V1 後 V2 正常 |
| 模式切換 20 次後關窗 | Debug `LiveCount==0`，無 `ana_users leak` |
| 800% 邊緣、HALFTONE、分割線 | 與 NONE 一致（`Cmp_Blit` 未改） |
| 100MP 圖 | 超上限提示失敗，模式維持原狀 |
| 解析度不同的 V1 | Sharp/Texture 不標 `*`，Neutral 照常標 |
| Snapshot 分析模式下 | 輸出含分析圖＋既有資訊列（R8） |

## 11. 待確認（發包前請拍板）

| # | 項目 | 建議 |
|---|---|---|
| Q1 | R5（Lab 改 `rgb_to_lab_f`） | 採用（單一真實源；差約 0.1–0.3，不採則 Neutral 與 Report 對不上） |
| Q2 | R1（新增 `OpenMode` 包裝） | 採用（`main.c` 零改動） |
| Q3 | 門檻（C<10、L 8–96、g≥160、×4） | 先沿用；可調另立版本（工具列滑桿約＋80 行） |
| Q4 | Mask 黑 vs 洋紅／棋盤 | 先黑色；要區分另立版本（棋盤要改 `Cmp_Blit`） |
| Q5 | Texture 正負方向顯示 | 先絕對值灰階；要中灰 128± 另立版本 |

## 12. 發包清單（gh 執行順序）

1. `compare.h`：§5 全份（含 struct 4 欄、ID、API、`OpenMode` 宣告）。
2. `compare_image.c`：§6（`Adopt`＋`Unref` 連帶）。
3. `compare_analysis.c`：§7（提案 §7 照抄＋4 處强制修改＋`hist` 改 `calloc`＋`_snprintf`）。
4. `compare_v1.c`：§8（struct 追加、三鈕、`v1_set_mode`／`update`／`schedule`／`disp`、接線 8 處、render 兩處、`v1_open_v2`）。
5. `compare_v2.c`：§9（struct 追加＋`grp[4]`、群組＋三鈕、`layout` 重算、min 寬 900、接線、render 兩處、`OpenMode` 實作）。
6. CMake 追加 `compare_analysis.c`；`#include "analyze.h"`（`compare_analysis.c`）。
7. 建置零警告 → 手測 §10 → commit（gh 不 push；commit 訊息：`compare analysis modes v1.0 (Sharp/Texture/Neutral)`）。

gh 只實作不 commit 以外的事：不動 `main.c`、不動 `Cmp_Blit`、不動 manifest、不進 Export。
