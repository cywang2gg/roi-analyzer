# ROI Analyzer — 确认验证文档

> 对应架构 `01_architecture.md` | 验收通过标准:全部 ✅ 项成立

## 1. 验证总览

| # | 验证项 | 方法 | 通过标准 |
|---|--------|------|----------|
| V1 | 构建 0 warning | `-Wall -Wextra` 全量构建 | `cmake --build build` 零 warning、零 error,产出 `bin/roi_analyzer.exe` |
| V2 | 图档加载正确 | 纯色测试图 | 像素值与预期一致(见 §2),尺寸/通道正确 |
| V3 | drag 框坐标正确 | 已知坐标框选 | `x0<=x1,y0<=y1`,count = 宽×高,log 坐标与框选一致 |
| V4 | fix3/fix5 尺寸恒定 | 3x3 / 5x5 点选 | count 恒为 9 / 25,框中心 = 点击点(边缘 clamp) |
| V5 | RGB mean/std 正确 | 纯色 + 渐变测试图 | 纯色 std=0,渐变 mean/std 与 Python 参考值一致(容差 0.01) |
| V6 | Y mean/std 正确 | 同上 | `Y=0.299R+0.587G+0.114B`,与参考值一致(容差 0.01) |
| V7 | Lab 正确 | 标准色块 | 与 c-vlcplayer `rgb_to_lab` 同输入同输出(容差 0.05);白场 L≈100、a/b≈0,黑场 L≈0 |
| V8 | per-image/mode CSV 格式正确 | 檢查源目錄 `<image>_<mode>.csv` | UTF-16LE BOM（`FF FE`）僅在檔案開頭；沒有 `sep=` 行；每區塊先有 `# ==== export`、`id Rm…b rect count` 14 欄 tab 表頭，ROI 每行 14 欄且數值 2 位小數；續寫有空行且不重複 BOM |
| V9 | 越界/空图鲁棒性 | 边界点击、无图操作 | 不崩溃、无 log 写入、状态栏提示 |
| V10 | 旋转正确（v2.8） | 90°×4 回原图；90°/180°/270° 各一次 | 画布适配新尺寸，ROI 框位置正确（§3 公式），表格数值更新，标题列＋状态列出现 `*`；90°×4 与原图逐像素一致 |
| V11 | 未存档防护（v2.8） | 旋转后按 →／开档／拖放／关窗／开比较 | 弹 Yes／No／Cancel：Yes 存档后继续（`*` 消失），No 直接继续，Cancel 留原地 |
| V12 | 存档＋比较关闭（v2.8） | 旋转后 Ctrl+S；旋转时开着 V1/V2 | 覆盖原路径 PNG，`*` 消失，状态列 `Saved <档名>`；比较视窗自动关闭 |
| V13 | 直方图 Y 行（v1.3） | RGB 叠合模式看统计列 | 显示 R/G/B/Y 四列，各 Mean／StdDev／Median |
| V14 | 監控觸發＋彈窗（v2.9 M1–M9） | 設監控路徑後丟新圖；連拍 5 檔；雙開 exe | 彈窗含唯讀縮圖＋5 按鈕；連拍依序彈不漏（ring 64）；雙開不重彈；Modal 忙碌只入隊 |
| V15 | 更名＋防護（v2.9 R1–R8） | F2 更名；非法字元／保留字／覆寫；旋轉未存檔按 F2 | 更名成功載新圖＋CSV 連動；非法／保留字阻擋；覆寫先備份 `.bak`；未存檔單一檢查點只問一次 |
| V16 | self-trigger 迴圈抑制（v2.9） | 監控目錄進檔→彈窗更名 `ddd_X` | 只彈一次，不再二次彈窗；`LastRenamePrefix` 不疊加；外部相機 5 秒後進檔正常觸發 |
| V17 | Metrics 一期指標（v3.0） | V1/V2 開 2 張圖按 Metrics（或 Ctrl+M） | HTML 報告開瀏覽器：Laplacian／Sobel／8 向對比／SNR／亮度四區／飽和度欄位齊全 |
| V18 | Metrics 二期＋4MP 非同步（v3.0） | Stage2 開啟；4MP 以上大圖按 Metrics | FFT 三頻帶＋Lab 色偏＋邊緣圖 Base64 內嵌；大圖走背景執行緒、UI 不凍結，進度框可取消 |
| V19 | Metric Set v2 遮罩驅動量測（v3.1） | 同一場景 3 張標準圖（清晰／普通／模糊）跑 Metrics | S1 遞減、S2 遞增、S3 單調；Edge／Flat／Neutral 覆蓋率合理（Flat 未被邊緣污染）；樣本不足項顯示 N/A＋reason，不回 0 |
| V20 | 排名引擎＋報告升級（v3.1） | 2～4 張圖跑 Metrics，切換 profile | 四方向正規化分數 0–100；類別分＋綜合排名正確；差異 ≤2 分標 `≈` 並列；HTML 含熱力表／SVG 雷達／警示區／legacy 收合／CSV 匯出；3 視圖縮圖內嵌 |
| V21 | 放大繪製來源列帶鏡像修正（v3.2） | 開大圖（如 4000×3000）→ `Ctrl+滾輪` 放大至超出視窗 → 分別向上、向下平移，將 ROI 框在**已知格線位置** | **框住的內容 = 框住的區域**（框不再與內容錯開）；上下平移方向皆正確；`zoom ≤ 1` 行為不變；headless harness 探測點 16/16 `OK`（修正前裁切情境 8/8 `DRIFT`） |
| V22 | 放大繪製 dest 反推（局部比例）（v3.2） | 同上情境，於畫面**左端與右端**各量一次 ROI 框邊界與最近格線的螢幕距離 | 局部比例 == 全域 `scale`，誤差 < 1 px 且**不隨距離由左往右累積**（修正前右緣可累積至 ~`scale` px）；`zoom` 1.0×／2.0×／3.7×／8.0× 皆同 |

## 2. 测试图生成(Python 参考实现)

```python
# tools/make_test_images.py — 用 PIL 生成,同步生成期望值
from PIL import Image
import json

def solid(path, size, rgb):
    Image.new("RGB", size, rgb).save(path)

def grad_x(path, w=256, h=64):
    # R 通道 0..255 横向渐变,G=B=0 → Y、std 可解析计算
    im = Image.new("RGB", (w, h))
    px = im.load()
    for x in range(w):
        for y in range(h):
            px[x, y] = (x, 0, 0)
    im.save(path)

solid("test_red.png",   (100, 100), (255, 0, 0))
solid("test_green.png", (100, 100), (0, 255, 0))
solid("test_blue.png",  (100, 100), (0, 0, 255))
solid("test_white.png", (100, 100), (255, 255, 255))
solid("test_black.png", (100, 100), (0, 0, 0))
grad_x("test_grad_r.png")
```

期望值(pure red 全图 drag):

| 字段 | 期望 |
|------|------|
| count | 10000 |
| R_mean / R_std | 255.00 / 0.00 |
| G_mean / B_mean | 0.00 / 0.00 |
| Y_mean | 76.245 → 76.24(0.299×255) |
| L/a/b | 53.24 / 80.11 / 67.22(与 c-vlcplayer `rgb_to_lab(255,0,0)` 一致) |

## 3. 交叉验证脚本(Python 参考值)

```python
# tools/ref_stats.py — 独立计算 ROI 统计,作为 C 程序的对照
import math
from PIL import Image

def ref_stats(path, x0, y0, x1, y1):
    im = Image.open(path).convert("RGB")
    rs = gs = bs = ys = r2 = g2 = b2 = y2 = n = 0
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            r, g, b = im.getpixel((x, y))
            yy = 0.299*r + 0.587*g + 0.114*b
            rs+=r; gs+=g; bs+=b; ys+=yy
            r2+=r*r; g2+=g*g; b2+=b*b; y2+=yy*yy; n+=1
    m = lambda s: s/n
    std = lambda s2, s: math.sqrt(max(s2/n-(s/n)**2, 0))
    return dict(n=n, r=(m(rs),std(r2,rs)), g=(m(gs),std(g2,gs)),
                b=(m(bs),std(b2,bs)), y=(m(ys),std(y2,ys)))
```

流程:C 程序框选 → 读源目录当前图片、当前模式的 `<image>_<mode>.csv` 最后一条資料行 → 与 `ref_stats` 同区域输出对比,容差 0.01。

## 4. 验证步骤(手动清单)

1. 构建:`cmake --build build`,确认 0 warning,exe 存在。
2. 拖入 `test_red.png` → 标题栏显示 `test_red.png 100x100`。
3. 按 `1`(drag),全图框选 → 状态栏显示 `R=255.00 G=0.00 B=0.00 Y=76.24`。
4. 檢查測試圖源目錄的 `test_red_drag.csv`，核對最後一列 = §2 期望值及 `id Rm…b rect count` 14 欄順序；重複 Export 後確認 UTF-16LE BOM 只在 offset 0、沒有額外首行、區塊間有空行，舊 `.log`／`.tsv` 檔未被修改。
5. 拖入 `test_grad_r.png`,drag 全图 → R_std ≈ 73.9(0..255 均匀分布总体 std = 255/√12 ≈ 73.90),Y_std ≈ 22.09(0.299×73.90)。
6. 按 `2`(fix3)任意点一下 → count=9;按 `3`(fix5) → count=25。
7. 在影像边缘点一下 fix5 → 框被 clamp,仍 count=25 且不崩溃。
8. 切换到 3x3/5x5 后即使尚未移动鼠标也应在图像中心看到固定框；未加载图片时按 `1` 拖曳 / 按 C → 无反应、不崩溃。
9. Lab 抽查:white 全图 → L≈100.00,a≈0.00,b≈0.00;black 全图 → L≈0.00。
10. 与 c-vlcplayer 对照:同图同区域,Lab 差值 < 0.05(同一公式移植,应完全一致)。

## 5. 已知风险与对策

| 风险 | 对策 |
|------|------|
| GDI+ 像素顺序误读(BGRA vs RGBA) | V2 纯色图验证:R 图必须 R_mean=255,G/B=0,否则交换通道 |
| JPEG 压缩噪声导致 std≠0 | 数值验证只用 PNG;注明 JPEG 仅功能性测试 |
| 固定框点到影像外 | 中心 clamp,框恒完整;V9 覆盖 |
| Lab 负零显示(`-0.00`) | 格式化前归零为 `0.00` |

## 6. 验收结论模板

```
构建: ✅ 0 warning, exe 290KB(示例)
V2-V4 坐标: ✅ count/坐标与预期一致
V5-V6 RGB/Y: ✅ 与 ref_stats 一致(容差内)
V7 Lab: ✅ 与 c-vlcplayer 一致,白/黑场正确
V8 CSV: ✅ 源目錄 per-image/per-mode `.csv`，UTF-16LE BOM、無額外首行、14 欄 tab 分隔、兩位小數及 append 區塊分隔正確
V9 鲁棒性: ✅ 无崩溃
结论: 通过 / 不通过(注明失败项)
```
