# PVT 靶標偵測＋量測架構書 v1.5

> 專案：`C:\Github\roi-analyzer`（純 C ＋ Win32 ＋ GDI ＋ WIC，ANSI 全 A 版，`-Wall -Wextra` 零警告，C11）
> 基準：`master`（v3.6.1）
> 需求：偵測測試圖上的四象限黑白圓靶（PVT target），精定位中心、量測黑白對比與 overshoot/undershoot
> 拍板（2026-10-08）：**偵測＝YOLO（使用者訓練）；自動觸發；`pvt_*.csv` 獨立 append；量測先固定 4 向（水平/垂直）**
> 前期實驗：`testdata/pvt/_pvt_detect.py`（2 輪傳統法）——結論：全域照度梯度＋廣角畸變使傳統法不可靠，轉 YOLO 找靶＋傳統精定位量測
> 日期：2026-10-16
> 狀態：**v1.5（6-class 實測模型已驗收、動態維度已解除、檔名決策 (a) 拍板）→ 可發包 gh**

## 變更歷史

| 版本 | 日期 | 內容 |
|---|---|---|
| v1.0 | 2026-10-08 | 初稿 |
| v1.1 | 2026-10-08 | （併入中） |
| v1.2 | 2026-10-08 | agy round 1 **NO-GO → 六項 Must-Fix 全採納**（§9）：MF1 `yolo_decode_v8` 寫死 6 通道/`[1,6,8400]`——`nc=1` PVT 模型是 `[1,5,8400]`，原樣呼叫會 **OOB read**；`yolo_ort_validate_tensor` 須動態比對通道數＋新增可指定 target class 的 decode（`yolo_decode_v8_ex`）。MF2 **執行緒安全**——`detect_job_t` 只帶 640 letterbox、無原圖 BGRA，worker 不得碰 `g_app.img.px`（UAF）；採**對策 A**：worker 只回 4 個 box（`WM_APP_PVT_DETECT_DONE`），§3/§4 精定位＋量測＋CSV 在**主執行緒**執行（4×40px 計算 <0.1ms）。MF3 box 內閾值改 **Box 局部 Otsu**（`box_w/4` 自適應核會掏空象限，實測碎裂 30 輪廓；Otsu 於 40×40 crop 圓度 0.795）。MF4 S3 **不可重用**（耦合 Sobel 法向/Lab/面積積分，數學定義亦與本書不同）——`pvt_locate.c` 自建 `pvt_analyze_profile()` 純 1D 函式。MF5 model 缺檔隔離——`detect_worker.c:302` 載入失敗會殺整條 worker；改 `g_pvt_session=NULL`＋`pvt_available=FALSE`，色卡照常。MF6 CSV writer 落 `export.c`（`Export_PvtLog`；`write_utf16le` 是該檔 static 私有函式，不得複製） |
| v1.3 | 2026-10-08 | **使用者拍板：單 model 多 class（2→重構）**——訓練合併為一套 3 class 資料集（`names=["122","color-chart","pvt-target"]`，nc=3 輸出 `[1,7,8400]`），單 session 單次推論：class1 走既有色卡流程、class2 走 PVT。v1.2 雙 session 架構作廢（MF5 自然消解）；**MF1 `yolo_decode_v8_ex` 由「選配」升「必需」**——`yolo_decode_v8` 只取 class1 寫死，無法取 class2；validate_tensor 通道=`4+nc` 依 json 動態比對（舊 2-class model 向後相容：無 class2 結果而已）。**訓練影響（使用者）**：PVT 標註須與色卡標註**合併同一資料集**重訓，不能分開訓兩顆 model |
| v1.4 | 2026-10-16 | **新模型準備流程定案**：主流程由 Roboflow 訓練 `yolov8n-640`（Nano、640），不需 Colab；**規劃建立** `tools/model_check.py` 檢查 ONNX input/output/class/metadata、`tools/build_model_json.py` 產出 runtime sidecar JSON；Colab 作備案（`tools/colab_train_yolov8n.py`），固定 Ultralytics `8.4.171`、ONNX opset 18、dynamic=False、nms=False、batch=1。模型驗收不通過前不得覆蓋正式 `models/color_chart.onnx` |
| v1.5 | 2026-10-17 | **實測模型驗收完成，設計由 3 class 改為 6 class**（§11）：實際訓練走 Roboflow 標註/資料集→Colab（Ultralytics 8.4.178）→ONNX，非 v1.4 假設的 Roboflow Train。`names=[122, color-chart, focus-aid, pvt-target, wedgeH, wedgeV]`，`nc=6`，輸出 `[1,10,8400]`。**pvt-target 是 class 3**（v1.3 假設 class 2 為錯，實測 metadata 修正）。首版 export 誤用 `dynamic=True`（`-1` 維度會被 `yolo_ort.c:104-111` 拒絕），已重匯 `dynamic=False` 固定 `[1,3,640,640]→[1,10,8400]`。檔案命名決策 **(a)**：新模型以 `models/color_chart.onnx` 為正式名，舊 2-class 退役。實測 6 張 2592×1944：color-chart 1、focus-aid 13、pvt-target 4、wedgeH 1、wedgeV 1，4 靶中心跨 AF on/off 差 ≤1px |

---

## 1. 分工與管線

```
標註/資料集：6 class（Roboflow）        yolo_ort.c 單一 session 推論
122/color-chart/focus-aid/          →   yolo_post.c decode_v8_ex＋NMS
pvt-target/wedgeH/wedgeV                → 逐 class boxes
訓練/匯出：Colab ultralytics 8.4.178     ↓
imgsz 640, opset 18,                ★  精定位（§3）：box 內局部 Otsu
dynamic=False, nms=False            →  cvMoments 次像素質心 → 72 點象限驗證
onnx + json 放 models/                  ↓
                                    ★  量測（§4）：4 向剖面 → 對比/overshoot/undershoot
                                        ↓
                                    canvas overlay＋狀態列＋pvt_*.csv（§5）
```

- **YOLO 管「找」**：吸收照度梯度/廣角畸變/構圖差異；box 精度 ±數 px 即可。
- **傳統管「精與量」**：box 內局部操作，全域照度不再是變因。
- **只實作 pvt-target**（class 3）；`focus-aid`(2)、`wedgeH`(4)、`wedgeV`(5) 由同一 model 免費輸出，列為延伸專案，本書不做後處理。

## 2. YOLO 接線（**單 model 多 class**，v1.5：6 class 實測）

| 項目 | 設計 |
|---|---|
| 模型檔 | `models/color_chart.onnx`＋`color_chart.json`——**單一 model、6 class**：`nc=6`，輸出 `[1,10,8400]`（4+6 通道）；訓練時色卡/PVT/focus-aid/wedge 標註合併於同一資料集 |
| class id（實測 metadata） | 0=`122`（捨棄）、1=`color-chart`（既有色卡流程）、2=`focus-aid`（延伸）、3=**`pvt-target`（本書實作）**、4=`wedgeH`（延伸）、5=`wedgeV`（延伸） |
| session | **單一 session**（v1.2 的雙 session 架構作廢；MF5 缺檔隔離問題隨單 model 自然消失——model 缺=兩功能同停，符合單檔語義） |
| worker | **單次推論**；`yolo_decode_v8_ex` 逐 class 解析（每 class 各自分數通道 `4+i`＋各自 NMS）：class1 → 既有色卡結果；class3 → PVT boxes → `WM_APP_PVT_DETECT_DONE` |
| validate_tensor | 通道數＝`4+nc`，由 json `names` 數量動態比對（MF1：寫死 `[1,6,8400]` 移除）。**固定維度為前提**——`dynamic=True` 匯出回傳 `-1`，現行 `yolo_ort.c:104-111` 嚴格比對會拒絕，故模型側要求 `dynamic=False` |
| 觸發 | 沿用既有 `IDT_DETECT_DELAY`（1 秒）自動觸發；換圖取消（seq 機制現成） |
| 失敗 | 色卡/PVT 由同一次推論產出，無獨立失敗面；decode_ex 對某 class 無候選＝該 class 0 結果（正常） |
| 效能 | **單次推論 ≈30ms**（比 v1.2 雙 model 少一半）；§3/§4 精定位量測在主執行緒 <0.1ms（MF2） |

## 2.1 新模型準備與訓練流程（v1.5 依實際走法修訂）

### A. 實務流程（2026-10-17 實際走法）

分工實際是「Roboflow 標註／資料集匯出 → Colab 訓練與匯出」，v1.4 假設的「Roboflow Train 一站到底」未被採用：

```text
Roboflow 標註（6 class）＋ Generate Dataset Version → 匯出 YOLOv8 格式
  → Google Colab：ultralytics 8.4.178 + yolov8n.pt 訓練
  → model.export(format="onnx", imgsz=640, opset=18,
                 dynamic=False, simplify=True, nms=False, batch=1)
  → models/color_chart-pvt.onnx（暫存）
  → model_check.py 驗收
  → build_model_json.py 產生 color_chart.json
  → 通過後更名為 models/color_chart.onnx（決策 (a)）
```

資料集 class 順序（不可變，直接對應 C 端語義）：

```yaml
names:
  0: 122
  1: color-chart
  2: focus-aid
  3: pvt-target
  4: wedgeH
  5: wedgeV
```

匯出的 ONNX **不可直接視為相容**；下載後必須先跑驗收工具。特別檢查是否為 raw detection output、是否關閉內嵌 NMS、class names/順序，以及 input/output 是否為固定維度。任何一項不過就停在 inbox，不得覆蓋正式模型。

`focus-aid` / `wedgeH` / `wedgeV` 目前只做偵測輸出，不做後處理（延伸專案）；標註時 wedge 僅中央上方那一組，其餘楔形未標註，屬已知範圍而非模型缺陷。

### B. 模型驗收工具（本機準備工具）

建立/規劃下列工具，工具只做檢查/產檔，不修改正式 model：

| 工具 | 功能 | 必要輸出 |
|---|---|---|
| `tools/model_check.py` | 用 ONNX Runtime 讀 model；檢查 input 固定 `[1,3,640,640]`、output raw shape `[1,4+nc,8400]`、`nc = len(names)`、metadata names、無 NMS 節點；列出 PASS/FAIL 與實際 shape | `MODEL-CHECK: PASS` 或明確 FAIL 原因 |
| `tools/build_model_json.py` | 讀取通過的 ONNX＋驗證 class names，產生 `color_chart.json`；寫入 `names`、`v1_use_class=1`、`pvt_use_class=3`、`output_shape`、`opset`、producer 版本與 source 欄位 | sidecar JSON，不改 ONNX |
| `tools/model_inbox/` | 暫存新下載檔及驗收報告 | 不納入正式 runtime 路徑 |

驗收條件（v1.5 實測基準）：

```text
input       = [1, 3, 640, 640]      固定（不接受 -1 / dim_param）
output      = [1, 10, 8400]         4 + nc，nc=6
class names = ["122","color-chart","focus-aid","pvt-target","wedgeH","wedgeV"]
output      = raw YOLOv8 detect；無 NMS/TopK 節點；end2end=False
opset       = 18
```

`model_check.py` 必須先檢查 tensor rank/dim，再檢查 class metadata；不能只依檔名判定模型版本。工具產生的 JSON 是 C runtime 的唯一 class mapping 來源。

### C. Colab 訓練＋匯出（實際採用路線）

```python
!pip install -q ultralytics
from ultralytics import YOLO

model = YOLO("yolov8n.pt")
model.train(data="/content/24-color-chart-3/data.yaml", imgsz=640,
            epochs=100, batch=16, device=0,
            project="/content/runs", name="train")

best = YOLO("/content/runs/detect/train-2/weights/best.pt")
best.export(format="onnx", imgsz=640, opset=18,
            dynamic=False, simplify=True, nms=False, batch=1)
```

**`dynamic=False` 是硬性要求**：`dynamic=True` 會讓 ONNX 宣告 `['batch',3,'height','width']`，ORT C API 回傳 `-1`，而 `yolo_ort.c:104-111` 嚴格比對維度會直接拒絕載入。`nms=False` 同理——內嵌 NMS 會改變輸出張量 layout，C 端 decode 立即失效。

匯出後仍須放入 `tools/model_inbox/`，跑同一套 `model_check.py` 與 `build_model_json.py`。

### D. 正式模型替換規則

```text
舊正式模型保持不動
  → 新模型放 model_inbox/<run>/
  → model_check.py PASS
  → build_model_json.py 產生 sidecar
  → 備份舊 models/color_chart.onnx/json
  → 新模型更名佔用 models/color_chart.onnx（決策 (a)：單一 model 語義、runtime 路徑不動）
  → 同步 bin/models/
  → ORT shape/metadata 再驗一次
  → 才進行 roi-analyzer 實機測試
```

新模型未通過 shape、class 順序、raw output 驗收前，**不得覆蓋 `models/color_chart.onnx`，也不得部署到 `bin/models/`**。

**替換時序**：本次 6-class 模型與現行 exe 不相容（10 通道 vs 寫死 6 通道），故檔案替換須與 P2（`decode_v8_ex`＋validate 動態化）同批落地；否則色卡偵測會在替換後立即失效。在那之前 `models/color_chart.onnx` 保持舊 2-class 不動，新模型暫以 `models/color_chart-pvt.onnx` 存放。

## 3. 精定位（新模組 `pvt_locate.c/.h`；**主執行緒執行，MF2**）

輸入：box（YOLO，worker 經 `WM_APP_PVT_DETECT_DONE` 送回）＋原圖 view（BGRA，主執行緒安全）。

1. **box 外擴 30%**（`CC_INSET_RATIO` 反向，容 box 誤差）。
2. **Box 局部 Otsu 閾值**（MF3：v1.0 的自適應核 `box_w/4` 會掏空象限內部——40px 盤、象限半徑 14–20px，小核均值使純黑白區碎裂成 30 輪廓；agy 實測 40×40 crop Otsu 圓度 0.795，保留完整外輪廓；box 局部無照度梯度，Otsu 雙峰假設成立）。
3. **contour**：面積篩（box 面積 ±60%，容畸變）＋circularity ≥0.45（廣角下微橢圓）。
4. **72 點象限驗證**（實驗第 2 輪已寫好演算法，轉 C）：annulus 0.35r–0.85r、4 扇區 ×2 相位，對角同明暗、相鄰反差 ≥50、`min(opp,adj)` 取分最高相位；**門檻不過 → `reason=quadrant_mismatch`**（拒絕 YOLO 誤檢）。
5. **次像素中心**：`cvMoments`（開候選 contour）質心；輸出 `(cx, cy)`（原圖座標）＋`r_eq`。

**驗收基準**：中心與象限幾何中心差 ≤1px；4/4 靶全數定位或逐一回 reason。

## 4. 量測（`pvt_measure`，同模組）

靶標結構：圓內 4 象限（黑白相間）→ **量測對象是圓與背景之間的 4 段圓邊界**？——**更正**：依 §1 實驗圖，靶標為「黑圓內 2 白象限」（黑白相間圓盤），其銳利度意義在**象限邊界（十字接縫）與圓周**。拍板採固定 4 向（水平/垂直），故：

- **剖面線**：過質心的水平線與垂直線各 1 條（左右/上下共 4 個半剖面），長度 `1.5 × r_eq`，沿線逐 px 灰階取樣（原圖，BGRA→Y 用既有 `(77R+150G+29B+128)>>8`）。
- **對比（Michelson）**：剖面兩端平台區（外側 20% 長度）均值 → `(max−min)/(max+min)`；4 個半剖面各一值＋整體均值/最小值輸出。
- **Overshoot/undershoot**（MF4：**自建純 1D 函式，不重用 metrics_v2 S3**——其耦合 Sobel 法向取樣/Lab 空間/面積積分，且數學定義為面積積分非峰值定義）：`pvt_analyze_profile(const double *profile, int len, ...)`——端點平台均值（外側 20%）、Michelson 對比、峰值 overshoot/undershoot %（黑→白與白→黑沿各算，共 8 過渡）。
- **輸出**（每靶）：`contrast_avg/contrast_min`、`overshoot_avg/max`、`undershoot_avg/max`＋各向原始 16 組。
- **灰階警示**：剖面平台區不可得（靶出框/遮擋）→ 該向 `reason=profile_incomplete`，量測值留空。

## 5. UI 整合與 CSV

| 項目 | 設計 |
|---|---|
| 觸發 | **自動**（F：載入後 1 秒，隨換圖重跑） |
| 開關 | `View > Show PVT Targets`（新 IDM，勾選狀態；預設 ON）——只控 overlay 顯示，不控偵測 |
| Overlay | canvas 疊加：中心紅十字（41px，比照既有）＋半徑圈＋`P1..P4` 編號（編號順序=左上/右上/左下/右下，依質心排序）＋量測值小字（選中靶 hover/點選顯示——v1 先常駐顯示 `P1 c=0.87 o=2.1%` 格式） |
| 狀態列 | 複用訊息區：`PVT: 4/4 (23ms)`；失敗 `PVT: 3/4 (P2 quadrant_mismatch)` |
| CSV | **`pvt_*.csv` 獨立 append**（`<主檔名>_pvt.csv`，放原圖資料夾——沿用 ROI log 逐圖慣例）：header `id\tcx\tcy\tcontrast_avg\tcontrast_min\tovershoot_avg\tovershoot_max\tundershoot_avg\tundershoot_max\treason`，UTF-16LE＋BOM＋tab＋`fopen(ab)`——**writer 實作於 `export.c`（MF6）**：`Export_PvtLog(...)`，重用該檔 static 的 `write_utf16le`/BOM/EACCES 處理，不在 pvt_locate 另造 |
| 時機 | 偵測完成（LOCATE 類訊息後）一次寫入；重跑同圖→append 新列（時間戳由檔案系統，不加欄） |

## 6. 檔案與實作分段（兩 commit）

| 檔案 | 變更 |
|---|---|
| `tools/model_check.py`（新） | ONNX 驗收（input/output/nc/names/raw 判定）；輸出 `MODEL-CHECK: PASS|FAIL` 與實際 shape；**P0 先行，不依賴新模型** |
| `tools/build_model_json.py`（新） | 由通過驗收的 ONNX 產生 sidecar `color_chart.json`（names/use_class/output_shape/opset/source）；**P0** |
| `tools/colab_train_yolov8n.py`（新） | Colab 訓練＋匯出腳本（ultralytics、yolov8n.pt、imgsz 640、opset 18、**dynamic=False**、nms=False）；**P0** |
| `tools/model_inbox/`（新） | 暫存新模型與驗收報告；`.gitignore` 排除 `*.onnx`/`*.pt`/權重 |
| `models/color_chart-pvt.onnx/.pt` | 已驗收的 6-class 模型（sha256 `416779f7…`／`a47ed95e…`）；**P2 落地時更名佔用 `models/color_chart.onnx`**（決策 (a)），`.pt` 不進版控 |
| `src/pvt_locate.h/.c`（新） | §3 精定位＋§4 量測（純計算：輸入 view＋box；含 `pvt_analyze_profile` 純 1D 函式，**不依賴 metrics_v2**，可單元測試） |
| `src/yolo_ort.c/.h`、`yolo_post.c/.h` | MF1：validate_tensor 通道=`4+nc`（json 動態比對）＋**`yolo_decode_v8_ex` 逐 class 解析**（每 class 各自 NMS；色卡走 class1、PVT 走 class **3**） |
| `src/detect_worker.c/.h` | 單次推論後 decode_ex 雙 class；class3 boxes 經 `WM_APP_PVT_DETECT_DONE` 送回（**只回 box**，MF2） |
| `src/detect.c/.h`、`app_messages.h` | `WM_APP_PVT_DETECT_DONE`＋PVT 狀態 |
| `src/main.c`、`app.h` | 收 box 後主執行緒跑 §3/§4＋View 選單 IDM＋狀態列 |
| `src/canvas.c` | overlay（十字＋編號＋量測值） |
| `src/export.c/.h` | **`Export_PvtLog`**（MF6：重用 write_utf16le/BOM/EACCES） |
| CMake | `pvt_locate.c` |
| 單元測試 | `tests/test_pvt.c`：合成 4 象限圖（程式生成，含已知中心/對比/已知 overshoot 注入）驗 §3/§4——不依賴實照；另 `yolo_decode_v8_ex` 加 3-class 合成張量案例 |

| commit | 內容 |
|---|---|
| **P0** | `tools/model_check.py`＋`tools/build_model_json.py`＋`tools/colab_train_yolov8n.py`＋`model_inbox/.gitignore`——**不依賴新模型，可立即發包**；產出 6-class sidecar JSON |
| **P1** | `pvt_locate.c/.h`（§3+§4 純計算）＋`test_pvt.c`（合成圖單測）——**不依賴 model** |
| **P2** | `yolo_decode_v8_ex`（class 3）＋validate `4+nc`＋worker/main 接線＋overlay＋CSV＋**模型更名落地（決策 a）＋bin/models 同步** |

## 7. 風險

| 風險 | 對策 |
|---|---|
| 訓練樣本僅 13 張 | Ultralytics augment（翻轉/亮度/Mosaic）；邊角靶（畸變最大處）務必入訓練集 |
| model 缺 | 單一 model 缺失時色卡/PVT 同停；狀態列明確回報，不進行部分偵測 |
| **新模型與舊 exe 不相容**（10 通道 vs 寫死 6） | 檔案替換與 P2 同批；替換前 `models/color_chart.onnx` 保持舊 2-class |
| **誤用 `dynamic=True` 匯出** | `model_check.py` 明確要求固定維度，`-1`/dim_param 直接 FAIL |
| **class id 漂移**（設計書寫 class 2，實測是 3） | 一律以 ONNX metadata `names` 為準；sidecar JSON 由腳本產生，不手打 |
| pvt-target 信心值偏低（0.59–0.86，低於色卡 0.96） | 初值 `conf=0.25` 可接受；若精定位階段 quadrant 驗證常拒絕，再議補訓或提高解析度 |
| box 內局部閾值在強畸變邊角失效 | 象限驗證拒絕＋`reason` 回報，量測留空不給假數據 |
| CSV Excel 鎖檔（EACCES） | 沿用既有提示慣例 |
| wedge 只標中央一組 | 已知範圍（非缺陷）；延伸專案需補標註 |

## 8. 待確認（agy review 重點）

| # | 議題 | 本書立場 |
|---|---|---|
| Q1 | session 架構 | 單一 session、單次推論；Roboflow/Colab 只是模型來源 |
| Q2 | 象限驗證門檻 50 | 初始值；`test_pvt.c` 合成圖調參後凍結 |
| Q3 | overshoot 定義（S3 慣例沿用） | 峰值超平台%；與 metrics_v2 對齊概念，但函式不重用 |
| Q4 | CSV 為何不含時間戳 | 沿用 ROI log 慣例（檔案 mtime）；如需可另加欄 |
| Q5 | overlay 常駐顯示量測值 vs hover | v1 常駐（無 hover 基礎設施）；另案 |

## 9. agy round-1 判定（2026-10-08）

| # | 項目 | 判定 | 摘要 |
|---|---|---|---|
| 1 | 雙 session | HISTORICAL NO-GO | v1.2 review 紀錄；v1.3 已改為單 model 多 class |
| 2 | worker 接線/取消 | PASS | `if (!cancelled(job_seq))` 檢查點現成；單 session 沿用 seq |
| 3 | `yolo_decode_v8` 重用 | **FAIL（阻斷）** | nc/class channel 寫死；已升級為 `decode_v8_ex` |
| 4 | box 自適應核 | **FAIL** | `box_w/4` 掏空象限；已改局部 Otsu |
| 5 | S3 重用 | **FAIL** | 耦合法向取樣/Lab/面積積分且定義不同；已自建 1D 函式 |
| 6 | CSV 管線 | PASS WITH REFACTOR | `write_utf16le` 為 export.c static；採 `Export_PvtLog` |
| 7 | canvas overlay | PASS | 第三 overlay 契合既有 paint 結構 |
| 8 | 執行緒/隔離 | **FAIL** | worker 碰原圖 UAF；已採主執行緒精定位＋量測 |
| 9 | P1/P2 切分 | PASS | 維持 |

**最終：NO-GO→六項全採納（本版 v1.2）→ GO**。P1（純計算＋合成單測）可先行發包；P2 之 MF1（decode_ex）可用色卡 model（nc=2）回歸驗證不破壞既有路徑，PVT onnx 到位後僅換 model 檔＋json。

## 10. 模型工具驗收命令（v1.5）

```text
python tools/model_check.py models/color_chart-pvt.onnx
python tools/build_model_json.py models/color_chart-pvt.onnx \
    --names 122 color-chart focus-aid pvt-target wedgeH wedgeV \
    --pvt-class 3 --v1-class 1 \
    --output models/color_chart.json \
    --source "roboflow+colab ultralytics 8.4.178"
```

工具本身列入版本控制；`tools/model_inbox/` 的大型 ONNX、權重與 Roboflow API key 不列入版本控制。驗收報告可保存為 `model_check.txt`，不得包含 token/API key。

**實作前置**：先建立並測試 `model_check.py`、`build_model_json.py`；以本次已驗收模型（§11）為基準案例驗證工具本身。

## 11. 模型驗收實測紀錄（2026-10-17）

| 項目 | 值 |
|---|---|
| 檔案 | `models/color_chart-pvt.onnx`（12,778,102 bytes，sha256 `416779f793f09e04f9b48c5f629726122fc810d008acb7994fdc651da9c4b247`） |
| 權重 | `models/color_chart-pvt.pt`（6,517,802 bytes，sha256 `a47ed95e4cfc314c5c4fcfc23648eb8ab16be09800781bee51e4c6df4b8086d0`） |
| producer | pytorch 2.11.0 / Ultralytics 8.4.178 / opset 18 |
| input | `images [1, 3, 640, 640]` float32（固定） |
| output | `output0 [1, 10, 8400]` float32（固定，4+6） |
| metadata | `task=detect`, `head=Detect`, `end2end=False`, `imgsz=[640,640]`, `stride=32`, `dynamic=False`, `nms=False` |
| 節點 | 233（無 NonMaxSuppression / TopK） |
| names | `{0:'122', 1:'color-chart', 2:'focus-aid', 3:'pvt-target', 4:'wedgeH', 5:'wedgeV'}` |
| 捨棄的版本 | 首版 `dynamic=True`（input/output 為 `dim_param`，節點 321）——`yolo_ort.c:104-111` 會拒絕，已重匯 |

實測（`testdata` 對應 DC500Z 6 張 2592×1944，`conf=0.25`、`iou=0.7`）：

| 影像 | color-chart | focus-aid | pvt-target | wedgeH | wedgeV |
|---|---|---|---|---|---|
| AF667 / AFD661 / AFU640 × on/off（共 6 張） | 1 | 13 | **4** | 1 | 1 |

4 個 pvt-target 中心（跨 6 張差異 ≤1px）：`(822,443)`、`(1824,468)`、`(774,1218)`、`(1865,1226)`；信心值 0.57–0.86。使用者已確認標註圖「無多框、無漏框」。

### 來源

- [1] [Roboflow Supported Models](https://docs.roboflow.com/models/supported-models)——`yolov8n-640` 等模型別名與訓練/匯出支援表
- [2] [Roboflow Download Model Weights](https://docs.roboflow.com/models/model-weights/download-roboflow-model-weights)——ONNX 為建議匯出格式
- [3] [Ultralytics Model Export](https://docs.ultralytics.com/modes/export)——ONNX 匯出參數（imgsz/opset/dynamic/nms）
- [4] [Ultralytics Roboflow Integration](https://docs.ultralytics.com/integrations/roboflow)——Roboflow 資料集以 YOLO 格式匯出用於 Ultralytics 訓練
