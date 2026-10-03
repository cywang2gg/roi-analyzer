<!-- File: docs/cc_patch_locate_architecture.md -->
# 色卡 24 Patch 定位＋數據擷取 — 架構書 v1.2

> 專案：`C:\Github
oi-analyzer`（純 C ＋ Win32 ＋ GDI ＋ WIC，ANSI 全 A 版，`-Wall -Wextra` 零警告，C11）
> 基準：`master`（v3.3，P2 已合併）
> 輸入資產：`C:\Users\fr583\OneDrive - 國立中央大學\文件\GitHub\ColorChartv2\偵測定位架構移植書.md`（Python 側已定稿，以下簡稱「移植書」）
> 來源（Python 原版，以原版為準）：`ColorChartv2/ColorImageComparator_v8-2_d65.py`（2452 行，working logic）
> 精簡版（有落差，勿抄）：`ColorChartv2/src/image/grid_detector.py`（`_refine_axis` 缺缺口填補／forced_spacing／typical<=5 保護，見 §4.4）
> 日期：2026-10-03
> 狀態：**v1.2 定稿（P1+P2+P3 已實作並手測通過；灰階編號判定待 P4）**

---

## 修訂紀錄

| 版本 | 日期 | 內容 |
|---|---|---|
| v1.0 | 2026-10-03 | 初稿。底稿為移植書 §1–§10；Stage A 沿用既有 `yolo_post.c`（P0/P1 已驗 IoU 0.9983，不重寫）；ONNX layout 已實測確證（§2.1）；OpenCV 選型列三方案待拍板（§1 D2、§11 Q1） |
| v1.1 | 2026-10-03 | 合併兩輪 review（R1–R9、M1–M11）：`CC_INSET_RATIO 0.3f`＋`np_slice`（R1/R8）；直擺 v1 判失敗＋編號固定橫擺 row-major（R2/Q7）；統計一律縮圖＋原圖映射僅供顯示（R3a/M2）；WIC 解碼為準不套 EXIF（R4/R9）；§4.4 邊界條件＋reason 列舉（R5）；§9 重寫為唯一 JSON golden＋注入式比對（R6/R7/M7/M10/M11）；容差表明確（M8）；§10 P4 具體化（M9）；L7/L8 新增；Q7–Q10 新增 |
| v1.2 | 2026-10-03 | **P1+P2+P3 實作完成並手測通過**（`2.jpg` 正確出 24 格）。本版修訂：(1) 第三輪 review 4 項必修——§7 改 **LoadLibrary 動態載入**（非 import lib，缺 DLL 不影響主程式）、blur 邊界 **REFLECT_101 外擴**修正（`cvSmooth` 內部為 `BORDER_REPLICATE`，與 Python 不同）、**版本凍結** `third_party/opencv-4.12/`、P2 縮為窄版；(2) Q7 **翻案為支援直擺**——根因查證為原版 `extract` 用動態格數（直擺跑 6×4）而 C 端寫死 5/7＋4×6 迴圈，已修：`cc_grid_refine` 輸出 `nh,nv,orient`、迴圈依 `(nh-1)×(nv-1)`、`dim` 不隨 target 交換；新增 `cc_canon_id` 四旋轉標準編號換算；(3) 灰階列判定（chroma＋monotone）暫緩至 P4（需 Lab stats 前置＋golden 調參）；(4) `dim/target` 查證確認（僅 fallback，非間距公式）；(5) `-isystem` 真正原因更正為抑制第三方標頭的 `-Wall -Wextra` 警告（非斜線方向）；(6) ABI 疑慮標為已排除 |

---

## 0. 背景與目標

v3.3 已有 YOLO 色卡框偵測（P1＋P2，洋紅框＋狀態列）。本版往下一層：**對偵測框內的色卡，定位出 24 個 patch 的 rect，在影像上疊加顯示（overlap），並擷取每 patch 數據**。

行為需求：

1. 偵測到色卡框（DETECT_DONE 且 count≥1）後，對 top-1 `color-chart` 框執行定位流程（Stage B→C→D），不阻塞 UI（沿用常駐 worker）。
2. 成功時在影像上疊加 24 個 patch rect（與洋紅偵測框不同圖層／顏色），row-major 編號 1–24 顯示於格角。
3. 每 patch 輸出一筆數據：`meanBGR、Y mean/std、Lab mean（sRGB D65_Native）`，供後續 ΔE／報表使用（報表本身不在本版範圍）。
4. 任一步失敗 → 回退手動 4 角點模式（§6），不當機、不污染既有 ROI／Export。

本版範圍（v1）：定位、疊加顯示、數據擷取、設定、狀態列提示。
不在本版範圍：ΔE 計算、報表／CSV 匯出 patch 數據、依 patch 自動建立 ROI、GPU 加速。

---

## 1. 前提與限制

| # | 限制 | 對設計的影響 |
|---|---|---|
| L1 | 專案純 C＋MinGW；第三方為 **OpenCV 4.12 C API（LoadLibrary 動態載入，版本凍結於 `third_party/opencv-4.12/`）** 與 ORT（動態載入） | 移植書 §7 的 OpenCV 函式可用，但**不得連結 import lib**（缺 DLL 會使 exe 無法啟動），一律走 `GetProcAddress`；見 D2 |
| L2 | ANSI A 版 API | 新 UI 字串沿用既有中文 UI 做法（ANSI A 版） |
| L3 | `-Wall -Wextra` 零警告 | 新檔宣告置頂、C89 風格相容、禁 `//` 註解（沿用專案慣例） |
| L4 | 主程式 x86-64 | OpenCV DLL 必須 x64（同 ORT 前例） |
| L5 | 支援 0° 與 ±90° 色卡；大角度旋轉與梯形不在自動流程內 | v1.2 起直擺亦支援（Q7 翻案）；梯形僅手動備援可處理 |
| L6 | YOLO 框只取 1 個；多色卡同框不支援；`122` 類一律忽略 | 沿用移植書 §10 與既有 `yolo_decode_v8`（只取 class 1） |
| L7 | C 端不套用 EXIF orientation（WIC 管線現況） | 含 EXIF 旋轉的直拍照片，經 WIC 解碼後呈直擺；v1.2 起直擺可正常定位（Q7 翻案），編號以 `cc_canon_id` 換算（灰階判定待 P4） |
| L8 | std 類指標（`y_std`／`l_noise`）不具跨解析度可比性 | 縮圖統計（R3a 定案）：INTER_AREA 本身是低通濾波，偏小程度隨原圖解析度改變；同解析度下可比，跨解析度僅供參考 |

### 設計決策（待拍板者標 ☆）

| # | 決策 | 內容 | 理由 | 狀態 |
|---|---|---|---|---|
| D1 | Stage A 沿用既有推論鏈 | `yolo_post.c`（letterbox＋`[1,6,8400]` 解碼＋NMS）＋`detect_worker.c`（常駐 worker）直接用，不重寫；定位流程吃「top-1 `color-chart` 框」 | P1 T11 端到端 IoU=0.9983 已驗；移植書 §2.1 的 layout（`images→output0`）與現況完全一致（§2.1 實測） | 已定 |
| D2 | 影像運算庫：MinGW 直接連結 MSYS2 ucrt64 OpenCV 4.12 C API（動態連結） | 實測確證：`core_c.h`／`imgproc_c.h` 可用，純 C 直接呼叫＋連結，免 MSVC shim、免 C++；部署只需 `libopencv_core-412.dll`（5.5MB）＋`libopencv_imgproc-412.dll`（7.5MB）放 exe 同目錄。注意：include 須寫 `-isystemC:/…`（連著寫；`-I…` 斜線方向會失效）；`cvHoughLines2` 為 10 參數（補 `min_theta=0`、`max_theta=CV_PI`）；`imgcodecs_c.h` 已拔（`cvLoadImage` 無）——解碼沿用 WIC，不受影響 | 已定（Q1） |
| D3 | Lab 公式不重寫 | 複用 `analyze.c` 既有 Lab（sRGB→XYZ D65→Lab），以 V7 式比對（容差 0.05）確認與 `rgb_to_lab_custom(D65_Native)` 一致 | 同源公式，不應有兩份 Lab 實作 | 已定 |
| D4 | Y 計算沿用既有權重 | `Y = 0.114B + 0.587G + 0.299R`（移植書 §5，與 `analyze.c`/BT.601 同源）；y_std 為 float32 逐像素標準差 | 與 Grid 表格同源，避免兩套 Y | 已定 |
| D5 | 內縮比寫死 `CC_INSET_RATIO 0.3`（double，不加 `f`） | 移植書 §5 註解殘留 `/2.5`（保留 20%）與實際 `10/3`（保留 40%）矛盾，以後者為準；**C 端以 double 常數表達（R1：C 的 `10/3` 是整數除法 =3；且 float `0.3f` 轉 double 為 0.3000000119，使 `(int)(10-10*0.3f)=6` 而非 Python 的 7——test_locate 已驗，必須用 double `0.3`）** | 已定 |
| D6 | 精煉以原版為準 | `refine_axis`／`grid_refinement` 以 `ColorImageComparator_v8-2_d65.py:608-661` 為準，**不抄** `grid_detector.py` 精簡版 | 精簡版缺缺口填補／forced_spacing／typical<=5 保護（移植書 §4.4 已標註） | 已定 |
| D7 | 疊加圖層順序 | 影像 → 洋紅偵測框 → **24 patch rect（青色）＋編號** → ROI 框（最上層） | ROI 是使用者操作主體，永遠最上 | 已定 |

---

## 2. Stage A：YOLO 取框（沿用，不重寫）

### 2.1 ONNX layout（2026-10-03 實測確證，移植書 §2.2 的 ⚠️ 已解除）

以 `c-vlcplayer/.venv_yolo`（UL 8.4.171）載入 `models/color_chart.onnx` 印出：

- `IN: images [1, 3, 640, 640]`；`OUT: output0 [1, 6, 8400]`；`opset: ai.onnx 18`
- 與 P0（§3.3 of 偵測架構書）及 `yolo_ort.c` 寫死的形狀檢查完全一致

結論：**移植書 §2.2 的 layout 警告解除**，`output0` 即標準 YOLOv8 detect head（`[cx,cy,w,h,s0,s1] × 8400`，通道優先 `out[c*N+i]`），C 端沿用 `yolo_decode_v8`（只取 class 1，`122` 捨棄，P1 已修為 class-1 分數）。

### 2.2 取框規則

- conf ≥ 0.25、IoU NMS 0.7（ultralytics 預設，與既有 `YOLO_DEFAULT_*` 一致）。
- NMS 後取**最高分**的 `color-chart` 框 1 個，輸出原圖座標系 `xc, yc, w, h`（像素）。
- 無框 → 整個定位流程不啟動（維持現有 `DETECT_DONE(0)` 顯示）。

---

## 3. Stage B：裁切＋縮放（`cc_crop_scale`）

對應移植書 §3（`ChartDetector.detect_and_crop`，原版 `analyze_image_with_working_logic:738-755`）：

```c
w_s = w * 1.2f;  h_s = h * 1.2f;            /* scale_factor = 1.2 */
x1 = clamp(xc - w_s/2, 0, W-1);  y1 = clamp(yc - h_s/2, 0, H-1);
x2 = clamp(xc + w_s/2, 0, W);    y2 = clamp(yc + h_s/2, 0, H);
crop = img[y1:y2, x1:x2];                       /* 空圖 → 整張失敗（reason=crop_empty），回退 §6 */
if (long_side > 400)                            /* max_dimension = 400 */
    等比縮放長邊至 400（INTER_AREA，手寫需處理邊緣像素部分覆蓋權重；D2-A 則手寫 area 平均）;
```

- 記住 `(x1, y1)` 與縮放比 **`rx = new_w/crop_w`、`ry = new_h/crop_h`**（M2：縮放後整數化使兩軸比不完全相等，ROI 僅 ~22px，必須分開存）。
- 本 stage 無 OpenCV 強依賴（crop 是指標＋pitch 運算；resize area 可手寫 ~60 行＋邊緣權重），**resize 列為 P1 可先行部分**（M9）。

---

## 4. Stage C：網格線偵測（核心）

輸入：Stage B 的 ≤400px BGR。對應原版 `extract_patches_from_grid:663-686`，以原版函式為準。

### 4.1 邊緣＋線段（原版 666–667）

```
gray = BGR2GRAY
blur = GaussianBlur(gray, 5x5, sigma=0)
edge = Canny(blur, 50, 150)
segs = HoughLinesP(edge, rho=1, theta=1°, threshold=40, minLineLength=30, maxLineGap=10)
```

### 4.2 H/V 分類（原版 670–674，`angle = deg(atan2(dy,dx))`）

- 水平：`|angle|<15` 或 `||angle|-180|<15`
- 垂直：`||angle|-90|<15`
- 其餘丟棄

### 4.3 合併（原版 `merge_lines:584-596`，spacing_threshold=25）

- 位置：水平線取 `(y1+y2)/2`，垂直線取 `(x1+x2)/2`（原版 `get_line_position:580-582`）。
- 排序後聚類：`|pos - mean(cluster)| < 25` 併入，否則結算上一簇（輸出簇平均）。

### 4.4 精煉（原版 `refine_axis:608-648` ＋ `grid_refinement:650-661`，D6 以此為準）

1. 方向判定：`cost1=|H-5|+|V-7|`、`cost2=|H-7|+|V-5|`；`cost2<cost1` 則 H/V 目標互換。**`cost1==cost2` 平手時維持預設（H=5、V=7）**；**互換發生（直擺）時 v1.2 起支援：目標改為 H=7、V=5，`dim` 不隨 target 交換**（H 軸的 `dim` 永遠是影像高度，V 軸永遠是寬度——此為排除根因 C 的關鍵）。編號以 `cc_canon_id` 換算為標準序（§5.1a）。
2. `typical = median(diff(pos))`；`typical<=5` 改用 `dim/target`；`is_good = (span > 0.5*dim) && (count>2)`。
   - **`count<2` 時 `diff` 為空**：`typical` 取 `dim/target`，`is_good=false`（R5：numpy 回 NaN＋警告，C 端明定不走 NaN 路徑）。
   - **`dim/target` 查證狀態**：原版第 625 行寫 `image_dim / target_count`（非 `dim/(target-1)`），5 條線 4 個間距的直覺是錯的——此處是「無資料時的 fallback 估計」不是間距公式，故 `dim/target` 正確（待 P0 harness 實測複驗，若不符再修）。
3. 好軸 spacing 借給壞軸（`forced_spacing` 跨軸借用；兩軸皆壞則無借用）。
4. 缺口填補：任一 `gap > 1.5*typical` 則均勻插入 `round(gap/typical)-1` 條。
5. 數量不足：比較首線離邊距 vs 末線離邊距，大者那側按 `mean(diff)` 外推（`count<2` 無 `mean(diff)` 時用 `typical`）。
6. 數量過多：以首末中點為中心，逐條丟離中心遠的那端。
7. 目標：橫擺水平 5 條、垂直 7 條；直擺水平 7 條、垂直 5 條。**失敗條件（皆回 `reason` 字串，對應 §8 狀態列 `Locate failed: reason`）**：
   - 精煉後仍不足目標數（典型：兩軸皆壞且 `count<=1`，外推無基準）：`reason=no_grid`
   - 空輸入（segs 為空且無 forced 可用）：`reason=no_lines`
   - OpenCV DLL 無法載入：`reason=cv_unavailable`（§7 D2）

> 精簡版落差（備查）：`grid_detector.py:_refine_axis` 缺第 3、4 步及 `typical<=5` 保護，C 移植勿抄。

---

## 5. Stage D：24 patch ROI＋數據（`cc_extract_rois`＋`cc_roi_stats`）

對應原版 `extract_patches_from_grid:687-735`＋`analyze_image_with_working_logic` patch 迴圈。
**本節所有座標皆為 Stage-B 縮圖座標；統計一律在縮圖上算（R3a 定案，與 Python 一致）**。

### 5.1 ROI 幾何（D5：`CC_INSET_RATIO 0.3` double）

```c
#define CC_INSET_RATIO 0.3   /* =1/(10/3)，每邊內縮 30%，保留中心 40%。必須是 double（不可加 f）：float 0.3f 轉 double 為 0.3000000119，使 (int)(10-10*0.3f)=6 而非 Python 的 7（test_locate 已驗） */

/* 模擬 numpy 1-D 切片 [start:stop]（step=1），回傳實際長度，<=0 表空（R8） */
static int np_slice(int start, int stop, int len, int *out_start)
{
    if (start < 0) start += len;
    if (stop < 0) stop += len;
    if (start < 0) start = 0;
    if (stop < 0) stop = 0;
    if (start > len) start = len;
    if (stop > len) stop = len;
    *out_start = start;
    return stop - start;
}

h[5] 排序， v[7] 排序;                       /* 不足 5/7 → 本張失敗（reason=no_grid） */
id = 1;
for r in 0..3:
  for c in 0..5:
    xl=v[c], xr=v[c+1], yt=h[r], yb=h[r+1];
    /* float→int 規則（R1）：原版先 float 運算再 int() 截斷（非四捨五入），C 端用 (int) 截斷 */
    ix1 = (int)(xl + (xr-xl) * CC_INSET_RATIO);
    ix2 = (int)(xr - (xr-xl) * CC_INSET_RATIO);
    iy1 = (int)(yt + (yb-yt) * CC_INSET_RATIO);
    iy2 = (int)(yb - (yb-yt) * CC_INSET_RATIO);
    /* 經 np_slice 判定空（外推線 <0 或 >dim 時觸發，見 R8）；空則該 slot valid=false */
    patch_id = id++;                         /* row-major: id = r*6+c+1（橫擺影像序，不保證對應 ColorChecker 標準序，R2） */
```

### 5.2 有效性門檻

- 要求有效 ROI **≥ 23 個**（`valid_count >= min_rois`，`min_rois=23` 寫死，Q5），即 `CcRoi rois[24]` 固定陣列＋`valid` 旗標（m5：overlay 與後續 ΔE 依 id 索引，不另設第二陣列）。
- 原版 `patch_counter` 只在非空時 append 但計數照加 → C 端同語義（id 照跳號，空 slot 留 `valid=false`）。
- 24 ROI 座標保留 Stage-B 圖座標；另存映射回原圖座標（§5.3，**僅供顯示**，統計不用原圖值）。

### 5.1a 直擺與標準編號換算（v1.2 新增）

`cc_grid_refine` 輸出 `h_target`／`v_target`（橫擺 5/7、直擺 7/5）；`cc_extract_rois` 依 `(h_target-1) × (v_target-1)`＝4×6 或 6×4 跑雙迴圈，格 id 為該方向的 row-major（`r*cols+c+1`），**影像格位與標準序不同**。標準序換算由 `cc_canon_id(img_row, img_col, rows, cols, rot)` 負責：

| rot | 意義 | 對應 |
|---|---|---|
| 0 | 正立 | `r=img_row`, `c=img_col` |
| 1 | 順時針 90° | `r=3-img_col`, `c=img_row` |
| 2 | 180° | `r=3-img_row`, `c=5-img_col` |
| 3 | 逆時針 90° | `r=img_col`, `c=5-img_row` |

回傳標準 id 1–24，超界回 -1。橫擺（rot 0）時 `cc_canon_id` 即恆等映射。

- **v1.2 實作範圍**：`cc_canon_id` 已實作並通過單元測試（`test_canon_id`）。**rot 判定（自動判斷順時針／逆時針）尚待 P4**：需仰賴每格 Lab 統計（`chroma`＋`L` 單調性），而 Lab 目前未接（§5.4 前置）。v1.2 直擺可正常定位與顯示，編號暫為影像序。
- **P4 判定的作法（設計保留）**：標準序第 4 列（id 19–24）為白到黑中性灰。對 rot 1／rot 3 兩候選分別計算該列 `chroma` 總和，取較小者；成立條件為 `monotone>=4`（L 單調遞減次數）且另一候選分數至少大 1.5 倍，否則回報 `orient_ambiguous`。已有 ≥23 有效格時，灰階列缺 1 格不影響判定。
- **色卡型號假設（Q13）**：灰階列判定只適用 ColorChecker Classic 24；其他 24 色卡需另案。

### 5.3 原圖映射（僅供顯示）

`orig_x = stageB_x / rx + x1`，`orig_y = stageB_y / ry + y1`（§3 記下的 origin＋`rx,ry`）。疊加顯示用原圖座標經既有「影像→視窗」轉換繪製（同偵測框做法）。**統計一律用縮圖值，不用映射後值（R3a）**。

### 5.4 每 ROI 數據（定位輸出到此為止）

```
meanBGR = mean(roi)
Y： y_mean = mean(0.114B+0.587G+0.299R)，y_std = std（float32 逐像素，D4）
Lab：沿用 analyze.c（D3；sRGB 反 gamma c<=0.04045 ? c/12.92 : ((c+0.055)/1.055)^2.4 →
      線性 RGB → sRGB→XYZ(D65) 矩陣 ×100 → 除白點 (95.047,100,108.883) →
      f(t) 立方根段 (δ=6/29) → L=116fy-16, a=500(fx-fy), b=200(fy-fz)）
l_star/a_star/b_star = mean(Lab); chroma=sqrt(a²+b²); l_noise=std(L)
```

- sRGB→XYZ(D65) 矩陣（原版 `rgb_to_lab_custom:505-509`）：`[[0.4124564,0.3575761,0.1804375],[0.2126729,0.7151522,0.0721750],[0.0193339,0.1191920,0.9503041]]`；白點 D65_Native（`D50_Adaptive` 的 Bradford 不移植，v1 只做 D65）。
- 記憶體：ROI 逐個計算，不整圖轉 Lab（沿用原版註解做法）。
- **M3 三確認**：(a) `analyze.c` 必須是**逐像素轉 Lab 再平均**（原版做法；若是先平均 RGB 再轉則重寫，且 `l_noise` 無法得出——P0 前置檢查）；(b) 全部 std 為母體標準差（ddof=0，numpy 預設）；(c) 累加精度用 `double`（Python float32 有 pairwise summation，C 用 double 才能進 0.05 容差）。

---

## 6. 手動備援（自動失敗時）

對應移植書 §6。原版 `out/ColorChartAnalyzer.py`：使用者點 TL→TR→BR→BL，以 `getPerspectiveTransform(src=[0,0,1,0,1,1,0,1], dst=四角)` ＋ 24 中心 `(c+0.5)/6,(r+0.5)/4` 經 `perspectiveTransform`（可處理梯形）。

- C 移植用**透視變換版**；ROI 取中心＋§5.1 內縮法（二選一，預設透視＋§5，依移植書 §6 建議）。
  具體演算法（M4）：**在單位正方形空間計算每格內縮後的 4 角 → 經 H 映射回原圖 → 取多邊形內的原圖像素做統計（point-in-quad mask）**。不經重取樣，不需雙線性插值，`y_std` 不被插值平滑（與 R3a 一致）。手動模式 patch 編號由點選順序 TL→TR→BR→BL 決定，與自動模式編號語義（R2 橫擺 row-major）對齊方式待 Q2 定案。
- 透視求解需 8×8 線性方程（高斯消去 ~80 行純 C）；D2-A 內含（point-in-quad 不需取樣器），D2-B/C 另議。
- 手動點選 UI（4 點採集流程）屬 P3 工作，見 §10 階段切分（**若 Q2=延後，P3 同步刪除手動 UI**，M9）。

---

## 7. C 模組切分與依賴

```c
cc_yolo_box(...) -> CcBox xc,yc,w,h        /* 沿用 yolo_decode top-1，不新增（D1） */
cc_crop_scale(img, box, 1.2f, 400) -> cropped + origin{x1,y1,rx,ry}   /* §3，純計算＋area resize */
cc_gray_blur_canny(gray) -> edge            /* §4.1（D2 決定實作方；定點權重＋REFLECT_101＋L1，見 M1） */
cc_hough_lines_p(edge) -> segs (上限 4096)  /* §4.1（D2 決定實作方；m4 上限防暴記憶體） */
cc_merge_positions(segs, horizontal, 25.0f) -> float[]                 /* §4.3，純計算 */
cc_refine_axis(pos, target, dim, forced) -> float[target]             /* §4.4，純計算 */
cc_grid_refine(h, v, shape) -> h[5], v[7], swapped                   /* §4.4，純計算；swapped=true 即 portrait（R2） */
cc_extract_rois(img, h[5], v[7]) -> CcRoi rois[24] (+valid)           /* §5.1–5.3，純計算 */
cc_roi_stats(roi) -> {meanBGR, y_mean, y_std, L, a, b}                /* §5.4（Lab 沿用 analyze.c；double 累加） */
cc_locate_run(...) -> orchestration（worker 內，仿 detect_worker 模式）/* §8 */
cc_overlay_draw(...) -> 24 rect＋編號（canvas.c 內，仿 draw_detection_overlay）/* §8 */
```

單一結構（m12）：`CcRoi {int id; int x1,y1,x2,y2; BOOL valid;}`＋`CcPatchStats {int id; double b,g,r,y_mean,y_std,L,a,b_star,chroma,l_noise;}` 以 id 對齊，不設第二陣列。

### D2 選型（已定案，Q1=OpenCV；v1.2 改為 LoadLibrary 動態載入）

v1.0 的 A/B/C 三方案作廢。實測確證 MSYS2 ucrt64 有 OpenCV 4.12（MinGW 建置），C API 可直接用：

| 項目 | 結論 |
|---|---|
| 標頭 | `core_c.h`／`imgproc_c.h`（`imgcodecs_c.h` 已拔，無 `cvLoadImage`——解碼沿用 WIC） |
| 載入方式 | **`LoadLibraryA`＋`GetProcAddress` 動態載入**（v1.2 修訂）。不連結 import lib：只要少一個 DLL，import lib 會讓 **exe 無法啟動**，連既有 ROI／Export 也一起失效，違反需求 4。C API 為 `CVAPI`＝`extern "C"`，符號無 mangling，可直接 `GetProcAddress`。缺 DLL 時定位功能停用（reason `cv_unavailable`），其餘功能照常 |
| 版本凍結 | **`third_party/opencv-4.12/`**（標頭＋2 個主 DLL＋`ldd` 相依閉包）。msys2 為滾動更新，`pacman -Syu` 升版會使 DLL 名由 `-412` 變 `-413` 而載入失敗；OpenCV 5.x 更會移除 C API。harness 用 `opencv-python==4.12.0.*`，版本寫入 JSON header |
| 部署 | `libopencv_core-412`／`imgproc-412`＋`libgcc_s_seh-1`／`libwinpthread-1`／`libstdc++-6`／`libtbb12`／`zlib1`（`ldd` 完整閉包，共 7 個） |
| 必須修正 | `cvSmooth(CV_GAUSSIAN)` 內部為 `BORDER_REPLICATE`，Python `cv2.GaussianBlur` 預設為 `BORDER_REFLECT_101`，外圍 2px 不同。修法：手動 2px REFLECT_101 外擴再取中心（不用 `cvCopyMakeBorder`，避免多一次整圖拷貝；ABI 疑慮已排除） |
| 其他 | include 用 `-isystem`（真正作用是抑制第三方標頭的 `-Wall -Wextra` 警告，非斜線方向）；`cvHoughLines2` 為 10 參數 |
| CRT | 專案與 OpenCV 皆 ucrt64；明文規定不可跨邊界 `free`（記憶體一律由配置者釋放） |

| 方案 | 說明 | 優點 | 缺點 |
|---|---|---|---|
| **A. 手寫純 C（推薦）** | gray（10 行）＋Gaussian 5×5（40 行）＋Canny（~200 行）＋HoughLinesP 機率版（~300 行）＋resize AREA（~60 行）＋透視求解（~80 行，僅手動備援） | 零新依賴；與純 C 約束一致；ORT 前例證明自研可行 | HoughP 工作量最大；需 golden test 保行為一致（§9） |
| B. OpenCV MSVC DLL＋C-ABI shim | 官方 `opencv_worldXXX.dll`（x64）＋自製 MSVC shim（C 匯出）＋exe 側 `LoadLibrary`（仿 ORT 模式） | 演算法與 Python 完全一致 | 需 MSVC 工具鏈建 shim；部署多 ~50MB；shim 也是新維護面 |
| C. MinGW 自建 OpenCV | 從源碼以 MinGW 建置 | 無 ABI 問題 | 建置重（數小時）＋體積大＋升級負擔；與「無第三方依賴」背離最遠 |

### M1 一致性判準（D2-A 目標定義）

方案 A 不追求與 OpenCV 線段逐條一致（做不到也不必要：HoughP 用 `cv::RNG((uint64)-1)` MWC 決定取點順序，edge 差 1px 即改變線段集合）。判準放在 **grid 層**：`h[5]/v[7]`（Stage-B 座標，排序後逐線 max abs diff）**≤1px**，＋24 ROI 的 Lab/Y 容差（§9 M8 表）。

降低 edge 差異的定點做法（成本不高，建議實作）：`cvtColor` 定點權重 `(R*4899+G*9617+B*1868+8192)>>14`；`GaussianBlur(5x5,sigma=0)` 8U 核 `[1,4,6,4,1]/16`＋`BORDER_REFLECT_101`；`Canny` 預設 `apertureSize=3`、`L2gradient=false`（L1 範數）、hysteresis 8 連通。HoughP 可選移植 MWC 公式（`state=(uint64)(uint32)state*4164903690U+(state>>32)`）追求近似一致（可選項）。

---

## 8. 執行緒／UI 整合（仿 P2 模式）

- 定位流程跑在**同一常駐 worker**（`detect_worker.c` 擴充狀態：DETECT_DONE 後接 LOCATE_RUNNING→LOCATE_DONE／LOCATE_FAILED；或另起 `cc_worker`——實作時二選一，建議前者，省一條執行緒）。
- 觸發點：`Detect_OnResult` 成功且 count≥1 → 送 locate job（含 top-1 框＋影像世代 seq）。**UI 端先 crop（§3）再送 job**（仿 letterbox 模式，job 內只有 ≤400px 副本），worker 不碰 `g_app.img`（m2 的「待定」已定案，刪）。
- 取消：沿用 seq 機制（換圖即丟棄）；locate 中換圖同 §7 多層取消。
- LOCATE 結果（24 ROI＋stats）綁定影像 seq 存於 `g_app`（m6），換圖時清除（`Detect_OnImageUnloading` 一併清）。
- 顯示：`canvas.c` 加 `cc_overlay_draw`（青色 `RGB(0,255,255)` 1px **＋1px 黑色外框（雙線，m1：Cyan patch 上可見）**＋編號文字加背景；與洋紅偵測框、ROI 框圖層見 D7）；狀態列複用訊息區（`Located 24 patches (xx ms)`／`Locate failed: reason`）；選單 View 加「Show Patch Grid」開關（IDM 待定，避開 167/168）。
- 設定：`roi_analyzer.ini [locate]`（沿用 `settings.c` 模式）：**只放 `enabled/show_grid`**（M6：`conf/iou` 一律讀 `[detect]`，不存第二份；`inset_ratio/min_rois` 寫死，Q5）。

---

## 9. 移植驗證（golden test，唯一 golden＝§9.3 JSON）

### 9.1 流程（M11＋第 3 點：BMP 中介反轉 R4/Q9）

```
test_locate.c --dump-bmp <input>  →  <name>.bmp（24-bit 無壓縮，4-byte row padding）
        ↓（同一份 BMP，兩端共用，杜絕解碼分叉）
Python harness（import 原版函式，headless dump）→ golden JSON
        ↓
C tests/test_locate.c 讀 JSON 比對（注入式，見 §9.4）
```

- C 端 dump BMP 用 WIC 解碼結果（`g_app.img` 同源管線抽出的 headless 函式），**不經 `cv2.imread`**（R4：EXIF／ICC／16-bit 行為以 WIC 為準；header 記 `decoder=WIC`＋`exif_orientation` 只記錄不套用）。
- 旋轉負例由 BMP 旋轉產生，兩端吃同一份檔案（R9＋第 3 點）。
- Python／OpenCV／numpy 版本寫入 JSON header（M10）；OpenCV 版本必須記錄（HoughP 跨版本可能不同）。

### 9.2 對照源（M7：既有 PNG/CSV 降級）

- `ColorChartv2/test_output/grid_A_*.png`、`report_*_vs_Standard_D65_*.csv`：**人工目視參考**，不用於自動比對（來源不明：可能精簡版產出或 D50 白點，且 PNG 無法精確讀線位）。
- 自動比對**唯一**以 §9.3 JSON 為準。

### 9.3 golden JSON schema（R6：逐 Stage 凍結；範本來自使用者提供的 GMO JSON）

```json
{
  "header": { "tool": "dump_golden_locate.py", "python": "3.x", "opencv": "4.x", "numpy": "1/2.x",
              "decoder": "WIC", "exif_orientation": 1, "source_pt_sha256": "ad6a508c…" },
  "image": "roi_2.jpg",
  "decode": { "w": 3024, "h": 4032, "orientation_applied": false, "sha1_bgr": "..." },
  "stage_a": { "xc": 0.0, "yc": 0.0, "w": 0.0, "h": 0.0, "conf": 0.0 },
  "stage_b": { "x1": 0, "y1": 0, "crop_w": 0, "crop_h": 0, "new_w": 0, "new_h": 0,
               "rx": 0.0, "ry": 0.0, "bmp_sha1": "..." },
  "stage_c": {
    "segs": [[0, 0, 0, 0]],
    "h_merged": [0.0],
    "v_merged": [0.0],
    "orient_swapped": false,
    "h_refined": [0.0],
    "v_refined": [0.0]
  },
  "stage_d": {
    "rois": [{ "id": 1, "x1": 0, "y1": 0, "x2": 0, "y2": 0, "valid": true }],
    "stats": [{ "id": 1, "b": 0.0, "g": 0.0, "r": 0.0, "y_mean": 0.0, "y_std": 0.0,
                "L": 0.0, "a": 0.0, "b_star": 0.0, "chroma": 0.0, "l_noise": 0.0 }]
  }
}
```

- `CroptestSamples/` 檔名皆 `roi_*`（疑似已裁好）：P0 先確認原版對此組是否跳過 Stage A；是則 `stage_a: null`，C 端同路徑（R7）。
- `segs` 全量凍結（含 HoughP RNG 順序）；`stage_b.bmp_sha1` 保證縮圖一致（resize ≤1 LSB，歸 P1）。

### 9.4 注入式比對表（R6：純計算層精確一致，只有 P2 容差）

| 測試 | 輸入（取自 JSON） | 比對 | 所屬階段 |
|---|---|---|---|
| crop/scale | 原 BMP＋`stage_a` | `stage_b`（x1/y1/new_w/new_h）完全一致；縮圖 `bmp_sha1` 一致（≤1 LSB） | P1 |
| merge | `segs` | `h_merged/v_merged` 完全一致（浮點 1e-4） | P1 |
| refine | `h_merged/v_merged`＋`new_w/new_h` | `h_refined/v_refined` 1e-4（含缺口填補／forced／橫擺互換／邊界） | P1 |
| extract | `h_refined/v_refined` | `rois` 完全一致（整數，經 `np_slice`＋`(int)` 截斷） | P1 |
| stats | Python 縮圖＋`rois` | mean 類 ≤0.05；`y_std`／`l_noise` ≤0.05（R3a：縮圖統計必須比，見 M8） | P1（前置：§9.5） |
| edge+hough | `stage_b` 縮圖 | `h_refined` 排序後逐線 max abs diff ≤1px（端到端，M1 判準） | P2 |
| e2e smoke | 原 BMP（YOLO 注入 `stage_a` 框，跳過推論） | 24 格成功、`valid>=23`、mean 容差（M8 端到端列） | P2/P4 |

### 9.5 前置檢查（P1 前置條件，M9）

1. `analyze.c` Lab 一致性：同輸入 vs `rgb_to_lab_custom(D65_Native)` 差值 < 0.05（V7 標準）；超標先修 `analyze.c`，不在定位模組另寫 Lab。
2. `cc_roi_stats` 依賴本項＋R3a 決策；未通過前 P1 stats 測試掛起，其餘照行。

### 9.6 測試集（R9：5 正例＋5 類負例／邊界）

| 案例 | 來源 | 預期 |
|---|---|---|
| 5 正例 | `CroptestSamples/`（roi_1-1、roi_2、roi_20231213_174058、roi_3、rTarget） | 24 格成功，容差見 M8 |
| 無色卡圖 | 另備 | Stage A 0 框，不啟動定位 |
| 直擺色卡（正例旋轉 90°，由 BMP 旋轉，§9.1） | 導出 | **v1.2：成功定位 24 格**；期望標準 id 的 stats 與橫擺 golden 相符（容差採端到端級 Lab ±0.5、Y ±1.0）；rot 判定正確者（P4）另驗；已知差異：Hough 隨機取點在轉置圖不同、`(int)` 截斷在翻轉後不對稱，ROI 邊界可能差 1px |
| 強模糊／低對比 | 另備或導出 | `Locate failed: <reason>`，不當機 |
| 色卡貼邊（框×1.2 越界） | 導出或挑圖 | clamp 正確，外推線觸發 `np_slice` 語義（R8） |
| 極小色卡（crop<100px） | 導出 | 不放大（`long_side>400` 才縮），refine 運作或明確失敗 |

### M8 容差表（依據：內縮中心 40% 區約 9px，平移 2px 仍同 patch，色塊均勻時 mean 變化小）

| 指標 | 注入式（縮圖＋rois） | 端到端 |
|---|---|---|
| 網格線 | —（1e-4 精確） | 排序後逐線 max abs diff ≤2px（Stage-B 座標；P2 內部判準 ≤1px，見 M1） |
| Lab／Y mean | ≤0.05（等同 V7） | ±0.5／±1.0 |
| `y_std`／`l_noise` | ≤0.05（R3a 必須比，連動 A） | ±0.5（參考值，失敗只警告） |
| `chroma` | ≤0.05 | ±0.5 |

### C 端測試執行體（M11）

- 新增 `tests/test_locate.c`：只連結 `cc_*.c`＋`analyze.c`＋WIC 解碼（`--dump-bmp` 模式共用同一解碼函式），不連結 UI；`--dump-bmp` 產 BMP，預設模式讀 JSON 比對。
- JSON 解析用極簡手寫解析器（或 TSV 化降成本，實作時定）；M5 的另一半（QA 可見數據）由本 harness 的比對報表覆蓋；v1 另加 `OutputDebugString` 輸出 24 筆 stats（M5 最小出口，免 UI）。

---

## 10. 實作階段

| 階段 | 內容 | 狀態 |
|---|---|---|
| P0 數據凍結 | `test_locate.c --dump-bmp` 產 BMP → `tools/dump_golden_locate.py`（import 原版函式，headless，不改原版邏輯）凍結 JSON（含 header 版本＋`stage_a:null` 確認，R7/M10）＋`analyze.c` Lab 一致性（§9.5） | **工具鏈已就緒**（`--dump-bmp` 可用）；golden JSON 待產（§9.5 Lab 一致性未驗） |
| P1 純計算核心 | `cc_crop_scale`（含 resize AREA）＋`cc_merge/refine/extract/stats`（§3–§5）＋**直擺泛化**（`cc_grid_refine` 的 `h_target/v_target`、`cc_canon_id`）＋單元測試（`np_slice` 負值／越界、refine 缺口／forced、內縮幾何、`test_canon_id`） | **✅ 完成，`ctest` 全過**；Lab 統計未接（待 §9.5） |
| P2 影像運算層 | `cc_cv.c`：`cc_gray_blur_canny`＋`cc_hough_lines_p`（OpenCV C API）＋`cc_locate_run` 編排 | **✅ 完成**（v1.2 窄版） |
| P3 整合 | worker（共用序列取消）＋`cc_overlay_draw`（青色＋黑外框＋編號）＋View 選單 `Show Patch Grid`＋狀態列＋`[locate]` 設定 | **✅ 完成並手測通過**（`2.jpg` 出 24 格） |
| P4 驗收與收尾 | (a) Lab 複用 `analyze.c` 接上＋§9.5 一致性；(b) 灰階列 rot 判定（§5.1a）；(c) golden JSON 產出與注入式比對；(d) 效能（Stage C+D <50ms）；(e) 手動備援 UI（若 Q2） | 待做 |

**LoadLibrary 動態載入、blur REFLECT_101 邊界修正、`third_party/opencv-4.12/` 版本凍結**三項為 v1.2 修訂項，與 P2 一併實作（見 §7 D2）。

---

## 11. 待使用者確認（v1.2 定案狀態）

| # | 項目 | 結論 |
|---|---|---|
| Q1 | D2 選型 | **定案：MSYS2 ucrt64 OpenCV 4.12 C API，LoadLibrary 動態載入**（v1.2 修訂；v1.0 A/B/C 作廢） |
| Q2 | 手動備援 UI 是否進 v1 | 自動流程＋狀態列提示進 v1；手動點選 UI 延後（P3 已同步刪除） |
| Q3 | patch 數據的下游 | v1 不進 Export；結構 `CcRoi/CcPatchStats[24]` 供後續用；最小出口為 `OutputDebugString`＋harness 報表 |
| Q4 | 青色 overlay＋編號樣式 | 青色 1px＋黑外框＋編號文字背景（已實作，手測通過） |
| Q5 | `inset`／`min_rois` 是否進設定檔 | 寫死 `CC_INSET_RATIO 0.3`（double）／`min_rois=23`；ini 只放 `enabled/show_grid` |
| Q6 | P1 是否現在發包 | **已發包並完成**（P1+P2+P3 全過） |
| Q7 | 直擺色卡處理 | **翻案：v1.2 起支援**（grid 泛化＋`cc_canon_id`）；灰階 rot 判定待 P4 |
| Q8 | 統計在縮圖或原圖上算 | **縮圖（已定案）**，對應 L8；std 必須比 |
| Q9 | EXIF／解碼基準 | **WIC（已定案）**，對應 L7 |
| Q10 | 測試集範圍 | **§9.6 表（已定案）** |
| Q11 | DLL 缺失時定位功能停用 | 缺 OpenCV DLL → reason `cv_unavailable`，狀態列提示，其餘功能照常（v1.2 新增） |
| Q12 | 橫擺 180° 處理 | 待 P4（灰階判定是否套用 rot 0/2；預設只記錄警告不改編號） |
| Q13 | 色卡型號假設 | 灰階判定只適用 Classic 24；待 P4 |

---

## 12. 自我審查紀錄

| # | 檢查項目 | 結果 |
|---|---|---|
| 1 | Stage A 不重寫的依據 | ONNX layout 實測（§2.1）＋T11 IoU 0.9983；移植書 §2.1 letterbox 描述（右下補灰）與既有 `yolo_post.c`（置中補灰）差異不影響 decode（pad 記錄在 transform 內），C 端沿用既有行為 |
| 2 | 移植書 §2.3 反 letterbox pad 註記 | 既有 `LetterboxTf{scale,pad_x,pad_y}` 已是通用式，右下／置中皆可還原，無需改動 |
| 3 | 精簡版落差已標註 | D6＋§4.4；review 時請對照原版行號（584–735） |
| 4 | `/2.5` 殘留已處理 | D5 寫死 double `0.3`（R1 整數除法＋float 精度，test_locate 已驗） |
| 5 | Lab 白點策略 | v1 只做 D65_Native；D50 Bradford 不移植（§5.4） |
| 6 | `122` 類處理 | Stage A 已過濾（D1）；Stage B–D 無類別概念 |
| 7 | 空 ROI 語義 | §5.2 與原版一致（跳號保留，計數門檻 23） |
| 8 | worker 影像存取 | §8 採 UI 端先 crop（仿 letterbox 模式），worker 不碰 `g_app.img` |
| 9 | P1 可先行性 | §4.3–§5 零影像運算依賴；單元測試可用 golden JSON 片段驗證 refine／內縮 |
| 10 | Q1 推薦 A 的理由 | 零新依賴＋ORT 前例；HoughP 雖大但邊界清晰（參數凍結：thr40/minLen30/maxGap10），golden test 可驗 |
| 11 | v1.1：執行緒安全 | job 資料所有權：UI 端 crop 副本所有權轉移給 worker（仿 letterbox job 模式），`free` 責任在 worker；LOCATE 結果所有權轉移給 UI（仿 `WM_APP_DETECT_DONE` lParam 模式）（m11） |
| 12 | v1.1：記憶體上限 | `cc_hough_lines_p` 輸出上限 4096 線段（m4）；640×640 float NCHW 重用 P1 緩衝；≤400px BGR 副本每 job 一份，CancelAll 即釋放 |
| 13 | v1.1：失敗 reason 列舉 | `crop_empty/no_lines/no_grid/model_missing/session_failed/oom/cv_unavailable`（R5＋§4.4.7＋D2）；`portrait` 已刪除（Q7 翻案，直擺正常支援） |
| 14 | v1.1：HoughP RNG 說明 | `cv::RNG((uint64)-1)` MWC 導致線段集敏感，判準放 grid 層（M1）；MWC 移植為可選項（m10） |
| 15 | v1.1：Q1–Q10 與 M8 連動 | Q8/Q9 為已定案（措辭反轉 R4 原建議方向，連動 B）；M8 std 在 R3a 下必須比（連動 A） |
| 16 | v1.2：LoadLibrary＋blur 邊界＋版本凍結 | D2 修訂三項（見修訂紀錄 v1.2(1)）；ABI 疑慮標已排除（`cvCopyMakeBorder` 未採用，理由為避免整圖拷貝） |
| 17 | v1.2：Q7 翻案＋`cc_canon_id` | 根因 B＋D 查證（原版動態格數）；`dim` 不交換（根因 C）；灰階判定待 P4（Q12/Q13）；`2.jpg` 手測 24 格通過 |
