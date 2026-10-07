# Histogram Panel 圖表顯示比例演算法 — LLM 移植說明書（自足版）

> 目的：把 roi_analyzer 的 Histogram 面板「長條高度比例／顯示換算」邏輯移植到其他專案。
> 本文件只講**顯示比例與座標換算**；資料收集與互動細節附在尾端供對照。
> 實碼來源：`src/histogram.c`（139 行）、`src/histpanel.c`（809 行）。純 C／Win32 GDI／32bpp BGRA。

## 1. 資料模型（顯示的輸入）

```c
/* 256 級直方圖，4 通道 */
bin[4][256];        /* [0]=R [1]=G [2]=B [3]=Y，單位=像素數 */
max_bin[4];         /* 每通道最大 bin 值（顯示正規化用） */
count;              /* 統計像素總數 */
```

關鍵：**顯示的正規化基準是 `max_bin`（最高那根柱），不是 count**。
4 通道同時顯示（RGB 模式）時，取 R/G/B 三個 `max_bin` 的最大值當共同基準。

## 2. 高度比例（核心公式）

每個 X 座標的柱高：

```
linear 模式:  ratio = value / maximum
log 模式:     ratio = log(1 + value) / log(1 + maximum)
bar_h  = round(ratio * graph_h)          # 四捨五入 (int)(ratio*h + 0.5)
```

- `value`＝該 X 位置的 bin 值，`maximum`＝`max_bin`（見上），`graph_h`＝繪圖區高度（px）。
- log 模式是 `log(1+v)/log(1+max)`（**+1 平移**，避免 log(0)；天然 log 或 log2 皆可，比值同）。
- value=0 或 maximum=0 → 柱高 0。
- **畫出來的是「柱體」不是「折線」**：某列 y 是否著色 = `graph_h - y <= bar_h`，
  即從底往上的實心填滿（§4 逐像素迴圈可見同一 X 的所有 y 判同一 `bar_h`）。

## 3. X 軸：256 bin → 圖寬的映射（寬 > 256 時）

**桶聚合用 max，不用 sum**（寬>256 時多個 bin 併進同一 X 欄位）：

```c
/* 某像素欄位 x 對應的 bin 範圍 [lo, hi] */
lo = x * 256 / width;
hi = (x + 1) * 256 / width - 1;   /* 注意 -1：半開區間 [lo, hi] */
/* clamp：lo>255→255；hi<lo→hi=lo；hi>255→255 */
bucket_value = max(bin[lo..hi]);  /* 取範圍內最大 bin，不是總和 */
```

- 為什麼用 max：sum 會隨縮放倍率改變柱高（同一影像放大面板後形狀變形）；
  max 保持「最高峰的高度」跨寬度穩定。
- `width <= 256` 時每個 X 對應 ≤1 個 bin，退化為直接取 `bin[level]`，公式自動成立
  （`lo==hi` 或部分 X 欄位為空 → value 0）。

## 4. 逐像素繪製（DIB 直接寫，無 GDI 繪圖物件）

圖表是 32bpp top-down DIB（`biHeight = -height`）逐像素寫：

```c
for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
        level  = x * 256 / width;                    /* 該欄的 bin 位址（clamp 255） */
        color  = 背景 RGB(40,40,40)；
        若 (sel_lo..sel_hi 選區內) color = RGB(70,70,70)；
        if (有效資料) {
            value = bucket_max(x);                   /* §3 */
            /* 判斷此列是否在柱體內：從底部數第 (height-1-y) 列 */
            if (height - y <= bar_height(value, maximum, height, log))
                color = 柱色(x, 通道)；
        }
        pixels[y * width + x] = color;               /* 0x00RRGGBB（§5） */
    }
}
```

- 判柱式：`height - y <= bar_h`（y=0 是最上列；底部 y=height-1 恆成立當 bar_h≥1）。
- 先判選區底色再判柱色 → 選區只影響「柱外」的底，柱體照常顯示。

## 5. RGB 疊色規則（三通道同圖）

R/G/B 三柱在同一 X 欄位各自判 `active`（該欄各自 bucket 值的柱體覆蓋此列），
**顏色由 active 組合決定**：

| R | G | B | 顏色 | RGB |
|---|---|---|---|---|
| ✓ | ✓ | ✓ | 白（三疊） | (200,200,200) |
| ✓ | ✓ | – | 黃 | (230,210,60) |
| ✓ | – | ✓ | 洋紅 | (220,80,220) |
| – | ✓ | ✓ | 青 | (60,210,220) |
| ✓ | – | – | 紅 | (230,60,60) |
| – | ✓ | – | 綠 | (60,200,60) |
| – | – | ✓ | 藍 | (70,110,240) |

- 共同正規化基準 = `max(max_bin[R], max_bin[G], max_bin[B])`（Y 不參與）。
- DIB 像素打包：`((R<<16) | (G<<8) | B)`（實碼 `dib_color()`；GDI 係 BGR 由 DIB 處理）。
- 單通道模式：直接用通道色（Y=灰白 (200,200,200)），無疊色。

## 6. 座標反算（滑鼠 → level）

```c
/* graph 矩形內 x（client 座標）→ level 0..255 */
level = (x - graph.left) * 256 / graph_w;   /* clamp 0..255 */
```

hover 直線在螢幕上的 X：

```c
hover_x = level * graph_w / 256;            /* clamp graph_w-1 */
/* 畫 1px 白直線：PatBlt(graph.left + hover_x, graph.top, 1, graph_h, WHITENESS) */
```

注意正反算非完全互逆（除法截斷），hover 線以 level 為準重算 X，而非存當初的 x。

## 7. 面板版面（各區 Y 座標）

```
combo    (8, 3,   w-100, 220)      通道下拉
log      (w-82, 4,  74, 22)        Log 核取
graph    (8, 52,  w-8, h-132)      直方圖繪圖區（§2-§6）
ramp     (8, graph.bottom+8, w-8, 高10)  漸層條（黑→白 or 單色漸變）
stats    (8, ramp.bottom+8, w-8, h-4)    文字區（§8）
graph.bottom 保底：若 < graph.top+20 → =graph.top+20
```

漸層條（ramp）：`level = x * 255 / (width-1)`，單通道給該色漸變、RGB/Y 給灰階。

## 8. 文字區內容（stats）

- RGB 模式 4 行：`R Mean %.2f StdDev %.2f Median %d`（G/B/Y 同式）。
- 單通道多一行 `Pixels: %u`。
- 有選區：`Level lo..hi  Count %u  Pct %.2f%%`（優先顯示）。
- 無選區有 hover：RGB 顯 `Level %d  R:%u G:%u B:%u`；單通道顯 `Level %d  Count %u  Pct %.2f%%`。
- Mean/StdDev 由 bin 直方圖反推（Σlevel·bin / count；變異數 = E[x²]−E[x]²），
  Median = 累積 ≥ count/2+count%2 的第一個 level。
- 選區 Pct = (Σ bin[0..hi]) / count × 100（累積到 hi，**不是只算 lo..hi**——沿用實碼語義）。

## 9. 更新與快取策略（顯示為何流暢）

- 三層 DIB：`base`（圖表本體）、`ramp`（漸層條）、`back`（合成背景）。
- 髒旗 `base_dirty`／`ramp_dirty`：只在資料、通道、Log、選區、尺寸變化時重算；
  hover 只在 back 上 PatBlt 白線＋局部 Invalidate（不重畫 base）。
- `WM_PAINT`：back 全填 → 貼 Source 標籤 → BitBlt(base) → hover 線 → BitBlt(ramp) → 文字 →
  依 `ps.rcPaint` 只 BitBlt 髒區到螢幕。
- 資料快取：`img_gen`（影像世代號）+ `whole` + `src` RECT 三者相同就跳過重算。

## 10. 移植檢查清單

1. 正規化基準 = `max_bin`（共同基準取 R/G/B max），不是 count 或 99 百分位。
2. Log 是 `log(1+v)/log(1+max)`，不是 `log(v)/log(max)`。
3. 寬>256 的桶聚合用 **max**（保持形狀穩定），不是 sum/avg。
4. 柱體判式 `height - y <= bar_h`（實心），不是畫折線。
5. X→level：`x*256/width`；level→X：`level*width/256`（各自 clamp）。
6. 選區著色先於柱體（只染底不染柱）；選區統計 Pct 是累積到 hi。
7. RGB 疊色 7 態表（§5）；共同基準讓三柱高度可直接比較。
8. hover 線由 level 反算 X，不存原始 x。

## 附：資料收集（對照用）

- 32bpp BGRA（px[0]=B, px[1]=G, px[2]=R），`Y = (299R + 587G + 114B + 500) / 1000`（整數 BT.601）。
- RECT 為**含端點**（left..right、top..bottom 皆取），clamp 後 `y<=bottom`、`x<=right` 迴圈。
- 每像素四通道各 +1；`count` = 統計像素數；rect 非法（swap 後空）直接回無效。
- `Hist_RangeStats(h, ch, lo, hi)`：區間 count/mean/std + 累積 Pct（§8）。
