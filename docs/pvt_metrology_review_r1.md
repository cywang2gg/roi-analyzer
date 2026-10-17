### PVT 靶標精定位與量測演算法技術評估報告

本報告針對 [`pvt_locate.c`](file:///C:/Github/roi-analyzer/src/pvt_locate.c)、[`pvt_locate.h`](file:///C:/Github/roi-analyzer/src/pvt_locate.h)、[`detect.c`](file:///C:/Github/roi-analyzer/src/detect.c) 及 [`pvt_target_architecture.md`](file:///C:/Github/roi-analyzer/docs/pvt_target_architecture.md) 進行逐行演算法審查與 ISP 影像品質量測（ISO 12233 / Imatest 標準慣例）驗證。

---

## Q1. 「p1 - p4 pvt-target 的比對有先尋找黑白邊界, 才計算 overshoot?」

> **結論：沒有（NO）。** 演算法並未先在 1D 剖面上定位黑白轉態邊界（transition edge），而是假設切線中點必通過邊界，直接對整條 1D 剖面 `[0 .. len-1]` 取**全域最大值與全域最小值**來計算 overshoot / undershoot。

### 1. 現行實作分析（File:Line 證據）

* **4 條剖面切線定義（弦切線，非過圓心放射線）**：
  * 雖然 [`pvt_locate.h:26`](file:///C:/Github/roi-analyzer/src/pvt_locate.h#L26) 註解寫 `"center outward: left, right, up, down"`、且 [`pvt_target_architecture.md:165`](file:///C:/Github/roi-analyzer/docs/pvt_target_architecture.md#L165) 寫「過質心的水平線與垂直線」，但實際實作於 [`pvt_locate.c:670-715`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L670-L715) 已改為**離圓心 $0.5 \cdot r_{\text{eq}}$ 的 4 條正交弦線（chords）**，橫跨象限間的垂直與水平黑白接縫（seam）：
    * `span = 1.2 * radius`（切線總長 $1.2 r_{\text{eq}}$，由 $-0.6 r_{\text{eq}}$ 取樣至 $+0.6 r_{\text{eq}}$，[`pvt_locate.c:670-691`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L670-L691)）。
    * **Cut 0**（[`pvt_locate.c:692-697`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L692-L697)）：$y = c_y - 0.5 r_{\text{eq}}$，$x \in [c_x - 0.6 r_{\text{eq}}, c_x + 0.6 r_{\text{eq}}]$（上半部水平切過垂直黑白接縫）。
    * **Cut 1**（[`pvt_locate.c:698-703`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L698-L703)）：$y = c_y + 0.5 r_{\text{eq}}$，$x \in [c_x - 0.6 r_{\text{eq}}, c_x + 0.6 r_{\text{eq}}]$（下半部水平切過垂直黑白接縫）。
    * **Cut 2**（[`pvt_locate.c:704-709`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L704-L709)）：$x = c_x - 0.5 r_{\text{eq}}$，$y \in [c_y - 0.6 r_{\text{eq}}, c_y + 0.6 r_{\text{eq}}]$（左半部垂直切過水平黑白接縫）。
    * **Cut 3**（[`pvt_locate.c:710-715`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L710-L715)）：$x = c_x + 0.5 r_{\text{eq}}$，$y \in [c_y - 0.6 r_{\text{eq}}, c_y + 0.6 r_{\text{eq}}]$（右半部垂直切過水平黑白接縫）。
    * *註：在半徑 $r$ 的圓中，距離圓心 $0.5r$ 處的弦半長為 $\sqrt{r^2 - (0.5r)^2} = \frac{\sqrt{3}}{2}r \approx 0.866r$。因此 $\pm 0.6r$ 的端點確實落在圓盤內部（距離圓周尚有 $0.266r$ 裕度）。*

* **[`Pvt_AnalyzeProfile`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L183-L282) 邊界與極值處理**：
  * **平台區（Platform）固定取頭尾 20%**：`platform_count = (len + 4) / 5`（[`pvt_locate.c:211`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L211)），即固定以索引 `[0 .. platform_count-1]` 為起點平台、`[len - platform_count .. len - 1]` 為終點平台，計算 `start_mean` 與 `end_mean`（[`pvt_locate.c:238-264`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L238-L264)）。
  * **未定位邊界，直接取全域極值**：在 [`pvt_locate.c:224-237`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L224-L237) 的迴圈中，`minimum` 與 `maximum` 直接掃描整個 `i = 0 .. len - 1`：
    $$\text{maximum} = \max_{0 \le i < \text{len}} \text{profile}[i], \quad \text{minimum} = \min_{0 \le i < \text{len}} \text{profile}[i]$$
    接著在 [`pvt_locate.c:279-280`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L279-L280) 計算：
    $$\text{overshoot\_pct} = \frac{\max(0, \text{maximum} - \text{high})}{\text{step}} \times 100\%, \quad \text{undershoot\_pct} = \frac{\max(0, \text{low} - \text{minimum})}{\text{step}} \times 100\%$$
  * **完全沒有顯式邊界定位**：程式中**沒有**計算一階導數極值（LSF peak）、二階導數零交越（zero-crossing）、或 50% 轉態點（midpoint crossing $y = \frac{\text{high}+\text{low}}{2}$）。
  * **沒有檢查轉態單調性與銳利度**：唯一檢查僅在 [`pvt_locate.c:266-271`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L266-L271)：
    1. 頭尾平均差 `step >= 20.0`
    2. 起點平台內部極差 `start_max - start_min <= max(8.0, 0.10 * step)`
    3. 終點平台內部極差 `end_max - end_min <= max(8.0, 0.10 * step)`
    中間 60% 區域 `[platform_count .. len - platform_count - 1]` 即使出現雙重邊緣、震盪、污點或極緩慢斜坡，只要頭尾 20% 平坦就會全數放行。

### 2. 理論與實務影響評估（Failure Modes）

1. **平台雜訊偽陽性（Platform Noise Floor Bias）**：
   * 因為 `maximum` 與 `minimum` 涵蓋了頭尾平台區 `[0 .. platform_count-1]` 與 `[len-platform_count .. len-1]`，即使 ISP **完全沒有 sharpening halo（真實 overshoot = 0）**，只要亮區平台有單顆像素因感測器散粒雜訊（Shot Noise）或壓縮雜訊高於 `high`（平台平均值），就會產出非零的 `overshoot_pct`。
   * 數學上，$\max_{i \in \text{platform}} X_i - \bar{X}_{\text{platform}} \ge 0$ 恆成立（除非所有像素完全同值）。在平台平坦度容許 $\pm 5\%$（極差 $10\%$ of step）的門檻下，純平台雜訊就會直接灌入高達 $5\%$ 的假 overshoot。
2. **遠離邊界的異常點（Spurious Outliers / Dust / Compression Artifacts）**：
   * 真實的 ISP 銳化 overshoot（unsharp masking / peaking filter）必發生在邊界轉態點兩側約 $1 \sim 3\text{ px}$ 的 ringing zone 內。全域極值無法區分「緊鄰黑白邊界的銳化 ringing」與「距離邊界 $10\text{ px}$ 處的圖表反光、灰塵或 Bayer demosaic 偽影」。
3. **中心偏移或標靶旋轉導致邊界滑入平台區**：
   * 若 $(c_x, c_y)$ 定位偏移或標靶旋轉超過 $\sim 18^\circ$，黑白交界會靠近甚至侵入頭尾 20% 的平台區（距離中心 $0.36 r_{\text{eq}}$ 處），導致平台均值被拉偏，或觸發 `profile_incomplete` 退件。

### 3. 具體改善建議

1. **先定位 50% 轉態邊界 $i_{\text{edge}}$**：
   計算中位閾值 $T_{\text{mid}} = 0.5 \times (\text{start\_mean} + \text{end\_mean})$，在中間區間 `[platform_count .. len - platform_count - 1]` 尋找跨越 $T_{\text{mid}}$ 的索引 $i_{\text{edge}}$，並驗證該處具有足夠的局部斜率（例如 $|P[i_{\text{edge}}+1] - P[i_{\text{edge}}-1]| \ge 0.25 \times \text{step}$，確保為真實單調邊緣）。
2. **限制 Overshoot / Undershoot 搜尋視窗（Local Ringing Window）**：
   只在邊界兩側局部鄰域 $i \in [i_{\text{edge}} - W, i_{\text{edge}} + W]$（例如 $W = 4\text{ px}$）且**在正確的明暗側**搜尋極值（亮側只找 overshoot `max`、暗側只找 undershoot `min`），排除遠端平台雜訊。

---

## Q2. 「對比的比較是依據 多大面積?」

> **結論：最終輸出的對比度（`contrast_avg`, `contrast_min`）完全來自 4 條「寬度僅 1 px」的 1D 單像素剖面線頭尾端點，並非 2D 面積！**
> 每個靶標若半徑 $r_{\text{eq}} \approx 20\text{ px}$，每條切線長 $26\text{ px}$，頭尾平台各僅取 **6 個像素（$1\text{ px} \times 6\text{ px}$）** 計算黑白均值；4 條切線合計僅用 **48 個像素**。至於 [`pvt_set_means`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L71-L181) 取樣的 2D 扇區均值 `sector_means[4]` 在驗證通過後**即被丟棄**，完全沒有用於對比度輸出。

### 1. 現行實作分析（File:Line 證據）

* **兩處面積/取樣機制分工截然不同**：
  1. **`pvt_set_means`（僅作象限驗證 Gatekeeper，不參與輸出指標）**：
     * 位於 [`pvt_locate.c:71-181`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L71-L181)，於 [`pvt_locate.c:647-652`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L647-L652) 呼叫。
     * 在 3 個同心圓環（$0.35r, 0.60r, 0.85r$）$\times$ 24 個角度 = 72 個候選點中，篩除靠近象限接縫的點（`sector_distance > PI / 8.0` 即排除距扇區中心軸 $>22.5^\circ$ 的點，[`pvt_locate.c:144-147`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L144-L147)）。
     * 實際每個象限僅保留 **3 個半徑 $\times$ 3 個角度 = 9 個離散像素點**（4 象限共 **36 個有效取樣點**）。計算完 `best_score >= 50.0` 後，回傳的 `sector_means[4]` 在 [`pvt_locate.c:638-652`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L638-L652) **完全未被後續程式碼讀取**。
  2. **`Pvt_AnalyzeProfile`（真正產出 `contrast_avg` 與 `contrast_min` 的來源）**：
     * 於 [`pvt_locate.c:660-752`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L660-L752) 針對 4 條切線逐一執行最近鄰單點取樣（`floor(sample_x + 0.5)`, [`pvt_locate.c:716-717`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L716-L717)）。
     * 切線長度 `span = 1.2 * radius`（[`pvt_locate.c:670`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L670)），取樣點數 `sample_count = clamp(ceil(1.2 * radius) + 1, 10, 64)`（[`pvt_locate.c:671-679`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L671-L679)）。
     * 在 [`Pvt_AnalyzeProfile`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L211-L278) 中，兩端平台像素數為：
       $$N_{\text{plat}} = \left\lfloor \frac{\text{sample\_count} + 4}{5} \right\rfloor \approx 20\% \times \text{sample\_count}$$
     * 計算 Michelson Contrast（[`pvt_locate.c:278`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L278)）：
       $$C_k = \frac{\text{high}_k - \text{low}_k}{\text{high}_k + \text{low}_k}, \quad k \in \{0, 1, 2, 3\}$$
     * 最終匯總（[`pvt_locate.c:739-752`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L739-L752)）：
       $$\text{contrast\_avg} = \frac{1}{4}\sum_{k=0}^{3} C_k, \quad \text{contrast\_min} = \min_{k \in \{0..3\}} C_k$$

* **實際像素數量換算表（以典型靶標尺寸為例）**：

| 靶標直徑 $D$ | 等效半徑 $r_{\text{eq}}$ | 切線跨距 `span` ($1.2 r_{\text{eq}}$) | 單條取樣數 `len` | 單端平台像素數 $N_{\text{plat}}$ | 單條切線用於對比度的像素面積 | 4 條切線總計用於對比度的像素數 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| $30\text{ px}$ | $15\text{ px}$ | $18.0\text{ px}$ | $19\text{ px}$ | **$4\text{ px}$** | $1\text{ px} \times 4\text{ px}$（黑白各 4 點） | $32\text{ px}$ |
| $40\text{ px}$ | $20\text{ px}$ | $24.0\text{ px}$ | $25\text{ px}$ | **$5\text{ px}$** | $1\text{ px} \times 5\text{ px}$（黑白各 5 點） | $40\text{ px}$ |
| $50\text{ px}$ | $25\text{ px}$ | $30.0\text{ px}$ | $31\text{ px}$ | **$7\text{ px}$** | $1\text{ px} \times 7\text{ px}$（黑白各 7 點） | $56\text{ px}$ |

### 2. 理論與實務影響評估（1-px 單線取樣的物理限制）

1. **極易受高頻雜訊與 Demosaicing 偽影影響**：
   * 單端僅 $4 \sim 7$ 個像素，標準誤（Standard Error of the Mean）僅降低 $\sqrt{5} \approx 2.2$ 倍。在低照度或高增益（High ISO）下，不僅均值變異大，更容易因為單顆 noisy pixel 超過 `start_max - start_min > max(8.0, 0.10 * step)`（[`pvt_locate.c:267-268`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L267-L268)）而使整個靶標被誤判為 `profile_incomplete` 退件！
2. **Nearest-Neighbor 取樣混疊（Sampling Aliasing & Grid Quantization）**：
   * [`pvt_locate.c:690-717`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L690-L717) 將總長 `span` 除以 `sample_count - 1` 後用 `floor(x + 0.5)` 取最近鄰整數座標。因為 `span` 為浮點數而 `sample_count = ceil(span) + 1`，步長 $\Delta \in (0.95, 1.0)$ 略小於 $1\text{ px}$，會導致每隔幾個點就有**相鄰兩個 sample 落入同一個整數像素座標**（重複取樣同一像素），使有效獨立樣本數進一步縮水，且邊緣步階響應（ESF）出現人工階梯。
3. **Michelson Contrast 對暗部偏移極度敏感**：
   * Michelson 公式 $\frac{H - L}{H + L}$ 中，分母受暗部 $L$ 影響極大（當 $L \to 0$ 時，$C \to 1$；但若黑象限受耀光 Veiling Glare 墊高 $L$，或單線取樣剛好壓到灰塵，對比度會劇烈跳動）。

### 3. 具體改善建議

1. **改用多線條帶平均（Multi-line 2D Strip / Band Averaging）產生 1D 剖面**：
   沿切線法向（橫向寬度 $W_{\perp} = 3 \sim 5\text{ px}$，或 $0.35 r_{\text{eq}} \sim 0.65 r_{\text{eq}}$ 帶狀區域）對垂直於切線方向做平均，合成一條低雜訊的 1D 剖面。如此單端平台有效面積由 $1 \times 5 = 5\text{ px}$ 提升至 $5 \times 5 = 25\text{ px}$，雜訊標準差立刻減半，且能大幅降低 `profile_incomplete` 誤退件率與假 overshoot。
2. **或直接以 2D 象限扇區面積統計（Trimmed Mean / Median）計算宏觀對比度**：
   若 `contrast_avg` 目的是衡量靶標黑白象限的穩態對比度（DC Contrast），應在 4 個象限內部（例如 $0.35 r_{\text{eq}} \le r \le 0.80 r_{\text{eq}}$，避開十字接縫 $\pm 15^\circ$）的完整 **2D 扇區像素集合**（每象限約 $100 \sim 200\text{ px}$）計算均值，比單線端點穩定一個數量級以上。

---

## Q3. 「有正確定位的依據?」

> **結論：現行定位依據並非對黑象限本身直接算質心，而是對「暗部連通元件經逐列水平凸包填滿（Row-wise Horizontal Hull Fill）後的完整圓盤輪廓」計算一階幾何矩（Moments）質心。**
> 這在正視、無漸暈、兩個黑象限在圓心處 8-連通（8-connected）時，可藉由對角對稱性獲得合理的圓盤中心；但在**離軸廣角畸變、照度梯度、或點擴散函數（PSF）使圓心處兩黑象限斷開**時，會產生明顯的系統性偏移（Systematic Bias）。

### 1. 現行實作分析（File:Line 證據）

定位演算法位於 [`pvt_locate.c:324-655`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L324-L655)，分為五個步驟：

1. **ROI 外擴與局部 Otsu 二值化**（[`pvt_locate.c:332-397`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L332-L397)）：
   * 將 YOLO box 四周外擴 30%（`expanded_x1 = box->x1 - 0.30 * box_width` 等），對整個擴展後的 crop 統計灰階直方圖，求取單一 Otsu 閾值 `threshold`，並將暗像素（`luminance <= threshold`）標記為 `mask = 1`。
2. **8-連通元件搜尋與「逐列水平填洞（Row-wise Span Fill）」**（[`pvt_locate.c:407-565`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L407-L565)）：
   * 透過 BFS 找出 8-連通暗部元件，記錄每一列 $y$ 的最左暗像素 `row_min[dy]` 與最右暗像素 `row_max[dy]`（[`pvt_locate.c:474-481`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L474-L481)）。
   * **關鍵機制**（[`pvt_locate.c:516-535`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L516-L535)）：在計算面積與一階矩 `filled_m10`、`filled_m01` 時，它**不是**只加總暗像素，而是將每一列從 `left = row_min[dy]` 到 `right = row_max[dy]` 之間的整段水平線段視為填滿（包含中間夾著的白色象限像素）：
     $$\text{current\_width}(y) = \text{ right}(y) - \text{left}(y) + 1$$
     $$\text{filled\_area} = \sum_{y} \text{current\_width}(y)$$
     $$\text{filled\_m10} = \sum_{y} \frac{\text{left}(y) + \text{right}(y)}{2} \cdot \text{current\_width}(y), \quad \text{filled\_m01} = \sum_{y} y \cdot \text{current\_width}(y)$$
   * **幾何意義**：若靶標外圍有一圈黑環（outer dark ring），此操作會填平內部的白象限，得到整個外圓盤的面積與質心；若無外黑環、僅有左上與右下兩個黑象限且在圓心處接觸，上半部列只涵蓋左上黑象限、下半部列只涵蓋右下黑象限，靠兩者的反對稱性抵銷來逼近中心。
3. **候選篩選條件與規格書落差**（[`pvt_locate.c:566-583`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L566-L583)）：
   * 篩選條件為 `bbox_area >= crop_area * 0.20`、`fill_ratio >= 0.25`、`circularity >= 0.20`（注意：架構書 [`pvt_target_architecture.md:155`](file:///C:/Github/roi-analyzer/docs/pvt_target_architecture.md#L155) 寫 `circularity >= 0.45`，但程式碼實作為 `0.20`）。
   * **候選排序邏輯風險**（[`pvt_locate.c:572-575`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L572-L575)）：`distance = fabs(crop_area - bbox_area)`，挑選 `bbox_area` **最接近整個外擴後 `crop_area`** 的連通元件！因為 `crop_area` 是 YOLO box 雙向各外擴 30% 後的面積（即 $(1.6)^2 = 2.56$ 倍 YOLO box 面積），真實靶標面積只佔 `crop_area` 約 $\frac{1}{2.56} \approx 39\%$。若背景中有大面積深色陰影或黑框連成一片，其 `bbox_area` 反而更接近 `crop_area`，會優先奪標。
4. **Fallback 退路機制**（[`pvt_locate.c:586-626`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L586-L626)）：
   * 若無連通元件通過篩選，改以 `crop` 中心 `(crop_width/2, crop_height/2)` 為圓心、$1.15 \times r_{\text{est}}$ 為半徑，直接將圓內所有暗像素 `mask != 0` 的座標加總求質心（`dark_m10 / dark_area`），並將面積乘以 2 換算等效半徑 `radius = sqrt(2.0 * best_area / PI)`（[`pvt_locate.c:646`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L646)）。
   * *微小座標偏移 Bug*：在 [`pvt_locate.c:606-612`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L606-L612) 中，距離判斷用了像素中心 `(double)x + 0.5`，但累加 `dark_m10 += (double)x` 卻漏加 `+ 0.5`（主路徑 [`pvt_locate.c:533`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L533) 的 `(left + right)/2` 也同樣基於整數索引而無 `+0.5`，但 Fallback 檢查與累加基準不一致）。
5. **72 點象限驗證（[`pvt_set_means`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L71-L181)）的角色**：
   * 它是一個**二元分類過濾器（Binary Gatekeeper）**，驗證以 `(center_x, center_y)` 為圓心時，四個象限是否符合「對角同明暗、相鄰反差大」：
     $$\text{score} = \frac{|M_0 - M_1| + |M_1 - M_2| + |M_2 - M_3| + |M_3 - M_0|}{4} - \max(|M_0 - M_2|, |M_1 - M_3|) \ge 50.0$$
   * **它只負責拒絕誤檢（False Positive Rejection），完全不會修正或微調 `(cx, cy)`**。即使 `(cx, cy)` 偏離真實十字交點達 $2 \sim 3\text{ px}$，由於 [`pvt_set_means`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L144) 刻意避開了象限邊界 $\pm 22.5^\circ$（只取扇區中央 $\pm 22.5^\circ$、半徑 $0.35r \sim 0.85r$ 處的內部點），只要偏移未超過 $r \cdot \sin(22.5^\circ) \approx 0.38r$（對 $r=20\text{ px}$ 而言約 $\pm 7.6\text{ px}$），`pvt_set_means` 依然會高分通過！因此**通過 `pvt_set_means` 僅代表圓心落在正確靶標內（誤差 $< 0.35r$），絕不構成次像素（$\le 1\text{ px}$）精定位的數學保證**。

### 2. 四種實務劣化情境下的偏移分析（Bias & Drift Conditions）

#### a) 跨 40–50 px 圓盤的照度梯度（Illumination Gradient / Vignetting）
* 對二值化幾何質心（Unweighted Binary Moments）而言，若梯度平緩，圓盤內部純黑與純白區仍能正確二值化，影響主要發生在**模糊邊緣（PSF transition zone）的等高線位移**。
* 當一側較亮、另一側較暗時，全域 Otsu 閾值 $T$ 在亮側會使暗部邊界內縮、在暗側會使暗部邊界外擴，導致二值化 mask 往暗側膨脹，質心向暗側漂移約 $0.5 \sim 1.5\text{ px}$。
* 此外，若外擴 30% 的背景為白色圖表底色，暗側背景可能跌破 Otsu 閾值而與黑象限黏連（bleeding），直接拉走 `row_min` / `row_max` 造成數個像素的嚴重偏移。

#### b) 廣角鏡頭桶狀畸變與透視變形（Barrel / Perspective Distortion）
* 在投影變換（Projective Transformation / Homography）與徑向畸變下，**圓盤外框橢圓的面積質心（Area Centroid of Ellipse）不等於圓心十字交點的投影（Projected Intersection of Crosshairs）**！
* 十字交點是兩條正交直線的交點，在透視投影下屬於**射影不變量（Projective Invariant）**；而面積質心不具備射影不變性。在廣角邊角處，外輪廓拉長且非線性壓縮，面積質心會往畸變拉伸方向偏移。

#### c) Otsu 閾值敏感度（Threshold Sensitivity）
* **最致命的拓樸斷裂風險（Topological Disconnection at Saddle Point）**：
  四象限靶標的中心交點 $(c_x, c_y)$ 處，兩個對角黑象限與兩個對角白象限交會，在光學點擴散函數（PSF）與離散像素取樣下，中心點是一個**鞍點（Saddle Point）**，其灰階值約為 $\frac{I_{\text{black}} + I_{\text{white}}}{2}$。
  * 在外擴 30% 的 crop 中，因為外圍多了一大圈淺色背景，亮像素比例遠高於暗像素（約 $75\%$ 亮 vs $25\%$ 暗），這會把 Otsu 閾值 $T$ **往高亮端推升**，通常能讓兩黑象限在中心保持 8-連通。
  * 但若外圍有黑框或陰影使 Otsu 閾值 $T$ 略低於鞍點灰階，**左上與右下兩個黑象限會在中心點斷開成 2 個獨立的連通元件（Connected Components）**！
  * 一旦斷開，[`pvt_locate.c:449-584`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L449-L584) 的 BFS 只會選出其中**單一一個黑象限（1/4 圓扇形）**作為最佳元件（只要單一扇形的 `bbox_area >= 0.20 * crop_area`，這在緊貼的 YOLO box 下可能成立），此時算出的 `(center_x, center_y)` 會直接落入**單一黑象限的質心**（偏離真實圓心約 $\frac{4\sqrt{2}}{3\pi}r \approx 0.60r \approx 12\text{ px}$）！雖多數情況下會被 `pvt_set_means` 攔截並報 `quadrant_mismatch`，但若觸發 Fallback（[`pvt_locate.c:586`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L586)），Fallback 的圓心 `ccx, ccy` 又是直接取 YOLO box 中心，等於放棄了次像素精定位。

#### d) 兩個黑象限之間的不對稱性（Asymmetry between Black Quadrants）
* 當無外黑環時，`row_min` 與 `row_max` 僅涵蓋兩個對角黑象限。設左上黑象限面積為 $A_1$（質心 $(-d, -d)$）、右下黑象限面積為 $A_2$（質心 $(+d, +d)$，其中 $d = \frac{4}{3\pi}r \approx 0.424r$）。
* 合成質心偏移量為：
  $$\Delta c = d \cdot \frac{A_2 - A_1}{A_1 + A_2} \approx 0.424 r \cdot \frac{\Delta A}{A_{\text{total}}}$$
* 只要因為廣角邊角漸暈（Vignetting）、斜射 MTF 不對稱（Coma / Astigmatism）或印刷誤差導致兩黑象限二值化後的像素數相差 $10\%$（例如一側 $165\text{ px}$、另一側 $150\text{ px}$），中心就會立刻往較大象限偏移 $0.424 \times 20 \times 0.05 \approx 0.42\text{ px}$；若某幾列只有單側黑象限被納入或被裁切，偏移更會放大至 $1 \sim 3\text{ px}$。

---

### 3. 經典精密量測 / ISP 標定領域的黃金標準（Gold Standard）

在經典光學量測（ISO 12233、Imatest、OpenCV `findChessboardCorners` / `cornerSubPix`、攝影測量標靶定位）中，針對四象限圓靶（Fiducial Crosshair / Quadrant Target）定位中心的黃金標準**絕非外輪廓或暗部面積質心**，而是**「正交刀口邊緣直線擬合求交點（Orthogonal Knife-Edge Line Fitting Intersection）」**或**「鞍點對稱性梯度算子（Saddle-Point Symmetry Operator）」**：

1. **方法 A：正交刀口次像素直線交點法（Sub-pixel Edge Line Intersection — 最推薦且與後續量測完美契合）**
   * **原理**：既然 [`pvt_locate.c:692-715`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L692-L715) 已經設計了橫跨垂直與水平接縫的剖面切線，四象限靶標真正的幾何中心定義就是**「垂直黑白分界線 $L_V$」與「水平黑白分界線 $L_H$」的交點**。
   * **演算法步驟**：
     1. 先用目前的粗糙質心（或 YOLO box 中心）$(c_x^{(0)}, c_y^{(0)})$ 作為初值。
     2. 在上下半部（例如 $y = c_y^{(0)} \pm k\Delta y$，避開圓心 $\pm 0.2r$ 鞍點模糊區，取 $0.25r \le |y - c_y^{(0)}| \le 0.75r$）抽取多條水平 1D 剖面，在每條剖面上利用**一階導數重心（Centroid of 1st Derivative / LSF）或線性內插 50% 交越點**求得次像素水平邊緣位置 $x_i$。
     3. 對點集 $\{(x_i, y_i)\}$ 進行加權最小平方法直線擬合（或直接取上下對稱位置的中點平均），得到垂直分界線 $L_V$。
     4. 同理在左右半部抽取多條垂直 1D 剖面，求得次像素垂直邊緣位置 $y_j$，擬合水平分界線 $L_H$。
     5. 求 $L_V \cap L_H$ 的交點 $(c_x^*, c_y^*)$。
   * **優勢**：
     * **射影不變性（Projective Invariance）**：即使廣角鏡頭將圓盤扭曲為橢圓，直線在透視投影下仍為直線（在局部 $40\text{ px}$ 範圍內徑向畸變曲率極小，且上下/左右對稱取樣會自然抵銷二次項），交點精準對應物理十字中心。
     * **對 Otsu 閾值與鞍點斷裂完全免疫**：完全在連續灰階（Gray-level）上運算，不依賴二值化連通性。
     * **與 Q1 邊界定位合而為一**：求出的次像素邊界位置可直接作為 Q1 計算 overshoot / undershoot 的精確邊界基準（精度可達 $0.05 \sim 0.1\text{ px}$）。

2. **方法 B：Forstner / Harris 鞍點算子（`cvCornerSubPix` 等效算法）**
   * **原理**：在圓心鄰域 $r \le 0.5 r_{\text{eq}}$ 內，四象限中心具有一個極強的**梯度正交特性**：局部每個像素 $(x_i, y_i)$ 的影像梯度向量 $\nabla I(x_i, y_i) = [g_x, g_y]^T$ 都垂直於指向圓心 $(c_x, c_y)$ 的向量，即：
     $$\nabla I(x_i, y_i)^T \cdot \begin{bmatrix} x_i - c_x \\ y_i - c_y \end{bmatrix} = 0$$
   * 透過最小化加權平方和 $\sum_i \left( g_{x,i}(x_i - c_x) + g_{y,i}(y_i - c_y) \right)^2$，只需解一個 $2 \times 2$ 線性方程組即可直接閉合解出次像素中心 $(c_x, c_y)$（純 C 僅需約 25 行程式碼，計算量 $< 0.01\text{ ms}$）。

---

## 總結對照與評估一覽表

| 審查項目 | 現行實作（[`pvt_locate.c`](file:///C:/Github/roi-analyzer/src/pvt_locate.c)） | 潛在風險與失效模式 | 建議改善方案（依優先級） |
| :--- | :--- | :--- | :--- |
| **Q1. Overshoot 邊界定位** | **無邊界定位**（[`pvt_locate.c:224-280`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L224-L280)）：直接對整條 1D 剖面 `[0..len-1]` 取全域 `maximum` / `minimum` | 1. 平台區散粒雜訊直接變成假 overshoot（恆 $\ge 0$）<br>2. 無法區分邊界旁 ringing 與遠端灰塵/偽影<br>3. 未檢查中間 60% 轉態區是否為單調銳利邊緣 | **P1**：先找 50% 轉態交越點 $i_{\text{edge}}$ 並驗證局部斜率；僅在 $[i_{\text{edge}}-W, i_{\text{edge}}+W]$ 視窗內、對應明暗側搜尋極值 |
| **Q2. 對比度量測面積** | **1 px 寬單線端點**（[`pvt_locate.c:211, 670-731`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L670-L731)）：4 條切線頭尾 20%（每端僅 $4 \sim 7\text{ px}$，4 線共約 $40\text{ px}$）；`pvt_set_means` 的 36 點扇區均值未用於輸出 | 1. 單線 $5\text{ px}$ 雜訊變異大，易誤觸平台平坦度門檻（`profile_incomplete`）<br>2. `floor(x+0.5)` 步長 $<1\text{ px}$ 造成重複取樣同一像素 | **P1**：切線改為法向寬度 $3 \sim 5\text{ px}$ 的 **2D 條帶平均（Strip Averaging）**，步長固定 $1.0\text{ px}$；或用 2D 象限扇區面積統計穩態對比 |
| **Q3. 靶心定位依據** | **Otsu 二值化 + 逐列水平填洞質心**（[`pvt_locate.c:381-646`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L381-L646)）+ 72 點象限門檻過濾（[`pvt_set_means`](file:///C:/Github/roi-analyzer/src/pvt_locate.c#L71-L181)） | 1. 無外黑環時若 Otsu 閾值偏低，兩黑象限在鞍點斷開會退化為單一象限質心或 Box 中心<br>2. 挑選最接近外擴 `crop_area` 的元件易被大面積背景黑塊干擾<br>3. `pvt_set_means` 僅作及格過濾，不修正中心 | **P1**：以粗質心為初值，利用 4 條切線的 50% 次像素交越點直接更新 $(c_x, c_y)$（**正交刀口交點法**），兼具射影不變性與次像素精度 |
