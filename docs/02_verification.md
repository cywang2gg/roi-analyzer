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
| V8 | per-image/mode log 格式正确 | 检查源目录 `<image>_<mode>.log` | `# roi rect` 开头,RGB/Y/Lab 数值保留 2 位小数,同图同模式 append 不覆盖旧记录 |
| V9 | 越界/空图鲁棒性 | 边界点击、无图操作 | 不崩溃、无 log 写入、状态栏提示 |

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

流程:C 程序框选 → 读源目录当前图片、当前模式的 `<image>_<mode>.log` 最后一条记录 → 与 `ref_stats` 同区域输出对比,容差 0.01。

## 4. 验证步骤(手动清单)

1. 构建:`cmake --build build`,确认 0 warning,exe 存在。
2. 拖入 `test_red.png` → 标题栏显示 `test_red.png 100x100`。
3. 按 `1`(drag),全图框选 → 状态栏显示 `R=255.00 G=0.00 B=0.00 Y=76.24`。
4. 检查测试图源目录的 `test_red_drag.log`,核对最后一条记录 = §2 期望值,并确认不存在 `test_red_log.log`、`roi_log.csv` 或 `roi_log.txt`。
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
V8 log: ✅ 源目录 per-image/per-mode `.log`,`# roi rect` 格式,2 位小数,append 正常
V9 鲁棒性: ✅ 无崩溃
结论: 通过 / 不通过(注明失败项)
```
