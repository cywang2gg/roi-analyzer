# ROI Analyzer — 系统架构设计书

> 版本 v1.1 | 2026-09-24 | 新增:居中半屏默认窗口、resize 跟随、Log 打开文件菜单

变更历史:
- v1.1 (2026-09-24): 默认窗口改为居中半屏(`GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)`,x=w/4,y=h/4,w/2×h/2);
  图片 + ROI(确认/预览/橡皮筋)跟随窗口 resize(橡皮筋改存影像坐标,overlay 每 paint 经当前 view 换算;
  `WM_SIZE` 补 `InvalidateRect`,mem-DC 改画 (0,0) 原点,去掉可疑 `pv.off_y -= rc.top`);
  Log 菜单新增「Open Log File」(打开当前图片的 per-image log,缺文件时 MessageBox 提示先确认 ROI);
  状态栏空闲提示注明每次确认自动追加当前图片 log。
- v1.0 (2026-09-24): 新项目(独立于 c-vlcplayer,但复用其坐标/Lab 公式)

## 1. 目标

纯 C + Win32 最小 ROI 分析工具:

- 拖入图档(PNG / JPG / BMP)显示
- ROI 框选:鼠标拖曳矩形,或固定 3x3 / 5x5 点选
- 输出每块 ROI 的 RGB mean/std、Y mean/std、Lab mean → 追加到当前图片的 `.log`

非目标:不做视频、不做 RTSP、不做 LDC、不做直方图窗口、不做 Macbeth 比对。

## 2. 目录结构

```
roi-analyzer/
├── CMakeLists.txt
├── src/
│   ├── main.c        # WinMain、窗口创建、消息循环、全局状态
│   ├── image.h/.c    # 图档加载 → 32bpp BGRA buffer
│   ├── view.h/.c     # fit-to-window 参数、窗口↔影像坐标换算
│   ├── roi.h/.c      # 鼠标状态机、ROI overlay 绘制
│   ├── analyze.h/.c  # mean/std + Lab 计算
│   └── log.h/.c      # per-image append-only log 输出
├── bin/              # 输出 exe
├── build/            # CMake build 目录(不进版控)
└── docs/
    ├── 01_architecture.md  # 本文件
    └── 02_verification.md  # 验证文件
```

总量估计 ~700 行 C,无第三方依赖(只用 Win32 + GDI+,系统内置)。

## 3. 核心数据结构

```c
// image.h — 影像缓冲(GDI+ 加载后转 32bpp BGRA)
typedef struct {
    unsigned char *px;      // BGRA, 4 bytes/px
    int w, h, pitch;        // pitch = w * 4
    char path[MAX_PATH];    // 来源文件名(写 log 用)
    BOOL valid;
} image_t;

// view.h — 显示参数(letterbox 居中)
typedef struct {
    int off_x, off_y;       // 影像在窗口的偏移
    int draw_w, draw_h;     // 影像绘制尺寸
    float scale;            // draw_w / image_w
} view_t;

// roi.h — ROI 状态
typedef enum { MODE_DRAG, MODE_FIX3, MODE_FIX5 } roi_mode_t;
typedef struct {
    roi_mode_t mode;
    BOOL dragging;          // 拖曳中(仅 MODE_DRAG)
    POINT anchor;           // 起点(窗口坐标)
    RECT rubber;            // 橡皮筋框(窗口坐标)
    RECT confirmed;         // 已确认框(影像坐标, inclusive)
    BOOL has_confirmed;     // 是否有已确认框可显示
    POINT preview;          // 固定模式预览中心(影像坐标)
    BOOL has_preview;
} roi_state_t;

// analyze.h — 分析结果(一块 ROI)
typedef struct {
    int x0, y0, x1, y1;     // 影像坐标 inclusive
    int count;              // 像素数
    double r_mean, r_std, g_mean, g_std, b_mean, b_std;
    double y_mean, y_std;
    double l_mean, a_mean, b_mean;   // Lab(由 mean RGB 转换)
} roi_result_t;
```

## 4. 模块职责

| 模块 | 函数 | 说明 |
|------|------|------|
| main | WinMain, WindowProc | 窗口、菜单、消息分派(无 Timer、无线程) |
| image | `Image_Load(path)`, `Image_Free()` | GDI+ `FromFile` → `LockBits(32bppARGB)` → memcpy 到 `px`;注意像素顺序为 BGRA |
| view | `View_Update()`, `View_ToImage()`, `View_ToWindow()` | fit 缩放:`scale = min(cw/iw, ch/ih)`,居中;坐标换算后 clamp 到 `[0, w-1]` |
| roi | `ROI_OnLDown/Move/LUp()`, `ROI_DrawOverlay()` | 见 §5 状态机 |
| analyze | `AnalyzeROI(img, rect, out)` | 单 pass:sum + sumsq → mean、总体 std;Y 用 BT.601;Lab 用 D65(与 c-vlcplayer 同一公式) |
| log | `LogROI(img_path, mode, result)`, `Log_GetPath()` | 追加源图片目录的 `<basename>_<mode>.log`(见 §7) |

## 5. 交互状态机

```
MODE_DRAG:
  IDLE --LDown--> DRAGGING --Move--> 更新 rubber --LUp--> 确认框 → Analyze → Log → IDLE(保留显示)
MODE_FIX3 / MODE_FIX5:
  IDLE --Move--> 更新 preview 中心 --LDown--> 以点击点为中心取 3x3/5x5 → Analyze → Log
全局按键: 1=DRAG, 2=FIX3, 3=FIX5, C=清除框, O=开档对话框
拖放: WM_DROPFILES → 取第一个文件 → Image_Load → 清除旧框 → 重绘
```

固定框约束:中心 clamp 到 `[r, w-1-r]`(r=1 或 2),保证框永远完整落在影像内,count 恒为 9 / 25。

拖曳框约束:换算到影像坐标后 clamp,`x0<=x1, y0<=y1`,最小 1x1;点在影像外按下 → 忽略。

## 6. 分析公式(与 c-vlcplayer 一致)

- `Y = 0.299·R + 0.587·G + 0.114·B`(BT.601)
- std:总体标准差,`sqrt(E[x²] − mean²)`,double 精度累加
- Lab:mean RGB → 线性化 → XYZ(D65) → Lab(直接移植 `analysis.c` 的 `rgb_to_lab`)

## 7. Log 格式

每个源图片和模式各有一个 append-only 日志，位于源图片所在目录。文件名为去掉最后扩展名后的
`<image basename>_3x3.log`、`<image basename>_5x5.log` 或 `<image basename>_drag.log`，例如 `C:/data/test_red.png` → `C:/data/test_red_3x3.log`。
每条记录必须以清晰的 `# roi rect` 行开始:

```
# roi rect=(10,10)-(59,59) count=2500 mode=drag [2026-09-24 10:00:01]
RGB mean=(255.00,0.00,0.00) std=(0.00,0.00,0.00)
Y mean=76.24 std=0.00
Lab L=53.24 a=80.11 b=67.22
```

mode 字段值:`drag` / `3x3` / `5x5`。

## 8. UI 版面(默认居中半屏,跟随 resize)

默认窗口:主屏居中半屏(`x=screenW/4, y=screenH/4, w=screenW/2, h=screenH/2`,
`GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)`),不再是 `CW_USEDEFAULT` + 1280×800。

```
菜单: File(Open.../Exit)  Mode(Drag[1]/3x3[2]/5x5[3])  Log(Open Log File/Open Folder/Clear)
画布: 影像居中 fit,ROI 确认框=黄色,固定模式预览框=白色,拖曳中=白色橡皮筋
标题栏: 档名 + 尺寸 + 模式
底部状态栏: 鼠标影像坐标 + 最后一笔 ROI 摘要(R/G/B/Y mean);空闲时提示每次确认自动追加当前图片 log
```

resize 跟随:`WM_SIZE → RefreshView → View_Update` 重算 fit 参数并 `InvalidateRect` 立即重绘;
`WM_PAINT` 的 mem-DC 以 (0,0)-(w,h) 为原点(含状态栏扣除),`g_view` 直接使用(无 `off_y` 修正)。
ROI 全部存影像坐标:确认框/预览中心本就如此,橡皮筋(anchor/rubber)也改存影像坐标,
overlay 每次 paint 经当前 view 换算 → 图片缩放时三者同步缩放。

Log 说明:每次 ROI 确认自动 append 当前图片、当前模式的 `<basename>_<mode>.log`;
「Open Log File」直接打开当前图片 log(无当前图片或尚无文件时 MessageBox);
「Clear」只删除当前图片 log 并清除 ROI 状态;Open Folder 行为不变。

无 Timer、无线程:图片静态,分析在 UI 线程同步执行(<100ms,无卡顿问题)。

## 9. 构建

```cmake
cmake_minimum_required(VERSION 3.10)
project(roi_analyzer C)
set(CMAKE_C_STANDARD 11)
add_executable(roi_analyzer src/main.c src/image.c src/view.c src/roi.c src/analyze.c src/log.c)
target_link_libraries(roi_analyzer -luser32 -lgdi32 -lkernel32 -lcomctl32 -lcomdlg32 -lgdiplus -lshell32 -lm)
set_target_properties(roi_analyzer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin LINK_FLAGS "-mwindows")
```

```bash
cd C:/Github/roi-analyzer
export PATH="/c/msys64/ucrt64/bin:$PATH"   # 同 c-vlcplayer:cc1.exe 需 libmpfr-6.dll
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM="C:/msys64/ucrt64/bin/mingw32-make.exe"
cmake --build build
```

编译选项建议:`-Wall -Wextra`,验收标准为 0 warning。
