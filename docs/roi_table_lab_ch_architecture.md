# ROI 表格加 c/h 欄（Lab chroma/hue）架構書 v1.1

> 專案：`C:\Github\roi-analyzer`（純 C ＋ Win32 ＋ GDI，ANSI 全 A 版，`-Wall -Wextra` 零警告，C11，宣告置頂，禁 `//` 註解）
> 基準：`master`（`15242f5`，v3.6）
> 需求：Grid ROI 表格在 `L a b` 之後增加 `c`（chroma）與 `h`（hue）欄；`L` 已有，c/h 由既有 `lab_a/lab_b` 直接導出，不重掃描
> 日期：2026-10-08
> 狀態：**v1.1（併入 agy round-1 三項修正）→ GO，發包 gh**

## 變更歷史

| 版本 | 日期 | 內容 |
|---|---|---|
| v1.0 | 2026-10-08 | 初稿 |
| v1.1 | 2026-10-08 | agy round 1 三項修正：(1) **索引平移敘述錯誤更正**——c/h 接在 `b`(13) 之後，**既有索引 0–13 完全不變**，僅追加 `values[14]/[15]`（原「逐一平移」會造成錯位）；(2) `table.c` 硬編碼 14 共 **4 處**（L67 `values[14][64]`、L80 `i<14` pending 填 `-`、L96 `i<14` 封口、L105 `i<14` SetItemText），非 2 處；(3) `export.c` 無 `fprintf`——實為 `_snprintf` 格式化＋`write_utf16le` 寫入 |

---

## 1. 演算法（分析碼層零掃描成本）

在 `AnalyzeROI` 現有 `rgb_to_lab_f` 之後追加導出（`analyze.c:175-178`）：

```c
out->lab_c = (double)sqrtf(fa * fa + fb * fb);
out->lab_h = (double)(atan2f(fb, fa) * (180.0f / 3.14159265f));
if (out->lab_h < 0.0)
    out->lab_h += 360.0;
```

- `lab_c`（chroma，C*）＝√(a²+b²)，與 `compare_analysis.c` 的 `neu_cast` 同式（§2 單一真實源皆源自 `rgb_to_lab_f` 的 a/b）。
- `lab_h`（hue，h_ab）＝atan2(b,a)，範圍 `[0,360)`；a=b=0 時 atan2f(0,0) 回 0（C 標準定義，無 NaN）。
- **float 計算、double 儲存**——與 `lab_l/a/b` 同慣例。

## 2. 逐檔變更

| 檔案 | 變更 |
|---|---|
| `src/analyze.h` | `roi_result_t` 加 `double lab_c, lab_h;`（置於 `lab_l, lab_a, lab_b` 之後） |
| `src/analyze.c` | `AnalyzeROI` 尾端追加 §1 兩行導出 |
| `src/table.c` | `g_headers[]` 14→16 欄（`…,"L","a","b","c","h"`）；`g_widths[]` 加 `{70,70}`（96dpi 基準，沿用 L/a/b 寬度）；**`append_row` 索引 0–13 全部不變**（c/h 追加在 `b`=13 之後）：僅新增 `values[14]=lab_c`、`values[15]=lab_h`（`%.2f`）；硬編碼 `14` 共 4 處改 16——L67 `char values[16][64]`、L80 `for (i=2; i<16; i++)`（pending 填 `-`）、L96 `for (i=0; i<16; i++)`（封口）、L105 `for (i=1; i<16; i++)`（SetItemText） |
| `src/export.c` | `k_column_header`：`…Ys\tL\ta\tb\tc\th\trect\tcount…`（UTF-16LE＋tab 慣例不變）；資料列（`_snprintf` 格式化＋`write_utf16le` 寫入，**非 fprintf**）在 `clean_zero(r->lab_b),` 後插入 `clean_zero(r->lab_c), clean_zero(r->lab_h),`（欄序與 header 逐欄對齊） |
| `docs/01_architecture_v1.md` | 樹註記＋changelog（v3.7） |

**不動**：`analyze.c` 掃描迴圈、`metrics*`、`compare_analysis.c`、`masks.c`（各自 Lab 用途獨立）。

## 3. 欄位契約變更

| 介面 | 舊 | 新 |
|---|---|---|
| ROI 表格欄 | 14 欄（# Rect Count Rm Rs Gm Gs Bm Bs Ym Ys L a b） | 16 欄（…L a **b c h**——b 後插入兩欄） |
| CSV 匯出 header | `id Rm Rs Gm Gs Bm Bs Ym Ys L a b rect count` | `id Rm Rs Gm Gs Bm Bs Ym Ys L a b c h rect count` |
| 記憶體版控 `roi_result_t` | struct 尾端追加兩 double（舊呼叫者全數以欄位名存取，無二進位相容問題） | — |

**CSV 舊檔**：各檔自帶 header，Excel 開舊檔不 受影響；新舊檔不可直接欄位比對（文件註記）。
**`table.c` 索引風險**：`append_row` 的數值填寫依硬編碼索引（`values[10]=y_std…`），gh 必須逐一平移，並確認 `Table_RebuildState`/`Table_FindRow` 的 `lParam` 機制不受欄數影響（不受——`lParam` 綁 global_index）。

## 4. 驗收清單

| # | 項目 | 預期 |
|---|---|---|
| T1 | 表格 | 16 欄，`c`/`h` 值與 `√(a²+b²)`/`atan2` 手算一致（抽 1 格驗算） |
| T2 | h 範圍 | 負 a 正 b → 90–180；正 a 正 b → 0–90；b<0 → >180；恆 `[0,360)` |
| T3 | CSV | UTF-16LE＋BOM＋tab；Excel 雙擊分欄 16 欄、中文路徑正常 |
| T4 | 空 ROI | `analysis_pending` 時 c/h 與其他欄同為 `-` |
| T5 | 回歸 | `ctest` 2/2、零警告、既有 14 欄值不變 |

## 5. 待確認（agy review 重點）

| # | 議題 | 本書立場 |
|---|---|---|
| Q1 | c/h 用 float 計算是否夠準 | 夠——來源 a/b 本身是 float 運算結果；double 化只有偽精度 |
| Q2 | h 是否要 8 分區名稱（Red/…/Magenta） | v1 不做（`CmpAna_HueName` 在 compare 模組，跨模組引用達反 compare.h 獨立性）；另案 |
| Q3 | 欄序插在 b 後 vs 表尾 | b 後（Lab 群組連續性）；文件同步 |
| Q4 | `metrics`/`masks` 的 Lab 是否也加 | 不做（各有 `mean_lab_l` 體系，範圍外） |
