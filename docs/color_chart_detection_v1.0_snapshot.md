<!-- File: docs/color_chart_detection_architecture.md -->
# 色卡偵測（YOLO）導入 — 架構書 v1.0

> 專案：`C:\Github\roi-analyzer`（純 C ＋ Win32 ＋ GDI ＋ WIC，ANSI 全 A 版，`-Wall -Wextra` 零警告，C11）
> 基準：`master`（v3.x，實作前以最新 commit 為準）
> 輸入資產：已訓練完成的色卡偵測模型 `.pt`（Ultralytics YOLO）
> 日期：2026-10-02
> 狀態：**草案待使用者確認（§19）→ agy 審查 → gh 實作**

---

## 修訂紀錄

| 版本 | 日期 | 內容 |
|---|---|---|
| v1.0 | 2026-10-02 | 初稿 |

---

## 0. 背景與目標

在影像開啟後，自動偵測畫面中的色卡（color chart），並把偵測框疊加顯示在影像上。

行為需求：

1. 影像**載入完成後持續顯示滿 1 秒**，才開始偵測。
2. 等待期間或偵測進行中，只要換圖（或關閉影像），就**立即停止偵測並回到 IDLE**；舊影像的結果一律不得顯示在新影像上。
3. 偵測不得阻塞 UI：拖曳、縮放、ROI 操作在偵測期間必須保持流暢。
4. 缺少模型或推論元件時，主程式其他功能照常運作，只停用偵測功能。

本版範圍（v1）：偵測、疊加顯示、狀態列提示、設定檔。
不在本版範圍：依偵測結果自動建立 ROI、色塊（patch）定位、GPU 加速（見 §18 後續階段）。

---

## 1. 前提與限制

| # | 限制 | 對設計的影響 |
|---|---|---|
| L1 | `.pt` 是 PyTorch 的序列化格式（pickle），只有 Python／LibTorch 能載入 | 純 C 程式**無法直接讀取 `.pt`**，必須先轉成 C 可以執行的格式（§3） |
| L2 | 專案是純 C、以 MinGW/GCC 建置 | 只能使用提供 **C API** 的推論引擎；C++ 函式庫（LibTorch、OpenCV DNN）需要額外包裝層，且 MSVC 與 MinGW 的 C++ ABI 不相容 |
| L3 | ANSI A 版 API | 推論引擎在 Windows 上以 `wchar_t` 路徑開啟模型，需要以 `MultiByteToWideChar(CP_ACP, ...)` 轉換 |
| L4 | `-Wall -Wextra` 零警告 | 第三方標頭須以 `-isystem` 引入；`GetProcAddress` 的轉型需要避開 `-Wcast-function-type`（§10.2） |
| L5 | 主程式的位元數（x86／x64） | 推論 DLL 的位元數必須與主程式一致（§19 Q6） |

---

## 2. 技術選型

| 方案 | 說明 | 優點 | 缺點 | 結論 |
|---|---|---|---|---|
| **A. ONNX Runtime C API** | `.pt` → `.onnx`，以 `onnxruntime.dll` 推論 | 官方提供純 C API（`onnxruntime_c_api.h`）；ABI 以函式表傳遞，可由 MinGW 動態載入；Ultralytics 官方支援匯出 ONNX；CPU 版 DLL 約十餘 MB；MIT 授權 | 需要轉檔步驟；需自行實作前後處理與 NMS | **採用** |
| B. LibTorch | 直接載入 TorchScript | 不需轉 ONNX | 只有 C++ API；官方只提供 MSVC 版本；DLL 合計超過 1 GB | 不採用 |
| C. OpenCV DNN | `cv::dnn::readNet` 讀 ONNX | 前處理方便 | C API 已廢棄，只能用 C++；需要自行以 MinGW 建置；新版 YOLO 運算子支援較慢 | 不採用 |
| D. Python 子程序 | 啟動 Python 執行 `ultralytics`，以 stdin/stdout 溝通 | 可直接用 `.pt`，實作最快 | 部署需要 Python ＋ PyTorch（GB 等級）；子程序啟動慢；行程管理與取消較複雜 | 不採用（僅用於轉檔與比對驗證） |
| E. Windows ML／DirectML | GPU 加速 | 速度快 | 增加相依與測試矩陣 | 後續階段（§18） |

---

## 3. 模型轉換（`.pt` → `.onnx`）

### 3.1 轉換環境

在開發機上以 Python 執行一次即可，**最終使用者不需要 Python**。

    pip install ultralytics onnx onnxruntime

### 3.2 匯出指令

    yolo export model=color_chart.pt format=onnx imgsz=640 opset=12 dynamic=False half=False simplify=True

| 參數 | 值 | 理由 |
|---|---|---|
| `imgsz` | 與訓練時相同（預設 640） | 輸入尺寸固定，C 端前處理較簡單 |
| `opset` | 12（或依 ONNX Runtime 版本調高） | 相容性佳 |
| `dynamic` | `False` | 固定形狀 `[1,3,640,640]`，避免動態形狀的額外處理 |
| `half` | `False` | CPU 推論使用 FP32 |
| `simplify` | `True` | 簡化計算圖 |
| `nms` | 不指定（不內嵌 NMS） | 由 C 端實作 NMS，行為可控。若模型為 YOLOv10／YOLO26 這類 end-to-end（免 NMS）架構，依 §9.3 的輸出格式處理 |

### 3.3 轉換後驗證（必做）

`tools/verify_onnx.py`：對同一組測試影像，分別以 `.pt` 與 `.onnx` 執行預測並比對：

    from ultralytics import YOLO

    pt = YOLO("color_chart.pt")
    ox = YOLO("color_chart.onnx")
    for path in ["samples/chart_01.jpg", "samples/chart_02.jpg"]:
        a = pt.predict(path, conf=0.25, iou=0.7, verbose=False)[0].boxes
        b = ox.predict(path, conf=0.25, iou=0.7, verbose=False)[0].boxes
        print(path, len(a), len(b))
        print(a.xyxy, a.conf)
        print(b.xyxy, b.conf)

通過條件：框數相同；座標差 2 px 以內；信心分數差 0.01 以內。

同時用下列指令記錄模型的輸入／輸出形狀，作為 C 端實作依據：

    python -c "import onnx; m=onnx.load('color_chart.onnx'); print(m.graph.input); print(m.graph.output); print(m.metadata_props)"

### 3.4 模型資訊紀錄

建立 `models/color_chart.json`，與 `.onnx` 一起版控與部署：

    {
        "source_pt_sha256": "<sha256 of color_chart.pt>",
        "ultralytics_version": "<x.y.z>",
        "yolo_family": "<v5 | v8 | v11 | v10 | yolo26>",
        "task": "detect",
        "imgsz": 640,
        "names": ["color_chart"],
        "output_shape": [1, 5, 8400],
        "opset": 12
    }

`names` 也存在 ONNX 的 metadata（key：`names`）中。v1 由設定檔或此 JSON 決定類別數與名稱，C 端不解析 JSON（只用於文件與驗證）；類別名稱寫入設定檔（§12）。

---

## 4. 整體架構

### 4.1 模組

    +--------------------------- UI 執行緒 ----------------------------+
    |                                                                  |
    |  影像載入流程 ──(卸載前)──> detect_on_image_unloading()          |
    |              ──(載入後)──> detect_on_image_loaded()              |
    |                                                                  |
    |  WM_TIMER(IDT_DETECT_DELAY) ──> detect_on_timer()                |
    |       └─ 前處理（letterbox 到 640x640 RGB，數 ms）              |
    |       └─ 放入 job slot ＋ SetEvent                              |
    |                                                                  |
    |  WM_APP_DETECT_DONE ──> detect_on_result()                       |
    |       └─ seq 比對 → 儲存結果 → InvalidateRect                  |
    |                                                                  |
    |  WM_PAINT ──> detect_overlay_draw()（疊加偵測框）               |
    +------------------------------------------------------------------+
                  | job slot（最新優先，容量 1）     ^ PostMessage
                  v                                  |
    +------------------------- 偵測工作執行緒 -------------------------+
    |  等待事件 → 取 job → 正規化成 NCHW float → ORT Run              |
    |  → 解碼輸出 → NMS → 座標還原 → PostMessage(結果)               |
    |  （每一步之間檢查取消旗標；Run 期間可由 UI 呼叫 terminate）     |
    +------------------------------------------------------------------+
                  |
                  v
           onnxruntime.dll（LoadLibrary 動態載入）＋ models/color_chart.onnx

### 4.2 檔案

| 檔案 | 職責 | 是否依賴 Win32 |
|---|---|---|
| `src/detect.h`、`src/detect.c` | 狀態機、計時器、對 UI 的公開 API、結果保存 | 是 |
| `src/detect_worker.c` | 工作執行緒、job slot、取消機制 | 是 |
| `src/yolo_ort.h`、`src/yolo_ort.c` | ONNX Runtime 動態載入、session 建立、推論 | 是（`LoadLibrary`） |
| `src/yolo_post.h`、`src/yolo_post.c` | letterbox 前處理、輸出解碼、NMS、座標還原 | **否**（純計算，可獨立單元測試） |
| `src/detect_overlay.c` | 在 `WM_PAINT` 中繪製偵測框與標籤 | 是（GDI） |
| `third_party/onnxruntime/include/` | `onnxruntime_c_api.h` 等標頭（鎖定版本） | — |
| `models/color_chart.onnx`、`models/color_chart.json` | 模型與模型資訊 | — |
| `tools/export_onnx.py`、`tools/verify_onnx.py` | 轉檔與驗證腳本 | — |
| `tests/test_yolo_post.c` | 前後處理單元測試 | — |

### 4.3 設計原則

- **所有 ONNX Runtime 呼叫只在工作執行緒中進行**（唯一例外：取消時由 UI 執行緒呼叫 `RunOptionsSetTerminate`，此函式官方允許跨執行緒呼叫）。
- **UI 執行緒只做輕量工作**：計時器、letterbox 前處理（輸出固定 640×640，成本與原圖大小無關）、接收結果、繪圖。
- **工作執行緒不存取影像記憶體**：前處理在 UI 執行緒完成，job 內只有 640×640 的副本，換圖時釋放舊影像不會造成存取衝突。
- **以序號（seq）判斷結果是否過期**：任何回傳結果都先比對序號，不符就丟棄。

---

## 5. 狀態機

### 5.1 狀態

| 狀態 | 意義 | 狀態列顯示 |
|---|---|---|
| `DETECT_DISABLED` | 功能停用（設定關閉、缺少 DLL、模型載入失敗） | `色卡偵測：停用（原因）` |
| `DETECT_IDLE` | 沒有影像，或已取消、尚未開始 | （空白） |
| `DETECT_PENDING` | 影像已載入，等待 1 秒計時 | （空白，避免閃爍） |
| `DETECT_RUNNING` | job 已送出，工作執行緒處理中 | `色卡偵測中…` |
| `DETECT_DONE` | 偵測完成（含 0 個結果） | `偵測到 N 個色卡（xx ms）`／`未偵測到色卡` |
| `DETECT_FAILED` | 推論過程發生錯誤 | `色卡偵測失敗：訊息` |

### 5.2 狀態轉移

    DISABLED（啟動失敗或設定關閉時停留於此，忽略所有事件）

               影像載入成功
      IDLE ───────────────────> PENDING
       ^ ^                        │  │
       │ │      換圖／關閉影像    │  │ 計時到（seq 相符）
       │ └────────────────────────┘  v
       │                          RUNNING ──結果（seq 相符，成功）──> DONE
       │       換圖／關閉影像      │    └──結果（seq 相符，錯誤）──> FAILED
       ├───────────────────────────┘
       │       換圖／關閉影像
       └──────────────────────────── DONE／FAILED

    任何狀態下收到 seq 不符的結果：丟棄並釋放，狀態不變。

| 目前狀態 | 事件 | 動作 | 下一狀態 |
|---|---|---|---|
| IDLE | 影像載入成功 | 記錄 `pending_seq = image_seq`；`SetTimer(1000 ms)` | PENDING |
| IDLE | 影像載入失敗 | 無 | IDLE |
| PENDING | `WM_TIMER` 且 `pending_seq == image_seq` | `KillTimer`；前處理；送出 job | RUNNING |
| PENDING | `WM_TIMER` 但 seq 不符或狀態已變 | `KillTimer`；忽略 | 不變 |
| PENDING | 換圖／關閉影像 | `KillTimer`；`image_seq++` | IDLE |
| RUNNING | 結果回來，seq 相符，成功 | 保存結果；`InvalidateRect` | DONE |
| RUNNING | 結果回來，seq 相符，錯誤 | 保存錯誤訊息 | FAILED |
| RUNNING | 換圖／關閉影像 | `image_seq++`；請求取消（§7）；清除疊加 | IDLE |
| DONE／FAILED | 換圖／關閉影像 | `image_seq++`；清除結果；`InvalidateRect` | IDLE |
| 任何 | 結果 seq 不符 | 釋放結果 | 不變 |
| 任何 | 程式結束 | §6.8 | — |

### 5.3 換圖的完整順序

換圖一律拆成兩個事件，確保「先回到 IDLE，再開始新影像的等待」：

1. **卸載舊影像之前**：呼叫 `detect_on_image_unloading()` → 取消計時器或進行中的偵測、清除結果 → **IDLE**。
2. 釋放舊影像、載入新影像。
3. **新影像載入成功之後**：呼叫 `detect_on_image_loaded()` → **PENDING**，重新計時 1 秒。
   載入失敗則停留在 IDLE。

「換圖」包含：開啟新檔、拖放檔案、上一張／下一張、重新載入同一檔案、關閉影像。只改變縮放、平移、ROI 模式**不算**換圖，不影響偵測。

---

## 6. 完整動作流程

### 6.1 程式啟動

1. 讀取設定（§12）。`enabled=0` 時進入 DISABLED，結束。
2. 以 exe 所在目錄的完整路徑載入 `onnxruntime.dll`（§10.1）。失敗 → DISABLED（原因：缺少 onnxruntime.dll）。
3. 取得 `OrtApi`。DLL 版本過舊（`GetApi` 回傳 `NULL`）→ DISABLED。
4. 建立工作執行緒（`_beginthreadex`，優先權 `THREAD_PRIORITY_BELOW_NORMAL`）。
5. 工作執行緒啟動後**立即在背景建立 session**（載入模型約數百 ms），並檢查輸入形狀是否為 `[1,3,imgsz,imgsz]`、輸出形狀是否為支援的格式（§9.3）。
6. session 建立失敗 → 工作執行緒以 `WM_APP_DETECT_INIT` 通知 UI → DISABLED（原因：模型載入失敗）。
7. 建立成功後執行一次暖機推論（全灰影像），讓第一次真正偵測不會較慢。

UI 不等待步驟 5～7，主視窗照常顯示。若使用者在 session 建立完成前就觸發偵測，job 會在 slot 中等待，建立完成後再處理。

### 6.2 開啟影像

1. 既有的載入流程在釋放舊影像前呼叫 `detect_on_image_unloading()`（§6.6）。
2. 新影像載入成功後呼叫 `detect_on_image_loaded()`：
   - 狀態為 DISABLED 時直接返回。
   - `pending_seq = image_seq`。
   - `SetTimer(hwnd, IDT_DETECT_DELAY, delay_ms, NULL)`。同一個 ID 再次呼叫 `SetTimer` 會重設計時，不會產生兩個計時器。
   - 狀態 → PENDING。

### 6.3 計時到期（滿 1 秒）

1. `WM_TIMER`（`IDT_DETECT_DELAY`）→ 先 `KillTimer`（一次性計時器）。
2. 檢查狀態是否為 PENDING，且 `pending_seq == image_seq`。
   **`KillTimer` 不會移除已經放進訊息佇列的 `WM_TIMER`**，所以這個檢查是必要的。
3. 從目前影像緩衝（32bpp BGRA）做 letterbox，輸出 640×640×3 的 RGB `uint8` 到 job 緩衝，並記錄縮放比例與補邊量（§9.1）。
4. 把 job（含 `seq`）放入 slot；若 slot 中已有尚未處理的 job，直接覆蓋（最新優先）。
5. `SetEvent(job_event)`；狀態 → RUNNING；更新狀態列。

### 6.4 工作執行緒處理

    等待 job_event 或 quit_event
      └─ 取出 job（鎖內），記錄 current_seq
      └─ [檢查點 1] 已取消？→ 回報 CANCELLED
      └─ uint8 RGB → float NCHW（/255）
      └─ [檢查點 2] 已取消？→ 回報 CANCELLED
      └─ 建立 RunOptions，在鎖內登記為「目前執行中」
      └─ Run()（期間可被 RunOptionsSetTerminate 中斷）
      └─ 在鎖內取消登記，釋放 RunOptions
      └─ [檢查點 3] 已取消？→ 回報 CANCELLED（即使 Run 已成功）
      └─ 解碼輸出 → 信心門檻 → NMS → 座標還原到原圖
      └─ [檢查點 4] 已取消？→ 回報 CANCELLED
      └─ 配置 DetectResult，PostMessage(WM_APP_DETECT_DONE)
           └─ PostMessage 失敗（視窗已關閉）→ 自行釋放結果

回報 CANCELLED 時不需要通知 UI（UI 已經回到 IDLE），直接釋放資源並回到等待。

### 6.5 結果回到 UI

1. `WM_APP_DETECT_DONE`，`lParam` 為 `DetectResult *`。
2. `result->seq != image_seq` → 釋放，結束（舊影像的結果）。
3. 狀態不是 RUNNING → 釋放，結束（防禦性檢查）。
4. 成功：把結果複製到 `detect.c` 內部的保存區，釋放 `result`，狀態 → DONE，`InvalidateRect`，更新狀態列。
5. 失敗：保存錯誤訊息，狀態 → FAILED，更新狀態列。

### 6.6 途中換圖（停止偵測）

`detect_on_image_unloading()`：

1. `image_seq++`（`InterlockedIncrement`），讓所有進行中與佇列中的結果都變成「過期」。
2. `KillTimer(hwnd, IDT_DETECT_DELAY)`。
3. 清空 job slot（尚未被取走的 job 直接丟棄）。
4. 若有執行中的 RunOptions，在鎖內呼叫 `RunOptionsSetTerminate`，讓 `Run()` 盡快返回。
5. 清除保存的偵測結果；`InvalidateRect`。
6. 狀態 → IDLE；清空狀態列的偵測訊息。

此函式**不等待**工作執行緒結束目前的推論，立即返回，UI 不會卡住。若工作執行緒仍在收尾，新影像的 job 會在 slot 中等待，處理順序正確。

### 6.7 手動重新偵測（選用）

選單「檢視 → 重新偵測色卡」：狀態不是 DISABLED 且有影像時，直接執行 §6.3 的第 3～5 步（不等 1 秒）。若正在 RUNNING，先依 §6.6 的第 1、3、4 步取消，再重新送出。

### 6.8 程式結束

在 `WM_DESTROY`（視窗仍有效）時：

1. `detect_on_image_unloading()`（取消所有工作）。
2. `SetEvent(quit_event)`。
3. `WaitForSingleObject(worker_thread, 3000)`。逾時則記錄警告，不強制終止執行緒（`TerminateThread` 可能讓 ORT 內部狀態損壞），直接繼續結束程序。
4. 以 `PeekMessage(..., WM_APP_DETECT_DONE, WM_APP_DETECT_DONE, PM_REMOVE)` 取出佇列中殘留的結果並釋放。
5. 在工作執行緒內（步驟 3 之前，收到 quit 時）釋放 session、session options、memory info、env。
6. `FreeLibrary(onnxruntime.dll)`（只有在執行緒確定結束後才執行）。

---

## 7. 取消機制

| 層級 | 機制 | 作用時機 |
|---|---|---|
| 序號 | `image_seq`（`volatile LONG`，以 `Interlocked*` 存取） | 所有結果在 UI 端比對，過期即丟棄。**這是正確性的最後防線** |
| 計時器 | `KillTimer` ＋ `WM_TIMER` 內的狀態與 seq 檢查 | PENDING 階段 |
| job slot | 換圖時清空 slot | job 已送出但工作執行緒尚未取走 |
| 檢查點 | 工作執行緒在每個步驟之間比對 `job.seq` 與 `image_seq` | 前處理、後處理階段 |
| ORT 中斷 | `RunOptionsSetTerminate` | `Run()` 執行中（通常數十到數百 ms） |

注意事項：

- `Run()` 被中斷時會回傳錯誤狀態。工作執行緒**以自己的取消判斷（seq 比對）**決定這是「取消」還是「失敗」，不要解析錯誤訊息字串。
- RunOptions 的生命週期由工作執行緒負責。UI 執行緒只能在鎖內、且指標不為 `NULL` 時呼叫 terminate；工作執行緒在 `Run()` 返回後，於鎖內把指標設為 `NULL` 再釋放。
- 即使所有中斷都失效，序號比對仍保證舊結果不會顯示在新影像上。

---

## 8. 執行緒與同步

| 物件 | 型別 | 用途 |
|---|---|---|
| `g_worker` | `HANDLE`（`_beginthreadex`） | 常駐工作執行緒（只建立一次，session 只載入一次） |
| `g_job_event` | auto-reset event | 通知有新 job |
| `g_quit_event` | manual-reset event | 通知結束 |
| `g_lock` | `CRITICAL_SECTION` | 保護 job slot 與「執行中的 RunOptions」指標 |
| `g_image_seq` | `volatile LONG` | 影像世代序號 |
| `WM_APP_DETECT_DONE` | `WM_APP + n` | 結果回傳（`lParam` = 結果指標，所有權轉移給 UI） |
| `WM_APP_DETECT_INIT` | `WM_APP + n + 1` | session 建立結果通知 |

規則：

- 工作執行緒對 UI 只使用 `PostMessage`，**禁止 `SendMessage`**（結束時 UI 執行緒在 `WaitForSingleObject` 等待，`SendMessage` 會造成死結）。
- `WM_APP` 編號需與專案既有的自訂訊息比對，避免衝突。
- 使用 `_beginthreadex` 而非 `CreateThread`，確保 CRT 在執行緒中正確初始化（後處理會用到 `qsort`、`expf` 等）。
- `SessionOptions` 的 `IntraOpNumThreads` 預設為「邏輯核心數的一半，至少 1」，保留 CPU 給 UI。

---

## 9. 前處理與後處理（`yolo_post.c`）

### 9.1 前處理：letterbox（UI 執行緒）

與 Ultralytics 的預測流程一致：

1. 縮放比例 \( r = \min(S / W,\ S / H) \)，其中 \( S \) 為 `imgsz`（640），\( W, H \) 為原圖寬高。
2. 縮放後尺寸 \( w' = \mathrm{round}(W r) \)、\( h' = \mathrm{round}(H r) \)。
3. 補邊置中：\( p_x = (S - w') / 2 \)、\( p_y = (S - h') / 2 \)，補邊顏色為 (114, 114, 114)。
4. 以雙線性內插取樣（對應 OpenCV `INTER_LINEAR`）。
5. 色彩順序：影像緩衝為 BGRA，輸出為 **RGB**（Ultralytics 模型輸入為 RGB）。
6. 輸出 640×640×3 的 `uint8`；記錄 \( r, p_x, p_y, W, H \) 到 job。

成本只與輸出尺寸（640×640）有關，與原圖大小無關，預估 5 ms 以內（T17 實測）。

影像緩衝格式以專案現況為準：若為 bottom-up DIB（`biHeight > 0`），取樣時需上下翻轉；若有灰階或索引色影像，以既有 WIC 轉換流程統一成 32bpp BGRA。

### 9.2 正規化（工作執行緒）

`uint8` RGB（HWC）→ `float` CHW，每個值除以 255，張量形狀 `[1, 3, S, S]`，緩衝約 4.9 MB（重複使用，不每次配置）。

### 9.3 輸出格式

依 §3.3 實際印出的輸出形狀決定。v1 **只實作確認後的那一種**，其餘在 session 建立時判定為不支援並停用（避免默默輸出錯誤結果）。

| 模型家族 | 輸出形狀 | 每個候選的內容 | 是否需要 NMS |
|---|---|---|---|
| YOLOv8／YOLO11 | `[1, 4+nc, N]`（640 時 N = 8400） | `cx, cy, w, h, score_0 … score_{nc-1}`（分數已過 sigmoid，無 objectness） | 需要 |
| YOLOv5 | `[1, N, 5+nc]`（640 時 N = 25200） | `cx, cy, w, h, obj, cls_0 …`；分數 = `obj × cls` | 需要 |
| YOLOv10／YOLO26（end-to-end） | `[1, K, 6]`（K 通常為 300） | `x1, y1, x2, y2, score, class_id` | 不需要 |

注意 YOLOv8 格式的記憶體排列是「通道優先」：第 \( c \) 個通道、第 \( i \) 個候選位於 `out[c * N + i]`。

### 9.4 解碼、NMS 與座標還原

1. 對每個候選取最大類別分數，低於 `conf`（預設 0.25）者捨棄。
2. 轉為 `x1, y1, x2, y2`（letterbox 座標）。
3. 依分數由高到低排序，以 IoU 門檻 `iou`（預設 0.70，與 Ultralytics 預測預設一致）進行**同類別**貪婪 NMS。
4. 最多保留 `max_det`（預設 100）個。
5. 還原到原圖座標：\( x = (x_{lb} - p_x) / r \)、\( y = (y_{lb} - p_y) / r \)，並限制在 \([0, W]\)、\([0, H]\)。
6. 寬或高小於 1 px 的框捨棄。

---

## 10. ONNX Runtime 整合（`yolo_ort.c`）

### 10.1 動態載入

- 以 `LoadLibraryExA(完整路徑, NULL, LOAD_WITH_ALTERED_SEARCH_PATH)` 載入 exe 同目錄下的 `onnxruntime.dll`。
  **必須使用完整路徑**：部分 Windows 版本的 `System32` 內也有 `onnxruntime.dll`（Windows ML 用），版本不受控制，誤載會造成 API 版本不符或行為不同。`LOAD_WITH_ALTERED_SEARCH_PATH` 讓它的相依 DLL 也從同一目錄載入。
- 只需要 `OrtGetApiBase` 一個匯出函式，其餘 API 都透過 `OrtApi` 函式表呼叫，**不需要連結 `.lib`**，也就沒有 MSVC／MinGW 匯入庫相容問題。
- DLL 不存在時，功能停用，主程式照常運作。

### 10.2 編譯注意事項

- 標頭以 `-isystem third_party/onnxruntime/include` 引入，避免第三方標頭觸發 `-Wall -Wextra` 警告。
- `GetProcAddress` 回傳 `FARPROC`，直接轉成其他函式指標型別會觸發 `-Wcast-function-type`（`-Wextra` 內含）。需經由 `void (*)(void)` 中轉，GCC 對此不發出警告。
- `ORT_API_VERSION` 由標頭定義。若 DLL 較舊，`GetApi(ORT_API_VERSION)` 會回傳 `NULL`，此時停用功能並提示 DLL 版本過舊。**標頭與 DLL 必須取自同一個 ONNX Runtime 發行版**，版本鎖定於 `third_party/onnxruntime/VERSION`。

### 10.3 Session 建立流程

1. `CreateEnv(ORT_LOGGING_LEVEL_WARNING, "roi-analyzer", &env)`
2. `CreateSessionOptions` → `SetIntraOpNumThreads` → `SetSessionGraphOptimizationLevel(ORT_ENABLE_ALL)`
3. 模型路徑以 `MultiByteToWideChar(CP_ACP, ...)` 轉為 `wchar_t`，`CreateSession(env, wpath, opts, &session)`
4. `CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &meminfo)`
5. 以 `SessionGetInputName`／`SessionGetOutputName` 取得名稱（以預設 allocator 配置，結束時 `AllocatorFree`）
6. 以 `SessionGetInputTypeInfo` 檢查輸入形狀、`SessionGetOutputTypeInfo` 檢查輸出形狀，並對照 §9.3 判定格式；不符合則停用

### 10.4 程式碼示意

以下僅為示意，函式與結構名稱以實作為準：

    /* src/yolo_ort.c（示意） */
    #include <windows.h>
    #include <onnxruntime_c_api.h>

    typedef const OrtApiBase *(ORT_API_CALL *PFN_OrtGetApiBase)(void);

    static HMODULE g_ort_dll;
    static const OrtApi *g_ort;

    /* 回傳 0 = 成功 */
    int yolo_ort_load(void)
    {
        char path[MAX_PATH];
        char *slash;
        PFN_OrtGetApiBase get_base;
        const OrtApiBase *base;
        DWORD n;

        n = GetModuleFileNameA(NULL, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) {
            return -1;
        }
        slash = strrchr(path, '\\');
        if (slash == NULL || (size_t)(slash - path) + 1 + sizeof("onnxruntime.dll") > sizeof(path)) {
            return -1;
        }
        strcpy(slash + 1, "onnxruntime.dll");

        g_ort_dll = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (g_ort_dll == NULL) {
            return -1;
        }

        /* 經由 void (*)(void) 中轉，避免 -Wcast-function-type */
        get_base = (PFN_OrtGetApiBase)(void (*)(void))GetProcAddress(g_ort_dll, "OrtGetApiBase");
        if (get_base == NULL) {
            return -1;
        }
        base = get_base();
        g_ort = base->GetApi(ORT_API_VERSION);
        if (g_ort == NULL) {
            return -2; /* DLL 版本過舊 */
        }
        return 0;
    }

    /* 推論一次；run_opts 由呼叫端建立並登記，以支援跨執行緒中斷 */
    int yolo_ort_run(YoloSession *s, float *input, OrtRunOptions *run_opts,
                     const float **out, int64_t out_dims[3])
    {
        OrtStatus *st;
        OrtValue *in_val = NULL;
        const int64_t in_shape[4] = { 1, 3, s->imgsz, s->imgsz };
        size_t in_bytes = (size_t)3 * (size_t)s->imgsz * (size_t)s->imgsz * sizeof(float);

        st = g_ort->CreateTensorWithDataAsOrtValue(s->meminfo, input, in_bytes,
                in_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in_val);
        if (st != NULL) {
            yolo_ort_set_error(s, st); /* 複製訊息後 ReleaseStatus */
            return -1;
        }

        if (s->out_val != NULL) {
            g_ort->ReleaseValue(s->out_val);
            s->out_val = NULL;
        }
        st = g_ort->Run(s->session, run_opts,
                (const char *const *)&s->in_name, (const OrtValue *const *)&in_val, 1,
                (const char *const *)&s->out_name, 1, &s->out_val);
        g_ort->ReleaseValue(in_val);
        if (st != NULL) {
            yolo_ort_set_error(s, st); /* 呼叫端以 seq 判斷是取消還是失敗 */
            return -1;
        }

        st = g_ort->GetTensorMutableData(s->out_val, (void **)out);
        if (st != NULL) {
            yolo_ort_set_error(s, st);
            return -1;
        }
        out_dims[0] = s->out_dims[0];
        out_dims[1] = s->out_dims[1];
        out_dims[2] = s->out_dims[2];
        return 0;
    }

狀態機與計時器（`detect.c`，示意）：

    /* src/detect.c（示意） */
    #define IDT_DETECT_DELAY 0x5D01

    static HWND g_hwnd;
    static DetectState g_state = DETECT_DISABLED;
    static volatile LONG g_image_seq;
    static LONG g_pending_seq;

    void detect_on_image_unloading(void)
    {
        InterlockedIncrement(&g_image_seq);
        KillTimer(g_hwnd, IDT_DETECT_DELAY);
        if (g_state == DETECT_DISABLED) {
            return;
        }
        worker_cancel_all();      /* 清空 slot ＋ 在鎖內 RunOptionsSetTerminate */
        detect_clear_result();
        InvalidateRect(g_hwnd, NULL, FALSE);
        g_state = DETECT_IDLE;
        detect_update_status();
    }

    void detect_on_image_loaded(void)
    {
        if (g_state == DETECT_DISABLED) {
            return;
        }
        g_pending_seq = InterlockedCompareExchange(&g_image_seq, 0, 0);
        SetTimer(g_hwnd, IDT_DETECT_DELAY, g_cfg.delay_ms, NULL);
        g_state = DETECT_PENDING;
    }

    /* WM_TIMER(IDT_DETECT_DELAY) 時由 WndProc 呼叫；影像緩衝由呼叫端提供 */
    void detect_on_timer(const unsigned char *bgra, int w, int h, int stride)
    {
        DetectJob *job;
        LONG seq;

        KillTimer(g_hwnd, IDT_DETECT_DELAY);
        seq = InterlockedCompareExchange(&g_image_seq, 0, 0);
        if (g_state != DETECT_PENDING || g_pending_seq != seq || bgra == NULL) {
            return; /* 已在佇列中的過期 WM_TIMER */
        }

        job = worker_acquire_job_buffer();
        yolo_letterbox_bgra(bgra, w, h, stride, g_cfg.imgsz, job->rgb, &job->tf);
        job->seq = seq;
        worker_submit(job);       /* 覆蓋 slot ＋ SetEvent */

        g_state = DETECT_RUNNING;
        detect_update_status();
    }

    /* WM_APP_DETECT_DONE */
    void detect_on_result(DetectResult *r)
    {
        LONG seq = InterlockedCompareExchange(&g_image_seq, 0, 0);

        if (r->seq != seq || g_state != DETECT_RUNNING) {
            free(r);
            return;
        }
        detect_store_result(r);
        g_state = (r->status == 0) ? DETECT_DONE : DETECT_FAILED;
        free(r);
        InvalidateRect(g_hwnd, NULL, FALSE);
        detect_update_status();
    }

後處理（`yolo_post.c`，YOLOv8／YOLO11 格式，示意）：

    /* src/yolo_post.c（示意） */
    typedef struct DetectBox {
        float x0, y0, x1, y1;
        float score;
        int class_id;
    } DetectBox;

    typedef struct LetterboxTf {
        float scale, pad_x, pad_y;
        int src_w, src_h;
    } LetterboxTf;

    static float clampf(float v, float lo, float hi)
    {
        return (v < lo) ? lo : (v > hi) ? hi : v;
    }

    /* out: [1, 4+nc, n]，回傳候選數 */
    int yolo_decode_v8(const float *out, int nc, int n, float conf,
                       const LetterboxTf *tf, DetectBox *boxes, int cap)
    {
        int i, c, count = 0;

        for (i = 0; i < n && count < cap; i++) {
            float best = 0.0f;
            int best_c = -1;
            float cx, cy, w, h;
            DetectBox *b;

            for (c = 0; c < nc; c++) {
                float s = out[(size_t)(4 + c) * (size_t)n + (size_t)i];
                if (s > best) {
                    best = s;
                    best_c = c;
                }
            }
            if (best_c < 0 || best < conf) {
                continue;
            }
            cx = out[(size_t)0 * (size_t)n + (size_t)i];
            cy = out[(size_t)1 * (size_t)n + (size_t)i];
            w  = out[(size_t)2 * (size_t)n + (size_t)i];
            h  = out[(size_t)3 * (size_t)n + (size_t)i];

            b = &boxes[count];
            b->x0 = clampf((cx - w * 0.5f - tf->pad_x) / tf->scale, 0.0f, (float)tf->src_w);
            b->y0 = clampf((cy - h * 0.5f - tf->pad_y) / tf->scale, 0.0f, (float)tf->src_h);
            b->x1 = clampf((cx + w * 0.5f - tf->pad_x) / tf->scale, 0.0f, (float)tf->src_w);
            b->y1 = clampf((cy + h * 0.5f - tf->pad_y) / tf->scale, 0.0f, (float)tf->src_h);
            b->score = best;
            b->class_id = best_c;
            if (b->x1 - b->x0 >= 1.0f && b->y1 - b->y0 >= 1.0f) {
                count++;
            }
        }
        return count;
    }

    static int cmp_score_desc(const void *a, const void *b)
    {
        float sa = ((const DetectBox *)a)->score;
        float sb = ((const DetectBox *)b)->score;
        return (sa < sb) ? 1 : (sa > sb) ? -1 : 0;
    }

    static float iou(const DetectBox *a, const DetectBox *b)
    {
        float ix0 = (a->x0 > b->x0) ? a->x0 : b->x0;
        float iy0 = (a->y0 > b->y0) ? a->y0 : b->y0;
        float ix1 = (a->x1 < b->x1) ? a->x1 : b->x1;
        float iy1 = (a->y1 < b->y1) ? a->y1 : b->y1;
        float iw = ix1 - ix0, ih = iy1 - iy0, inter, uni;

        if (iw <= 0.0f || ih <= 0.0f) {
            return 0.0f;
        }
        inter = iw * ih;
        uni = (a->x1 - a->x0) * (a->y1 - a->y0) + (b->x1 - b->x0) * (b->y1 - b->y0) - inter;
        return (uni > 0.0f) ? inter / uni : 0.0f;
    }

    /* 同類別貪婪 NMS，結果原地壓縮，回傳保留數 */
    int yolo_nms(DetectBox *boxes, int count, float iou_thr, int max_det)
    {
        int i, j, kept = 0;

        qsort(boxes, (size_t)count, sizeof(DetectBox), cmp_score_desc);
        for (i = 0; i < count && kept < max_det; i++) {
            int keep = 1;
            for (j = 0; j < kept; j++) {
                if (boxes[j].class_id == boxes[i].class_id &&
                    iou(&boxes[j], &boxes[i]) > iou_thr) {
                    keep = 0;
                    break;
                }
            }
            if (keep) {
                boxes[kept++] = boxes[i];
            }
        }
        return kept;
    }

候選緩衝上限（`cap`）建議設為 `N`（例如 8400），以靜態或一次性配置的陣列保存，不在每次推論時配置。

---

## 11. UI 顯示

| 項目 | 規格 |
|---|---|
| 偵測框 | 以影像座標保存，`WM_PAINT` 時用既有的「影像 → 視窗」座標轉換函式換算，縮放、平移後位置正確 |
| 樣式 | 2 px 實線，顏色與 ROI 框明顯不同（建議洋紅色 `RGB(255,0,255)`），框左上角顯示 `類別名 0.93` |
| 圖層順序 | 影像 → 偵測框 → ROI 框（ROI 是使用者操作的主體，放在最上層） |
| 顯示開關 | 選單「檢視 → 顯示色卡偵測框」，對應設定 `show_overlay` |
| 狀態列 | 依 §5.1 的狀態顯示文字；若專案狀態列有多個區塊，使用獨立區塊，不覆蓋既有資訊 |
| 游標 | 偵測期間**不**改為等待游標（偵測是背景工作，不應暗示 UI 忙碌） |
| 選單 | 「重新偵測色卡」（§6.7）；DISABLED 時灰化並在狀態列說明原因 |

v1 不改變 ROI、Grid、Export 的任何行為；偵測結果不寫入 Export。

---

## 12. 設定

沿用專案既有的設定機制；若沒有，則使用 exe 同目錄的 `roi-analyzer.ini`，以 `GetPrivateProfileStringA`／`GetPrivateProfileIntA` 讀取。

    [detect]
    enabled=1
    model=models\color_chart.onnx
    names=color_chart
    imgsz=640
    delay_ms=1000
    conf=0.25
    iou=0.70
    max_det=100
    threads=0
    show_overlay=1

| 鍵 | 範圍 | 說明 |
|---|---|---|
| `enabled` | 0／1 | 0 = 完全不載入 DLL |
| `model` | 路徑 | 相對路徑以 exe 所在目錄為基準 |
| `names` | 逗號分隔 | 類別名稱，數量必須等於模型類別數 nc，不符則停用 |
| `imgsz` | 32 的倍數 | 必須與模型輸入形狀一致，不符則停用 |
| `delay_ms` | 200～10000 | 影像載入完成到開始偵測的等待時間 |
| `conf` | 0.01～0.99 | 信心門檻 |
| `iou` | 0.10～0.95 | NMS IoU 門檻 |
| `max_det` | 1～300 | 最多保留框數 |
| `threads` | 0～64 | 0 = 自動（邏輯核心數的一半，至少 1） |
| `show_overlay` | 0／1 | 是否繪製偵測框 |

超出範圍的值使用預設值，並在偵錯輸出中記錄。

---

## 13. 錯誤處理

| 情況 | 處理 | 使用者看到的訊息 |
|---|---|---|
| 缺少 `onnxruntime.dll` | DISABLED | `色卡偵測：停用（找不到 onnxruntime.dll）` |
| DLL 版本過舊 | DISABLED | `色卡偵測：停用（onnxruntime.dll 版本過舊）` |
| DLL 位元數不符（`LoadLibrary` 失敗，`ERROR_BAD_EXE_FORMAT`） | DISABLED | `色卡偵測：停用（onnxruntime.dll 位元數不符）` |
| 模型檔不存在 | DISABLED | `色卡偵測：停用（找不到模型檔）` |
| 模型輸入／輸出形狀不支援，或 `names`、`imgsz` 與模型不符 | DISABLED | `色卡偵測：停用（模型格式不支援）` |
| 單次推論失敗（非取消） | FAILED，下一次換圖會重新嘗試 | `色卡偵測失敗：<ORT 訊息前 100 字>` |
| 記憶體配置失敗 | FAILED | `色卡偵測失敗：記憶體不足` |
| 結束時工作執行緒逾時 | 記錄警告，繼續結束 | 無 |

所有停用與失敗都**不使用 `MessageBox`**，只顯示在狀態列，避免每次開圖都跳出對話框。詳細錯誤寫入既有的偵錯輸出（`OutputDebugStringA` 或 log）。

---

## 14. 建置與部署

### 14.1 建置

- Makefile 新增 `src/detect.c`、`src/detect_worker.c`、`src/yolo_ort.c`、`src/yolo_post.c`、`src/detect_overlay.c`。
- `CFLAGS` 新增 `-isystem third_party/onnxruntime/include`。
- **不連結** ONNX Runtime 的匯入庫（動態載入）。
- 新增測試目標 `test_yolo_post`（只編譯 `yolo_post.c` ＋ `tests/test_yolo_post.c`，不需要 Win32 或 ORT）。
- 建置後步驟：把 `onnxruntime.dll` 與 `models/` 複製到輸出目錄。

### 14.2 部署目錄

    roi-analyzer\
        roi-analyzer.exe
        onnxruntime.dll
        roi-analyzer.ini          （選用）
        models\
            color_chart.onnx
            color_chart.json
        licenses\
            onnxruntime_LICENSE.txt
            THIRD_PARTY_NOTICES.txt

刪除 `onnxruntime.dll` 或 `models\` 之後，程式必須仍能正常執行其他功能（T13、T14）。

### 14.3 授權

| 元件 | 授權 | 注意事項 |
|---|---|---|
| ONNX Runtime | MIT | 隨附授權文字即可 |
| Ultralytics YOLO（訓練框架與以其訓練的權重） | **AGPL-3.0**（另有商業授權） | 若程式會對外散布，以 Ultralytics 訓練的模型可能受 AGPL 約束。**散布前需確認授權方式**（§19 Q8） |

---

## 15. 效能預估

| 項目 | 預估（YOLOv8n 等級，640，CPU） | 驗證 |
|---|---|---|
| DLL 載入 | 50 ms 以內 | T18 |
| session 建立＋暖機 | 200～800 ms（背景，不阻塞 UI） | T18 |
| letterbox（UI 執行緒） | 5 ms 以內 | T17 |
| 單次推論 | 40～200 ms（依 CPU 與模型大小） | T18 |
| 後處理＋NMS | 5 ms 以內 | T18 |
| 記憶體增加 | DLL ＋ 模型 ＋ 緩衝，約 50～150 MB | T19 |

若模型為 s／m 等較大版本，推論時間可能到數百 ms，但因為在背景執行，不影響 UI，只影響結果出現的時間。

---

## 16. 測試計畫

### 16.1 功能與時序

| # | 測試 | 步驟 | 通過條件 |
|---|---|---|---|
| T1 | 正常偵測 | 開啟含色卡的影像，等待 | 約 1 秒後開始偵測，數百 ms 內顯示框；狀態列顯示框數與耗時 |
| T2 | 1 秒內換圖 | 開圖後 0.5 秒內換到另一張 | 第一張**完全不執行**推論（偵錯輸出可確認）；第二張重新計時 1 秒 |
| T3 | 偵測中換圖 | 在 RUNNING 期間換圖（可暫時把 `threads=1` 並用大模型拉長推論時間） | 舊結果不顯示；狀態回到 IDLE，再進入新影像的 PENDING；程式不當機 |
| T4 | 快速連續換圖 | 以方向鍵連續換圖 50 次（間隔約 0.2 秒），最後停在一張 | 只有最後一張出現偵測結果；結果與單獨開啟該圖時一致 |
| T5 | 關閉影像 | 在 PENDING、RUNNING、DONE 三種狀態下分別關閉影像 | 疊加清除，狀態回到 IDLE，不會在空白畫面上出現框 |
| T6 | 重新載入同一張 | DONE 之後重新載入同一檔案 | 結果清除後重新計時、重新偵測 |
| T7 | 縮放／平移 | DONE 後縮放、平移 | 框位置正確跟隨影像；不會重新觸發偵測 |
| T8 | 無色卡影像 | 開啟不含色卡的影像 | 狀態列顯示「未偵測到色卡」，沒有框 |
| T9 | 手動重新偵測 | 執行選單「重新偵測色卡」 | 立即偵測，結果與自動偵測一致 |
| T10 | 程式結束 | 在 PENDING、RUNNING 狀態下關閉程式 | 3 秒內正常結束，沒有當機或殘留程序 |

### 16.2 正確性

| # | 測試 | 通過條件 |
|---|---|---|
| T11 | 與 Python 結果比對 | 對 10 張以上測試影像，C 端結果與 `verify_onnx.py` 的 ONNX 結果比較：框數相同、座標差 2 px 以內、分數差 0.01 以內（雙線性內插實作差異造成的微小誤差可接受） |
| T12 | 影像尺寸與方向 | 直式、橫式、極小（如 100×80）、極大（如 8000×6000）、非 4 倍數寬度的影像，框座標都正確；bottom-up DIB 不會上下顛倒 |
| T20 | 單元測試 | `test_yolo_post`：letterbox 參數、座標還原、IoU、NMS（重疊／不重疊／不同類別）、空輸入，全部通過 |

### 16.3 錯誤處理與部署

| # | 測試 | 通過條件 |
|---|---|---|
| T13 | 缺少 DLL | 移除 `onnxruntime.dll` 後啟動：狀態列顯示停用原因，其他功能正常 |
| T14 | 缺少模型 | 移除 `models\` 後啟動：同上 |
| T15 | 錯誤模型 | 放入不同輸入尺寸或不同任務（如分割模型）的 ONNX：停用並顯示「模型格式不支援」，不輸出錯誤框 |
| T16 | `enabled=0` | 不載入 DLL（以 Process Explorer 確認），選單灰化 |
| T21 | System32 同名 DLL | 在含有系統 `onnxruntime.dll` 的 Windows 版本上執行：載入的是 exe 同目錄的版本（Process Explorer 確認路徑） |
| T22 | 中文路徑 | 程式與模型放在含中文的目錄下：模型能正常載入 |

### 16.4 效能與資源

| # | 測試 | 通過條件 |
|---|---|---|
| T17 | UI 執行緒耗時 | `detect_on_timer()` 以 `QueryPerformanceCounter` 量測，8000×6000 影像下仍在 20 ms 以內 |
| T18 | 推論耗時 | 記錄 session 建立、暖機、單次推論、後處理時間，填入 §15 |
| T19 | 長時間執行 | 連續換圖 500 次後，記憶體與 handle 數量沒有持續上升（工作管理員或 Process Explorer） |
| T23 | UI 回應 | RUNNING 期間拖曳視窗、縮放影像、編輯 ROI，沒有明顯卡頓 |

### 16.5 建置

| # | 測試 | 通過條件 |
|---|---|---|
| T24 | 編譯 | `-Wall -Wextra` 完整建置零警告（含新檔案與 `-isystem` 引入的標頭） |
| T25 | 既有功能回歸 | ROI、Grid、Export、Report 的既有測試全部通過 |

---

## 17. 風險

| # | 風險 | 可能性 | 影響 | 對策 |
|---|---|---|---|---|
| R1 | 模型家族或任務類型與假設不同（例如 OBB、pose） | 中 | 高 | §3.3 先印出輸出形狀；§9.3 只實作確認的格式，其餘停用 |
| R2 | ONNX 結果與 `.pt` 有差異 | 低 | 中 | §3.3 轉換驗證、T11 |
| R3 | C 端前處理與 Ultralytics 不一致（色彩順序、補邊、內插） | 中 | 高 | §9.1 明確規格、T11 比對 |
| R4 | 換圖時舊結果顯示在新圖上 | 低（有序號機制） | 高 | §7 多層取消、T3、T4 |
| R5 | 結束時死結或當機 | 低 | 高 | 禁止 `SendMessage`、逾時等待、T10 |
| R6 | 誤載系統內建的 `onnxruntime.dll` | 中 | 中 | 完整路徑載入（§10.1）、T21 |
| R7 | 主程式為 x86，但只準備了 x64 DLL | 中 | 中 | §19 Q6 先確認；T13 顯示明確原因 |
| R8 | Ultralytics AGPL-3.0 授權 | 視散布方式 | 高 | §14.3、§19 Q8 |
| R9 | 低階 CPU 推論過慢 | 低 | 低 | 背景執行；可改用 n 版模型或後續加入 DirectML |
| R10 | 部署體積增加（DLL 約十餘 MB ＋ 模型） | 確定 | 低 | 接受；記錄於發行說明 |

---

## 18. 實作階段

| 階段 | 內容 | 完成條件 |
|---|---|---|
| P0 模型準備 | 匯出 ONNX、執行 `verify_onnx.py`、填寫 `color_chart.json`、確認輸出形狀 | §3.3 通過；§19 Q1～Q3 已回覆 |
| P1 推論核心 | `yolo_ort.c` ＋ `yolo_post.c`；以命令列測試程式讀一張影像並輸出框座標 | T11、T20 通過 |
| P2 執行緒與狀態機 | `detect.c` ＋ `detect_worker.c`；接上影像載入流程與 `WM_TIMER`；先以偵錯輸出驗證 | T1～T6、T10 通過 |
| P3 UI | `detect_overlay.c`、狀態列、選單、設定檔 | T7～T9、T16、T23 通過 |
| P4 錯誤處理與部署 | §13 全部情況、建置複製、授權文件 | T12～T15、T21、T22、T24 通過 |
| P5 驗收 | 全部測試、效能數據回填 §15 | T1～T25 全部通過 |

後續版本（不在 v1）：

- 依偵測框自動建立 ROI，或在框內定位 24 格色塊（可能需要 pose／角點模型或第二階段演算法）。
- DirectML GPU 加速（`SessionOptionsAppendExecutionProvider_DML`）。
- 把偵測結果寫入 Export（需另行定義欄位）。

---

## 19. 待使用者確認

| # | 項目 | 為什麼需要確認 | 建議 |
|---|---|---|---|
| Q1 | YOLO 版本（v5／v8／v11／v10／YOLO26） | 決定輸出格式與是否需要 NMS（§9.3） | 提供 `ultralytics` 版本與訓練指令，或直接提供 §3.3 印出的輸出形狀 |
| Q2 | 任務類型（detect／obb／pose／segment） | 色卡若會旋轉，訓練時可能用 OBB；本文件以 detect 為準 | 確認為 detect；若為其他類型，需修訂 §9 |
| Q3 | 類別數與名稱、訓練時的 `imgsz` | 決定 `names`、`imgsz` 設定 | 提供 `data.yaml` |
| Q4 | 偵測結果的用途 | v1 只顯示框；若要自動建立 ROI，屬後續版本 | v1 只顯示，確認後再規劃 |
| Q5 | 等待時間的起算點 | 本文件定義為「影像載入完成」後滿 1 秒 | 同意，並開放 `delay_ms` 設定 |
| Q6 | 主程式位元數（x86／x64） | DLL 必須一致 | 提供目前建置的目標平台 |
| Q7 | 是否需要 GPU | 影響部署與測試範圍 | v1 只用 CPU |
| Q8 | 程式是否對外散布 | Ultralytics 為 AGPL-3.0 | 若會散布，先確認授權方式 |
| Q9 | 換圖後，若新影像與舊影像相同（例如重新載入），是否保留舊結果 | 本文件設計為一律重新偵測 | 一律重新偵測（行為單純，避免快取失效問題） |

---

## 20. 自我審查紀錄

| # | 檢查項目 | 結果 |
|---|---|---|
| 1 | 純 C 無法讀 `.pt` 的限制是否有解法 | 以 ONNX ＋ ONNX Runtime C API 解決（§1、§2、§3） |
| 2 | 「滿 1 秒才偵測」 | `SetTimer` 一次性計時；起算點為載入完成（§6.2、Q5） |
| 3 | 等待期間換圖 | `KillTimer` ＋ `WM_TIMER` 內狀態與 seq 檢查（處理已入佇列的訊息）（§6.3、§7） |
| 4 | 偵測中換圖 | 清空 slot、`RunOptionsSetTerminate`、檢查點、序號丟棄（§6.6、§7） |
| 5 | 換圖後回到 IDLE，再開始新影像的等待 | 卸載／載入拆成兩個事件（§5.3） |
| 6 | 舊結果不會顯示在新圖上 | 序號比對為最後防線（§7、T3、T4） |
| 7 | UI 不阻塞 | ORT 只在工作執行緒；UI 端 letterbox 成本固定（§4.3、T17、T23） |
| 8 | 換圖時釋放影像不會造成存取衝突 | 工作執行緒只使用 job 內的 640×640 副本（§4.3） |
| 9 | 結束時不死結、不洩漏 | 禁止 `SendMessage`、逾時等待、清除殘留訊息（§6.8、T10） |
| 10 | 元件缺失時主程式可用 | 動態載入、DISABLED 狀態、只用狀態列提示（§10.1、§13） |
| 11 | `-Wall -Wextra` 零警告 | `-isystem`、`GetProcAddress` 中轉轉型（§10.2、T24） |
| 12 | 系統內建同名 DLL | 完整路徑載入（§10.1、T21） |
| 13 | 前處理與訓練一致 | RGB、letterbox 置中、補邊 114、雙線性（§9.1、T11） |
| 14 | 不同 YOLO 版本的輸出差異 | 依實際形狀判定，不支援就停用（§9.3、R1） |
| 15 | 授權 | AGPL-3.0 風險已列出（§14.3、Q8） |