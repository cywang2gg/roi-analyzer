# ROI Analyzer — 系統架構設計書

> 版本：v3.0 | 日期：2026-09-28 | 比較視窗 Metrics 指標分析（一期空間域＋二期 FFT／Lab）＋UTF-8 HTML 報告（v2.9 監控併入）

## 變更歷史

- **v3.0（2026-09-28）**：比較視窗 Metrics 指標分析（`src/metrics.h/.c` 約 600 行：一期空間域 Laplacian／Sobel／Tenengrad／Brenner／8 向對比／雜訊 SNR／亮度四區／飽和度，`metrics_workspace_t` 單趟 3 行快取峰值 <10MB。`src/fft.h/.c` 約 150 行：Radix-2 2D FFT＋PSD 三頻帶。色偏共用 `analyze.c` Lab 不另轉 CbCr。`src/report.h/.c` 約 450 行：單一 HTML＋UTF-8 BOM＋`<meta charset>`＋CP_ACP→UTF-16→UTF-8 兩段轉碼＋Base64 邊緣圖＋`ShellExecuteA` 開瀏覽器。`src/metrics_async.h/.c` 約 200 行：4MP 門檻 `CreateThread`＋`CmpImage_Ref` 保護＋`WM_APP_METRICS_DONE (WM_APP+103)`。`image_wic` 加記憶體 PNG 編碼）。UI：V1／V2 工具列 `Metrics` 鈕（沿用 `Compare_PreTranslate` 命令段）＋`View > Metrics Report for Compare (Ctrl+M)`（IDM 109／110）＋Stage2 開關＋`IDD_METRICS_PROGRESS`（240）進度框。詳見 `compare_metrics_architecture.md`。

- **v2.9（2026-09-28）**：資料夾監控（`src/monitor.h/.c` 約 600 行：`ReadDirectoryChangesW`＋Overlapped＋背景 worker＋`WaitForMultipleObjects` 稠密壓縮；寫入完成等待限背景執行緒 `Sleep(500)`＋`File_WaitForWriteComplete`；副檔名白名單 png/jpg/jpeg/bmp）。新檔提示彈窗（`IDD_NEW_FILE_PROMPT`：WIC 唯讀縮圖＋檔名輸入＋僅更名／更名並開啟／加入比較／立即比較／取消；單一檢查點原則，不做前置 `ConfirmDiscard`；比較分支 `Unref`；無主圖時比較按鈕禁用）。更名（`src/rename.h/.c` 約 300 行：`ExtractPrefix`＋`is_reserved_base`＋檔名校驗 228＋`MoveFileExA` 覆寫前 `.bak`／`_conflict_N` 備份＋關聯 log 連動搬移失敗只警告）。設定持久化（`src/settings.h/.c` 約 110 行：exe 同目錄 `roi_analyzer.ini`，Monitor Path0-2/Active0-2＋Rename LastRenamePrefix）。`File > Rename File... (F2)`＋`Folder Monitor Settings...`；IDM 107/108、IDD 210/220/230；F2 進加速鍵表＋灰化陣列。self-trigger 迴圈抑制三道防線（事前登記＋worker 發送前二次檢查＋UI `Monitor_IsSelfRename` 終端攔截；ring 16＋SRWLock＋5 秒窗＋`GetFullPathNameA` 標準化）。詳見 `monitor_rename_architecture.md`。

- **v2.8（2026-09-28）**：影像旋轉

- **v2.7 增補（2026-09-27）**：主視窗多檔拖放（`CmpDrop_Collect` 共用收集：W 版路徑→略過目錄→副檔名過濾→自然排序→去重→嚴格 ACP；不按 `Ctrl` 且 ≥2 張時主視窗載入第一張＋V1 開全部，按 `Ctrl` 時含目前影像且主視窗不變）。比較視窗 Snapshot（`compare_snap.c` 約 550 行：`CmpSnap_Begin/Finalize/End`＋WIC PNG 編碼＋`CF_BITMAP` 剪貼簿；按鈕與 `Ctrl+S` 存檔＋複製、`Ctrl+Shift+S` 另存、`Ctrl+C` 只複製；存檔位置為影像所在資料夾，檔名 `snap_<A>_vs_<B>_時間戳`，失敗時開另存對話框）。Snapshot 輸出尺寸改以**螢幕實際佔用像素**（`Snap_PhysicalScale` 動態切換執行緒 DPI 感知量測，維持 manifest DPI-unaware；`(u,v)` 不變、以 `z×scale` 渲染，可見範圍與螢幕相同但無系統點陣放大模糊）。V1／V2 工具列加 `Info bar` 核取方塊。C11：比較模組與 `export` 的路徑拆解改用 shlwapi（DBCS 安全）。詳見 `compareformV1V2_architecture_.md` §14。
- **v2.7（2026-09-27）**：新增比較視窗 V1（2～4 張並排，Lock 同步）與 V2（兩張疊加分割線、`Swap`、各自倍率、像素讀值、雙擊重設）。影像以參考計數 `cmp_image_t` 共享，目前影像以 `Image_Clone` 記憶體複製交付。詳見 `compareformV1V2_architecture_.md`（定稿 C1～C10）。直方圖命令 ID 移至 161～166；`View` 選單新增 `Compare Files… (Ctrl+K)`／`Compare Current with Next (K)`。
- **v2.6（2026-09-27）**：狀態列改為游標／訊息／ROI 模式／影像索引／耗時五欄；支援雲端佔位檔提示、WIC 優先解碼與 GDI+ fallback、延遲建立顯示金字塔、先顯示影像再分析，以及以整數逐列累加 RGB／Y 統計。放大使用可見原圖區域與最近鄰顯示；分析、直方圖及游標取色仍使用原圖。
- **v2.5（2026-09-27）**：加入同資料夾上一張／下一張、自然排序與目錄 mtime 快取；同解析度沿用 ROI、不同解析度重建既有格線；長按方向鍵延後 ROI／直方圖重算。GRID3 與 GRID5 同時存在時，ROI 分析約需兩次全圖掃描；耗時估計以 Release 實測為準。
- **v2.4（2026-09-27）**：右側 Histogram 面板（通道／Log／hover／範圍選取）；DRAG 拖曳中 80ms 節流即時預覽；面板閃爍修正（hover 比對、無背景擦除、分層快取）；啟動改用 comctl32 v6 manifest 並精簡 ICC 旗標。

- **v2.3（2026-09-26）**：格線按模式互斥顯示（GRID3 僅 3×3、GRID5 僅 5×5、DRAG 不顯示）；手動框在所有模式顯示於格線上方（先畫格線後畫手動框）。

- **v2.2（2026-09-26）**：加入右鍵拖曳與方向鍵平移、0 鍵重設 fit，以及影像平移邊界規則。
- **v2.1（2026-09-26）**：加入縮放操作；手動框與 3×3／5×5 分區共存；表格依來源分頁並分來源匯出。
- **v2.0（2026-09-26）**：重新定義為多 ROI 工作流程；分析結果顯示於表格，並由使用者明確匯出。
- **v1.1（2026-09-24）**：加入置中半屏視窗、視窗縮放跟隨與記錄檔選單。
- **v1.0（2026-09-24）**：建立純 C 與 Win32 的 ROI 分析工具。

## 1. 目標

以純 C 與 Win32 建立最小化 ROI 分析工具：

- 開啟或拖放 PNG、JPG、BMP 影像（主視窗拖入 2 張以上時開啟 V1 比較視窗，詳見 §9 比較視窗段）；影像依畫布大小等比例縮放並置中，視窗調整大小時同步更新。
- 手動拖曳矩形建立 ROI，支援單選新增（取代現有清單）與複選新增（累加）。
- 支援以右鍵拖曳或 `Ctrl`+方向鍵平移影像；平移時影像不可完全移出畫布。
- 將整張影像等分為 3×3 或 5×5 區塊；每個區塊都是一個 ROI，並在畫布顯示格線。
- 支援影像放大與縮小；手動 ROI 與分區 ROI 可同時存在。
- 在 ROI 上依清單順序標示 1 至 n；每個 ROI 的分析數值以一列顯示於主視窗的 Grid 表格。
- 使用 Export 按鈕、選單或快速鍵，將目前清單中的全部 ROI 追加至記錄檔。
- 右側 Histogram 面板：無選取時顯示整張影像，有選取時顯示該 ROI；支援 RGB／Y／R／G／B 通道、線性／對數縱軸、hover 顯示 Level 統計、圖表拖曳選取 Level 範圍。RGB 疊合時統計列顯示 R／G／B／Y 四列（各 Mean／StdDev／Median，Y 與 Grid 表格同源 BT.601）。

非目標：視訊、RTSP、LDC、Macbeth 比對，以及 ROI 拖曳移動或縮放編輯。

## 2. 目錄結構

```text
roi-analyzer/
├── CMakeLists.txt
├── src/
│   ├── main.c         # WinMain、主視窗、選單、版面配置、訊息分派
│   ├── app.h          # 全域狀態 app_t
│   ├── canvas.h/.c    # 畫布子視窗：影像、ROI 疊圖與滑鼠事件
│   ├── filelist.h/.c  # 同資料夾影像清單、自然排序、雲端屬性與索引
│   ├── image.h/.c     # WIC 優先、GDI+ fallback，解碼為 32 位元 BGRA
│   │                  # 另提供 Image_Clone（malloc＋memcpy，供比較視窗交付目前影像）
│   ├── image_wic.c    # WIC 記憶體解碼與 BGRA CopyPixels
│   ├── metrics.h/.c     # 一期空間域指標引擎（v3.0）
│   ├── fft.h/.c         # 二期 Radix-2 2D FFT＋PSD（v3.0）
│   ├── report.h/.c      # UTF-8 HTML 報告＋Base64（v3.0）
│   ├── metrics_async.h/.c # 4MP 背景執行緒封裝（v3.0）
│   ├── image_wic.h      # 記憶體 PNG 編碼宣告（v3.0 新增，原僅 .c）
│   ├── settings.h/.c    # INI 持久化：監控路徑＋前綴記憶（v2.9）
│   ├── monitor.h/.c     # 資料夾監控 worker＋self-trigger 抑制 ring（v2.9）
│   ├── rename.h/.c      # 更名核心：校驗＋MoveFileExA＋log 連動（v2.9）
│   ├── image_save.h/.c  # WIC PNG 原子覆寫存檔（v2.8 旋轉後存檔）
│   ├── rotate.h/.c      # 影像旋轉＋ROI 矩形座標變換（v2.8）
│   ├── compare.h / compare_image.c / compare_core.c
│   │                  # cmp_image_t 參考計數＋金字塔轉接；縮放／視圖數學／Cmp_Blit／
│   │                  # 視窗登錄表／Compare_PreTranslate
│   ├── compare_v1.c   # V1 並排視窗（2～4 張：1×2／3×1／2×2，Lock 同步）
│   ├── compare_v2.c   # V2 分割線視窗（疊加、Swap、各自倍率、像素讀值）
│   ├── compare_snap.c # Snapshot：DIB 離屏重繪、WIC PNG 編碼、CF_BITMAP 剪貼簿、資訊列
│   ├── view.h/.c      # 縮放、金字塔繪製與視窗／影像座標換算
│   ├── roi.h/.c       # ROI 清單、拖曳狀態機與 3×3／5×5 分區
│   ├── analyze.h/.c   # mean／std 與 Lab 計算
│   ├── histogram.h/.c # 直方圖統計（純計算，不依賴 GUI）
│   ├── histpanel.h/.c # Histogram 面板子視窗：通道選單、繪圖、滑鼠互動
│   ├── table.h/.c     # Grid 表格（ListView report）封裝
│   ├── export.h/.c    # 記錄檔輸出
│   ├── app.manifest   # comctl32 v6 manifest（現代控制項樣式）
│   └── app.rc         # 資源檔：嵌入 manifest、旋轉角度對話框（v2.8）
├── bin/               # 輸出執行檔
├── build/             # CMake 建置目錄（不納入版控）
└── docs/
    ├── 01_architecture_v1.md  # 本文件
    ├── 01_histogram_arch.md   # Histogram 模組設計書（v1.3：RGB 統計列 R/G/B/Y 四列）
    ├── compareformV1V2_architecture_.md  # CompareForm V1／V2 設計書（v2.7 定稿＋§14 增補）
    ├── rotate_v2_8_architecture.md  # v2.8 定稿：旋轉＋未存檔防護
    ├── compare_metrics_architecture.md  # v3.0：比較指標＋FFT＋HTML 報告
    ├── monitor_rename_architecture.md  # v2.9：監控＋更名（含 self-trigger 三道防線）
    └── 02_verification.md     # 驗證文件
```

預估約 11,500 行 C 程式碼（主程式約 6,100＋比較模組約 3,500＋旋轉／存檔約 340＋監控／更名／設定約 1,000＋指標／FFT／報告約 1,400），無第三方依賴；使用 Win32、WIC、GDI+ 與系統內建 Common Controls。

## 3. 核心資料結構

```c
// image.h — WIC 優先、GDI+ fallback，轉為 32 位元 BGRA 的影像緩衝區
typedef struct {
    unsigned char *px;      // BGRA，每像素 4 位元組
    int w, h, pitch;        // pitch = w * 4
    char path[MAX_PATH];    // 來源影像路徑（ANSI 建置；供匯出使用）
    BOOL valid;
} image_t;
```

```c
// view.h — 相對於 canvas 子視窗的 letterbox 與縮放參數
typedef struct {
    int off_x, off_y;       // 影像在畫布中的實際偏移（置中位置加平移）
    int draw_w, draw_h;     // 影像繪製尺寸
    int pan_x, pan_y;       // 使用者平移偏移；預設為 0
    float scale;            // 套用 zoom 後的 draw_w / image_w
    float zoom;             // 相對 fit 倍率；1.0 = fit，範圍 0.1 至 8.0
} view_t;
```

`View_Update()` 先依畫布大小計算置中偏移，再加上 `pan_x/pan_y`。若繪製尺寸不大於畫布，該軸固定置中並忽略平移；若大於畫布，偏移限制在 `[畫布尺寸 - 繪製尺寸, 0]`，確保影像仍覆蓋該軸畫布、不會完全拖出視野或露出黑邊。`View_Pan()` 累加平移並重算視圖；`View_Reset()` 將 zoom 設為 1.0、平移歸零並回到置中 fit。

```c
// analyze.h — 單一 ROI 的分析結果
typedef struct {
    int x0, y0, x1, y1;     // 影像座標，包含邊界（inclusive）
    int count;              // 像素數
    double r_mean, r_std;
    double g_mean, g_std;
    double b_mean, b_std;
    double y_mean, y_std;
    double lab_l, lab_a, lab_b; // 對 mean RGB 轉換所得的 Lab
} roi_result_t;
```

```c
// roi.h — 含來源標籤的 ROI 清單與拖曳狀態
typedef enum { MODE_DRAG, MODE_GRID3, MODE_GRID5 } roi_mode_t;

typedef struct {
    RECT rc;                // 影像座標，inclusive：left=x0、top=y0、right=x1、bottom=y1
    roi_mode_t source;      // ROI 來源：MODE_DRAG、MODE_GRID3 或 MODE_GRID5
    roi_result_t res;
} roi_item_t;

typedef struct {
    roi_item_t *items;      // 共用動態陣列；以 source 區分三種 ROI 來源
    int count, cap;
    int selected;           // 選取的全域 ROI 索引；-1 代表未選取
} roi_list_t;

typedef struct {
    BOOL dragging;
    BOOL additive;          // 本次拖曳累加 ROI；由複選模式或 Ctrl 決定
    POINT anchor_img;       // 拖曳起點，影像座標
    POINT cur_img;          // 拖曳目前位置，影像座標
    POINT down_win;         // 滑鼠按下位置，畫布座標，用於區分點擊與拖曳
} drag_state_t;
```

```c
// app.h — 全域狀態；僅 main.c 定義一次，其餘模組透過 extern 存取
typedef struct {
    HWND hwnd_main, hwnd_canvas, hwnd_table, hwnd_tabs, hwnd_status, hwnd_hist;
    HWND hwnd_btn_export, hwnd_btn_clear, hwnd_chk_multi;
    image_t img;
    view_t view;
    view_pyr_t pyramid;      // 顯示金字塔（lazy 建，pyramid_gen 綁 img_gen）
    filelist_t files;        // 同資料夾清單；file_idx 為目前索引（-1=不在清單）
    int file_idx;
    roi_list_t rois;
    drag_state_t drag;
    roi_mode_t mode;
    roi_mode_t table_page;
    BOOL multi;
    BOOL show_hist;
    BOOL analysis_stale;     // 長按瀏覽：影像已換、分析待補
    unsigned int img_gen;    // 每次 Image_Load 成功就 +1（直方圖快取鍵）
} app_t;                     // 示意；實際欄位見 app.h

extern app_t g_app;
```

```c
// filelist.h — 同資料夾影像清單（W 掃描、自然排序、mtime 快取）
// 公開 API 保持 char；內部以 FindFirstFileW 掃描，寬字元預轉後 StrCmpLogicalW 排序。
// 每筆記錄 cloud 旗標（RECALL_ON_DATA_ACCESS / RECALL_ON_OPEN / OFFLINE），
// 導覽到雲端佔位檔前先顯示「雲端檔案下載中…」。
// FileList_Refresh：同目錄且 GetFileAttributesExA mtime 未變則沿用（約 0.01ms），
// 否則重掃；Find 找不到（!exact）時強制重掃一次再定位。
// FileList_Free 後清空 scanned，避免 mtime 快取誤判命中。
```

同步入口（定義於 `main.c`）：

- `App_RoiChanged()` — 重建表格、重繪畫布、更新直方圖與狀態列
- `App_SelectROI(idx)` — 設選取＋切換對應頁籤＋同步三者
- `App_UpdateHistogram()` — 依 `selected` 推送整張或 ROI 來源給面板
- `App_PreviewHistogram(img_rc)` — 拖曳中 80ms 節流預覽，不動清單／選取／表格

所有 ROI（手動框、分區格線、編號及橡皮筋起終點）一律以影像座標儲存；繪製時才依目前 `view_t` 換算成畫布座標，因此縮放時會同步變化。縮放不改變 ROI 座標或分析結果；表格數值永遠是原始影像座標上的像素統計。

## 4. 模組職責

| 模組 | 主要函式 | 說明 |
|------|----------|------|
| main | `WinMain`、`MainWndProc`、`Layout()` | 建立主視窗、選單與子控制項；處理 `WM_SIZE`、`WM_COMMAND`、`WM_NOTIFY` 與 `WM_DROPFILES`；提供 `App_*` 同步入口 |
| histogram | `Hist_Compute()`、`Hist_RangeStats()` | 純計算：256-bin 統計、mean／std／median、範圍統計；不依賴 GUI |
| histpanel | `HistPanel_Register/Create/SetSource/ClearSource/SetChannel/SetLogScale` | 右側面板子視窗；三層快取（base／ramp／back）雙緩衝繪製；hover 比對＋子區域重繪；詳見 `01_histogram_arch.md` |
| canvas | `Canvas_Register()`、`CanvasWndProc` | 畫布子視窗；雙緩衝繪製影像、ROI、編號、格線與橡皮筋；首次 paint 後延遲建立金字塔，處理左鍵 ROI 與右鍵平移 |
| filelist | `FileList_Scan/Refresh/Find/Path/Free()` | 同資料夾影像自然排序清單；記錄 ANSI 路徑可用性及雲端佔位檔屬性 |
| image | `Image_Load(img, path)`、`Image_Free(img)`、`Image_Clone(dst, src)` | WIC 將整檔讀入記憶體後解碼並 `CopyPixels` 為 BGRA；WIC 失敗則 fallback 至 GDI+ `LockBits`。`Image_Clone` 以 `malloc`＋`memcpy` 複製像素，與 `Image_Free` 同配置器 |
| compare | `Compare_Init`、`Compare_CanOpen(need)`、`Compare_CloseAll`、`Compare_PreTranslate(msg)`、`CompareV1_Open`、`CompareV2_Open` | 比較視窗模組（無 owner 的獨立頂層視窗）：`cmp_image_t` 參考計數共享影像、`cmp_view_t` 視口中心影像座標模型、`Cmp_Blit` 金字塔繪製、`CmpReg` 視窗登錄表、`CmpDrop_Collect` 共用拖放收集、`compare_snap` Snapshot；詳見 `compareformV1V2_architecture_.md` |
| view | `ViewPyr_Build/Free/Pick()`、`View_DrawImagePyramid()` 與座標換算函式 | 延遲建立最多六層 2×2 平均金字塔；縮小選用仍不低於顯示尺寸的最小層，放大以原圖可見子矩形及 COLORONCOLOR 繪製 |
| roi | `ROI_Clear()`、`ROI_ClearSource()`、`ROI_Add()`、`ROI_Remove()`、`ROI_BuildGrid(n)`、`ROI_HitTest()`、`ROI_OnLDown/Move/LUp()` | 管理帶來源標籤的共用 ROI 清單、建立分區、命中測試與拖曳狀態機（見 §5） |
| analyze | `AnalyzeROI(img, rc, out)` | 逐列以 32 位元整數累加 RGB、平方與交叉項，再併入 64 位元總和；寬度超過 66051 時使用 64 位元逐像素 fallback；Y 由 BT.601 加權項推導，Lab 使用 D65 |
| table | `Table_Create()`、`Table_Rebuild()`、`Table_AppendRow()`、`Table_Select()`、`Table_Clear()` | 封裝頁籤控制項與 ListView report 模式；依目前頁籤篩選 ROI，欄位定義見 §7 |
| export | `Export_Log(img, rois)`、`Export_GetPath()` | 將各來源 ROI 分別依 §8 格式追加至來源影像旁、以來源命名的記錄檔 |

## 5. 互動狀態機

### 5.1 MODE_DRAG（手動框選）

```text
IDLE --LDown（影像內）--> DRAGGING
  設定滑鼠捕捉，記錄 anchor_img、down_win 與本次 additive 狀態
DRAGGING --Move--> 更新 cur_img，讓畫布重繪白色橡皮筋；同時以 80ms 節流呼叫 `App_PreviewHistogram()`，直方圖即時跟隨橡皮筋矩形（標籤 `Drag (preview)`，不動清單／選取／表格）
DRAGGING --Esc--> 取消橡皮筋、釋放滑鼠捕捉，呼叫 `App_UpdateHistogram()` 回到選取／整張，回到 IDLE
DRAGGING --LUp-->
  ├─ 位移 < 3 個畫布像素：視為點擊
  │    命中 ROI → 選取該 ROI，並同步表格；未命中 → 清除選取
  └─ 位移 ≥ 3 個畫布像素：建立新 ROI
       ├─ 單選（multi=FALSE 且未按 Ctrl）：僅取代手動 ROI 清單
       └─ 複選（multi=TRUE 或按住 Ctrl）：將新 ROI 加入手動 ROI
       → AnalyzeROI → 更新表格 → 選取新 ROI → 回到 IDLE
```

- 左鍵拖曳只用於 ROI；右鍵按住拖曳只平移畫面。兩種操作互不觸發，右鍵平移不進入 ROI 狀態機。
- 框選範圍換算成影像座標後限制於影像邊界，並正規化為 `x0 <= x1`、`y0 <= y1`；至少涵蓋一個像素。
- 在影像外按下滑鼠不開始拖曳；拖曳移出影像時，端點限制於影像邊界。
- 點擊既有 ROI 用於選取，不建立新框；手動 ROI 依其來源內順序獨立編號 1 至 n。
- 刪除 ROI 後，後續 ROI 編號自動遞補，表格重新建置。

### 5.2 MODE_GRID3／MODE_GRID5（自動分區）

```text
切換至 GRID 模式，若該來源尚無分區則建立 N×N 分區（N 為 3 或 5）
  → 保留其他來源 ROI；對 N×N 區塊逐區執行 AnalyzeROI
  → 重建目前頁籤表格並繪製全部來源 ROI、格線及編號

GRID 模式下 LDown
  → 命中分區時選取該 ROI，並切換至對應表格頁籤；不建立新 ROI
```

以整數邊界確保寬、高不能整除時，每個像素仍恰好分配至一區：

\[
x_i = \left\lfloor \frac{i \cdot W}{N} \right\rfloor,\quad
y_j = \left\lfloor \frac{j \cdot H}{N} \right\rfloor,\quad i,j=0..N
\]

第 \( (r,c) \) 區的 inclusive 範圍為
\([x_c,\ x_{c+1}-1] \times [y_r,\ y_{r+1}-1]\)，編號採列優先
\(r \cdot N + c + 1\)（左上為 1，右下為 \(N^2\)）。3×3 與 5×5 分區各自保留並獨立編號；影像寬或高小於 N 時不建立該來源分區，並以訊息方塊提示。

### 5.3 模式切換規則

| 切換情境 | 行為 |
|------|------|
| DRAG → GRID3／GRID5 | 保留手動 ROI；切換至對應來源分頁，若該分區尚未建立則建立分區 |
| GRID3 ↔ GRID5 | 保留兩種分區及手動 ROI；切換至對應來源分頁，僅建立尚未存在的分區 |
| GRID3／GRID5 → DRAG | 保留所有分區 ROI 與格線；切換至 Drag 分頁 |
| 載入新影像 | 清除全部來源 ROI；若為 GRID 模式，立即對新影像建立對應分區 |
| GRID 分區尺寸不足 | 顯示提示並維持該 GRID 模式；該來源無 ROI，其他來源清單不受影響 |

手動框選的單選／複選只影響手動 ROI 的新增方式，不代表表格列的選取模式。切換 DRAG、GRID3、GRID5 只改變操作模式，不會清除其他來源 ROI；表格與畫布一次只選取一個 ROI。

### 5.4 表格與畫布的選取同步

- 使用者選取目前頁籤表格列（`LVN_ITEMCHANGED`）時，將該列映射至共用清單的全域索引，設定 `rois.selected` 並重繪畫布，讓對應 ROI 高亮。
- 使用者在畫布點選 ROI 時，設定相同全域索引、切換至該來源頁籤並選取對應表格列，再呼叫 `ListView_EnsureVisible()` 捲動至該列。
- 選取僅改變顯示狀態，不改變清單內容或匯出資料。

### 5.5 全域按鍵

| 按鍵 | 功能 |
|------|------|
| `1`／`2`／`3` | 切換 DRAG／GRID3／GRID5 |
| `M` | 切換手動框選的單選／複選新增模式 |
| `Ctrl` + 拖曳 | 單選模式下暫時累加 ROI |
| `Ctrl` + 滾輪 | 以滑鼠位置為錨點放大／縮小 |
| `+`／`-` | 以畫布中心為錨點放大／縮小 |
| 右鍵按住拖曳 | 平移影像 |
| `←`／`→` | 上一張／下一張影像；長按瀏覽時延後 ROI 與直方圖分析 |
| `Ctrl` + 方向鍵 | 平移影像 20 畫布像素；`Ctrl+Shift` + 方向鍵平移 100 像素 |
| `0` | 重設為置中 fit（zoom=1.0、pan=0） |
| `Delete` | 刪除選取的手動 ROI（僅限 DRAG 模式） |
| `Esc` | 拖曳中：取消橡皮筋並恢復直方圖；非拖曳：清除選取（直方圖回整張） |
| `H` | 顯示／隱藏 Histogram 面板 |
| `A`／`Y`／`R`／`G`／`B` | 切換直方圖通道（RGB／Y／R／G／B） |
| `L` | 直方圖線性／對數縱軸 |
| `C` | 清除目前表格頁籤對應來源的 ROI |
| `Shift+C` | 清除全部來源 ROI 與表格內容 |
| `Ctrl+E` | 匯出目前全部 ROI |
| `O` | 開啟影像檔案對話框 |

註：畫布操作快捷鍵由主訊息迴圈依焦點與控制項類別過濾；方向鍵瀏覽在表格、下拉選單、頁籤及編輯欄位中不攔截。`Ctrl+E` 為選單加速鍵。

## 6. 分析公式

- 亮度使用 BT.601：\(Y = 0.299R + 0.587G + 0.114B\)。
- RGB、平方及 RGB 交叉乘積以 `uint32_t` 逐列累加，再合併到 `uint64_t`；每列寬度不超過 66051，超過時改用逐像素 `double` 累加。
- 標準差為母體標準差：\(\sqrt{E[x^2] - \mu^2}\)；若浮點誤差使根號內的值小於零，先限制為零。Y 的平均值與平方平均值由 RGB 平均、平方及交叉乘積依 BT.601 權重推導。
- Lab 由 ROI 的平均 RGB 轉換：sRGB 線性化 → D65 XYZ → Lab；轉換公式沿用 `c-vlcplayer` 的 `analysis.c` 中 `rgb_to_lab`。
- RGB、Y 的統計等價於 ROI 內個別像素計算；Lab 是平均 RGB 轉換結果，不是逐像素 Lab 的平均。
- GRID3 與 GRID5 同時存在時，分析只走訪各 ROI 矩形，合計約等於兩次全圖掃描。沿用 4000×3000 影像低於約 100 毫秒的原估計，實際耗時以 Release 實測為準。

## 7. Grid 表格（ListView）

- 使用一個 Tab Control 與一個 ListView，頁籤固定為 `Drag`、`3x3`、`5x5`；頁籤切換只更新同一個 ListView 的列，不改變目前操作模式。
- 每個頁籤只顯示其來源的 ROI；畫布同時顯示全部來源 ROI。點選任一頁籤列會選取畫布上的對應 ROI。
- `Clear` 按鈕與 `C` 只清除目前頁籤的來源；`Edit > Clear All ROI` 與 `Shift+C` 清除全部來源。
- 樣式：`LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL`。
- 擴充樣式：`LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER`。
- 一個 ROI 顯示為一列，數值橫向排列；浮點數統一格式為 `%.2f`。
- 欄位順序固定如下：

| # | Rect | Count | R mean | R std | G mean | G std | B mean | B std | Y mean | Y std | L | a | b |
|---|------|-------|--------|-------|--------|-------|--------|-------|--------|-------|---|---|---|
| 1 | (10,10)-(59,59) | 2500 | 255.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 76.24 | 0.00 | 53.24 | 80.11 | 67.22 |

- `Table_Rebuild()` 期間以 `WM_SETREDRAW(FALSE/TRUE)` 暫停及恢復重繪，避免大量更新閃爍。
- 表格是呈現層；唯一資料來源為帶來源標籤的 `g_app.rois`。頁籤依來源篩選，匯出從 ROI 清單讀取，不從 ListView 取值。
- ListView 使用單列選取；「複選」指累加建立多個 ROI，不是同時選取多列表格列。

## 8. Export 記錄檔格式

- 觸發方式：Export 按鈕、`File > Export Log` 或 `Ctrl+E`。
- 依 ROI 來源分別輸出至來源影像所在目錄，檔名為 `<影像主檔名>_<模式>.log`，採 UTF-8 編碼及 append 追加方式。模式字串固定為手動框選的 `drag`、3×3 分區的 `grid3x3`、5×5 分區的 `grid5x5`。
  - 例：`C:\data\test_red.png` 的三種來源分別輸出至 `C:\data\test_red_drag.log`、`C:\data\test_red_grid3x3.log`、`C:\data\test_red_grid5x5.log`。
- Export 將目前存在的各來源 ROI 分別寫入其來源記錄檔；某來源 ROI 數量為零時不建立或修改該來源檔案。全部來源皆無 ROI 時顯示「尚無 ROI」提示。
- 匯出成功後，狀態列顯示各輸出路徑；開檔或寫入失敗時顯示錯誤訊息，不回報為成功。
- 每個來源記錄檔每次匯出各寫入一個區塊，以 `# ==== export` 開始；區塊內依該來源編號順序逐一輸出 ROI：

```text
# ==== export [2026-09-26 10:00:01] image=C:\data\test_red.png size=640x480 mode=grid3x3 rois=9
# roi 1 rect=(0,0)-(212,159) count=34080
RGB mean=(255.00,0.00,0.00) std=(0.00,0.00,0.00)
Y mean=76.24 std=0.00
Lab L=53.24 a=80.11 b=67.22
# roi 2 rect=(213,0)-(425,159) count=34080
RGB mean=(255.00,0.00,0.00) std=(0.00,0.00,0.00)
Y mean=76.24 std=0.00
Lab L=53.24 a=80.11 b=67.22
...
```

`Clear` 只清除目前表格頁籤來源的 ROI，不刪除記錄檔；`Shift+C` 或 `Edit > Clear All ROI` 清除全部來源 ROI。`Log > Open Log File` 開啟目前影像與操作模式來源對應的記錄檔；沒有目前影像或檔案不存在時顯示提示。`Log > Open Folder` 開啟目前影像所在資料夾。

## 9. UI 版面

預設主視窗為主螢幕置中半屏：`x=screenW/4`、`y=screenH/4`、`w=screenW/2`、`h=screenH/2`，螢幕尺寸由 `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)` 取得。

```text
┌──────────────────────────────────────────────────────┐
│ File   Mode   Edit   Log                             │ 選單
├──────────────────────────────────────────────────────┤
│                                                      │
│                    Canvas 子視窗                    │
│   影像 fit 置中 + ROI 框 + 編號 + 格線 + 縮放提示    │
│ Ctrl+滾輪／+/- 縮放；右鍵拖曳或 Ctrl+方向鍵平移；0 回 fit │
│                                                      │
├──────────────────────────────────────────────────────┤
│ [Export]  [Clear]  ☐ 複選                            │ 按鈕列
├──────────────────────────────────────────────────────┤
│ [Drag]          [3x3]          [5x5]                 │ 頁籤
├──────────────────────────────────────────────────────┤
│ # │ Rect │ Count │ R mean │ R std │ ... │ L │ a │ b  │ Grid 表格
│ 1 │ ...                                              │
├──────────────────────────────────────────────────────┤
│ 游標/RGB │ 訊息（自動填滿） │ ROI/模式/zoom │ i/n │ load|ana|hist|paint|show|done │
└──────────────────────────────────────────────────────┘
```

選單內容：

- **File**：`Open...`、`Export Log (Ctrl+E)`、`Exit`。
- **Mode**：`Drag [1]`、`3x3 Grid [2]`、`5x5 Grid [3]`、分隔線、`Multi Select [M]`。複選項目的勾選狀態與按鈕列核取方塊同步。
- **Edit**：`Delete Selected ROI [Del]`、`Clear Current Tab ROI [C]`、`Clear All ROI [Shift+C]`。
- **Log**：`Open Log File`、`Open Folder`。
- **View**：`Histogram Panel [H]`（打勾項）、分隔線、`Channel: RGB [A]／Luminosity [Y]／Red [R]／Green [G]／Blue [B]`（單選打勾）、`Log Scale [L]`（打勾項）、分隔線、`Compare Files… [Ctrl+K]`、`Compare Current with Next [K]`。

直方圖命令 ID 為 161～166（`IDM_HIST_RGB/_Y/_R/_G/_B/_LOG`），比較視窗命令 ID 為 155／156（`IDM_COMPARE_FILES`／`IDM_COMPARE_NEXT`）。

比較視窗（`Ctrl+K` 開 V1；`K` 開 V2，最後一張時對上一張）為無 owner 的獨立頂層視窗，有自己的工作列按鈕；開啟時自動納入目前影像（以 `Image_Clone` 記憶體複製，不重新解碼）。主視窗拖入 2 張以上時也開 V1：不按 `Ctrl` 時主視窗載入自然排序第一張＋V1 開全部（附註「Ctrl+drop to include current image」），按住 `Ctrl` 放開滑鼠時含目前影像且主視窗不變；拖放判斷以 `CmpDrop_Collect`（W 版路徑→略過目錄→副檔名過濾→自然排序→去重→嚴格 ACP）計數，解碼失敗於載入階段另外計數。V1 支援 2～4 張（1×2／3×1／2×2）、`Lock` 同步倍率與平移、拖放加圖、`V2 ▶`；V2 為兩張疊加，可拖曳分割線、`Swap`、各自倍率滑桿、`Sync pan`／平移目標、雙擊重設視圖，狀態列顯示游標下兩張圖各自的影像座標與 RGB。比較視窗影像上限 8 張（開啟前預先檢查），主視窗關閉時先 `Compare_CloseAll()`。

Snapshot（V1 工具列／V2 Actions 的 `Snapshot` 按鈕、`Ctrl+S` 存檔＋複製、`Ctrl+Shift+S` 另存＋複製、`Ctrl+C` 只複製）：同一個繪製函式離屏重繪到 DIB（不受遮擋與系統 DPI 放大影響），加上底部資訊列後以 WIC 編碼 PNG 存檔並以 `CF_BITMAP` 複製到剪貼簿（剪貼簿先做，對話框取消仍保留）。存檔位置為影像所在資料夾（V1 取第 0 格、V2 取畫面左側），檔名 `snap_<A>_vs_<B>_yyyymmdd-hhmmss[_k].png`（A/B 主檔名 DBCS 安全截 32 位元組，不覆寫既有檔），寫入失敗時開另存新檔對話框。資訊列只出現在輸出圖中（畫面不顯示），V1／V2 工具列有 `Info bar` 核取方塊可關閉；內容為檔名、倍率、可見影像區域（影像座標，可對應主視窗 ROI）與時間戳（與檔名同一 `SYSTEMTIME`）。單鍵 `S`／`M`／`P`／`L`／`V`／`0`／`+`／`-` 只在無 `Ctrl` 時生效，避免 `Ctrl+S` 同時觸發 `Swap`。C11：比較模組與 `export` 的路徑拆解一律用 shlwapi（`strrchr` 在 Big5／Shift-JIS 下會在 0x5C 第二位元組處誤切）。

`Layout()` 於 `WM_SIZE` 呼叫，依序配置子視窗：

1. 傳送 `WM_SIZE` 給狀態列，使其貼齊底部；`App_StatusLayout()` 配置游標 220、訊息自動填滿、模式 200、索引 90、耗時約 260 像素五欄，窄視窗優先壓縮訊息欄。
2. 表格目標高度為 `max(140, client_h × 0.3)`；若無法保留至少 100 像素畫布高度，則壓縮表格至可用高度。
3. 按鈕列固定 28 像素，置於表格上方。
4. 畫布佔用上方剩餘區域；移動或縮放畫布後，由畫布的 `WM_SIZE` 呼叫 `View_Update()` 與 `InvalidateRect()`。

畫布支援 `Ctrl+滑鼠滾輪` 與 `+`／`-` 縮放，範圍為 fit 基準的 10% 至 800%，初始為 fit。縮放時以滑鼠在畫布上的位置為錨點；鍵盤縮放或沒有有效滑鼠位置時以畫布中心為錨點。更新 `view_t.zoom` 後重算偏移與繪製尺寸，錨點所對應的影像座標保持不動。

平移操作為右鍵按住拖曳（影像跟隨滑鼠位移）及 `Ctrl`+方向鍵；每次平移 20 畫布像素，另按 `Shift` 時為 100 像素。單獨 `←`／`→` 用於切換同資料夾影像。`0` 將 zoom 重設為 1.0、pan 歸零並將影像置中。`view_t.pan_x/pan_y` 保存使用者平移偏移；每次更新先算置中位置再套用 pan。當影像該軸尺寸不大於畫布時忽略該軸 pan 並保持置中；尺寸較大時將偏移 clamp 至 `[畫布尺寸 - 影像繪製尺寸, 0]`，因此影像至少覆蓋該軸畫布，不會完全拖出視野或露出黑邊。左鍵仍僅建立或選取 ROI，與右鍵平移互不干擾。

畫布的 `WM_PAINT` 使用原點為畫布 `(0,0)` 的記憶體 DC 雙緩衝繪圖：

| 元素 | 樣式 |
|------|------|
| 背景 | 深灰 `RGB(32,32,32)` |
| 影像縮小 | 首次 paint 後建立最多六層 2×2 平均金字塔；選用仍不小於顯示尺寸的最小層，以 `HALFTONE` 繪製 |
| 影像放大 | 只提交畫布可見的原圖子矩形至 `StretchDIBits`，使用 `COLORONCOLOR` 最近鄰 |
| 一般 ROI 框 | 黃色，2 像素 |
| 選取中的 ROI 框 | 洋紅色，3 像素 |
| 拖曳橡皮筋 | 白色虛線，1 像素；起終點以影像座標保存後依目前 view 繪製 |
| 分區格線 | 青色，1 像素，包含外框 |
| 格線顯示規則 | GRID3 僅顯示 3×3 格線，GRID5 僅顯示 5×5 格線；兩者互斥。DRAG 不顯示格線；手動框在所有模式均顯示於格線上方 |
| 編號標籤 | 白字黑底；手動框標於左上內側，顯示寬度小於 18 像素時改置於框外上方；分區標於格中央 |

影像中的 inclusive 矩形轉換至畫布座標時，右、下邊界使用下一像素邊界，確保相鄰分區接合；目前縮放係數 `s` 已包含 fit 與 `zoom`：

\[
L = \text{off}_x + x_0 \cdot s,\quad R = \text{off}_x + (x_1 + 1) \cdot s
\]

\(T\)、\(B\) 方向亦同。調整大小時，畫布呼叫 `View_Update()` 重算 fit 與偏移並立即重繪，保留 `zoom` 設定；所有 ROI 座標維持在影像座標，不需額外偏移修正。

標題列顯示檔名、影像尺寸、`[i/n]`（索引非精確時顯示 `—/n`）、模式及單選／複選狀態。狀態列五欄顯示游標與原圖 RGB、操作訊息、ROI 數／模式／zoom、置中的影像索引，以及 load／analysis／histogram／paint／show／done 毫秒。長按方向鍵時只更新影像與索引，訊息欄顯示「瀏覽中…」；離開長按後再補做分析。雲端佔位檔載入前會先更新狀態列並呼叫 `UpdateWindow()` 顯示下載提示。導航先替換影像並同步畫出首幀，再分析 ROI、更新表格及直方圖；金字塔只供顯示使用，分析、直方圖及游標取色始終讀取原始 BGRA。WIC 優先解碼，失敗才使用 GDI+；WinMain 以 STA 初始化 COM。金字塔建置與分析均在 UI 執行緒執行，不使用背景執行緒。

## 10. 建置

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(roi_analyzer C RC)
set(CMAKE_C_STANDARD 11)

add_executable(roi_analyzer
    src/app.rc
    src/main.c
    src/canvas.c
    src/filelist.c
    src/image.c
    src/image_wic.c
    src/view.c
    src/roi.c
    src/analyze.c
    src/histogram.c
    src/histpanel.c
    src/table.c
    src/export.c
    src/compare_image.c
    src/compare_core.c
    src/compare_v1.c
    src/compare_v2.c
    src/compare_snap.c
)

target_compile_options(roi_analyzer PRIVATE "$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra>")
target_include_directories(roi_analyzer PRIVATE src)
target_compile_definitions(roi_analyzer PRIVATE NOMINMAX)
target_link_libraries(roi_analyzer user32 gdi32 kernel32 comctl32 comdlg32 gdiplus shell32 shlwapi windowscodecs ole32 uuid m)

set_target_properties(roi_analyzer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    LINK_FLAGS "-mwindows"
)
```

```bash
cd C:/Github/roi-analyzer
export PATH="/c/msys64/ucrt64/bin:$PATH"   # cc1.exe 執行時需可載入 libmpfr-6.dll
rm -rf build
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="C:/msys64/ucrt64/bin/mingw32-make.exe"
cmake --build build
```

注意事項：

- 啟動時呼叫 `App_InitCommonControls()`：`ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES`。不要加 `ICC_STANDARD_CLASSES`（無 v6 manifest 時會回傳 FALSE）。失敗時 fallback `InitCommonControls()` 並輸出 Debug 訊息，**不中止程式**；真正檢查點是各 `CreateWindowEx` 回傳值。
- `src/app.manifest` 嵌入 comctl32 v6（`src/app.rc`：`1 RT_MANIFEST "app.manifest"`），否則 `LVS_EX_DOUBLEBUFFER` 無效且控制項為 Win95 外觀。`project()` 需宣告 `RC` 語言；`-Wall -Wextra` 用 generator expression 限定 C 語言，避免傳給 windres。改 `project()` 後需刪 `build/` 重配。
- 本設計保持 **ANSI 建置（不加 UNICODE／_UNICODE）**；`image_t.path` 為 `char`，中文路徑依賴此行為。面板來源標籤的 `wchar_t` 僅作顯示字串（`TextOutW`），與路徑無關。
- 主視窗與 Histogram 面板皆設 `WS_CLIPCHILDREN`；面板 `WM_ERASEBKGND` 回 1、`hbrBackground = NULL`，由三層快取（base／ramp／back）雙緩衝繪製，避免滑鼠移動閃爍。
- 編譯選項使用 `-Wall -Wextra`，驗收標準為零警告。

## 11. 待確認事項

以下是依目前需求採用的設計假設，實作前仍應確認：

1. **3×3／5×5 的語意**：本版定義為整張影像分成 9／25 個 ROI。若仍需以點擊位置為中心取 3×3／5×5 像素，須另增模式。
2. **單選／複選的語意**：本版以複選核取方塊控制新增時是否累加，並支援 Ctrl 暫時累加。表格仍維持單列選取；若需求是同時選取多列，需改用 ListView 多重選取並另定義刪除與匯出的選取範圍。
3. **匯出方式與格式**：本版固定依 ROI 來源輸出至影像旁的 append 記錄檔。若需「另存新檔」對話框或 CSV 格式以供試算表使用，須新增 Export As 功能。
4. **比較視窗已知限制**：未宣告 DPI 感知，150% 等非 100% 系統縮放下比較視窗與主畫布一樣經系統點陣放大，100% 不是真正的像素對像素；Snapshot 已改以螢幕實際佔用像素輸出（`Snap_PhysicalScale` 暫時切換執行緒 DPI 感知後量測），但長期仍建議在 manifest 宣告 Per-Monitor V2 並處理 `WM_DPICHANGED`。B1 背景預載尚未實作，連開多張大圖時開啟 V1 需等待解碼完成。
