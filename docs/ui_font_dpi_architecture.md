# UI 高清化（字型統一＋DPI Awareness）架構書 v1.2

> 專案：`C:\Github\roi-analyzer`（純 C ＋ Win32 ＋ GDI ＋ WIC，ANSI 全 A 版，`-Wall -Wextra` 零警告，C11，宣告置頂，禁 `//` 註解）
> 基準：`master`（v3.4，`5abbb17`）
> 問題：使用者回報「程式字體很粗、UI 不夠清晰」
> 日期：2026-10-05
> 狀態：**v1.2（併入 agy round-2 review，GO）→ 發包 gh**

## 變更歷史

| 版本 | 日期 | 內容 |
|---|---|---|
| v1.0 | 2026-10-05 | 初稿（root cause 2 項＋方案 A/B＋P1/P2 分段） |
| v1.1 | 2026-10-05 | 併入 agy review round 1（V1–V12）：V4 histpanel 統計文字為 GDI `TextOut` 非子控制項→新增 P1-c 在 `histpanel.c` 自行處理字型；V5 更正「動態建立」為 `WM_CREATE` 同步建立；V6 `ui_font` 改 file-static `s_ui_font`（區域變數 `WM_CREATE` 看不到）；V7 改 `LOGFONTA`＋`CreateFontIndirectA`（純 ANSI）；V8 移除生命週期矛盾句；V9 `DeleteObject` 移到 WinMain 訊息迴圈後；V10 套用須在 `Layout()` 前；V11 owner-draw 狀態列第 3 區自選字型。V1/V2/V3/V12 PASS 不變（見 §7） |
| v1.2 | 2026-10-05 | 併入 agy review round 2（GO）：F1 `WM_DRAWITEM` 與 `back_dc` 選字型後須存回 `old_font`（GDI 契約）；F2 `s_ui_font = create_ui_font();` 必須在 WinMain 的 `Compare_Init`（`main.c:2604`）**之前**（誤放 `WM_CREATE` 會使 `Compare_Init` 收到 NULL）；F3 `font_height` 用 `GetTextMetricsA` 的 `tmHeight + 1`（96dpi 得 16，與現行版面一致），非 `GetObjectA` `lfHeight` 絕對值（得 12，過擠） |

---

## 0. Root Cause（實碼查證）

| # | 發現 | 證據 | 症狀 |
|---|---|---|---|
| RC1 | `src/app.manifest` **無 `dpiAware` 宣告**（grep 0 筆） | `app.manifest` 全文只有 ComCtl v6 dependency＋trustInfo | DPI-unaware：在 125%/150% 螢幕縮放下，DWM 把整個視窗**點陣放大**（bitmap stretch），文字與 UI 全部模糊變粗 |
| RC2 | `main.c` **未對任何主視窗控制項送 `WM_SETFONT`**（grep：`WM_SETFONT` 僅存在於 `compare_v1.c`／`compare_v2.c`） | `main.c:2178` 建立狀態列後無 font 設定 | 主視窗狀態列、ListView、按鈕、對話框全部使用系統預設字型（粗體點陣字 System/Tahoma），而非 Segoe UI |

**與 Win32/Win64 無關**（x64 編譯不影響字型渲染）；Common-Controls v6 manifest 已有，控制項視覺樣式正常。

## 1. 現況地圖（實碼錨點，v1.1 更正）

| 位置 | 現況 | 需求 |
|---|---|---|
| `main.c` WinMain | 無 `HFONT` 變數、無 `WM_SETFONT`、無 DPI API | 建立全域 UI 字型＋套用＋`Compare_Init` 傳入 |
| `src/app.manifest` | 無 dpiAware | 加 System DPI aware（方案 A）或 Per-Monitor V2（方案 B） |
| `compare_core.c:42` `Cmp_UiFont()` | `s_font ? s_font : DEFAULT_GUI_FONT` | P1 完成後 `Compare_Init` 傳入真 Segoe UI 字型，比較視窗自動受惠（不必改） |
| `main.c:2604` `Compare_Init(instance, (HFONT)GetStockObject(DEFAULT_GUI_FONT))` | 傳 stock font | 改傳 `s_ui_font`（1 行） |
| `main.c:2174` `WM_CREATE` | 同步建立全部控制項（狀態列 2178、canvas 2186、tabs 2219、hist 2228、table 2233），最後 `Layout()`（2251） | 在 `Layout()` **之前**呼叫 `App_SetControlFont(hwnd, s_ui_font)` |
| `main.c:2259` `WM_DRAWITEM` | owner-draw 狀態列第 3 區（`SB_PART_INDEX`）直接 `DrawTextA`，未選字型 | 選 `s_ui_font` 進 `draw->hDC` |
| `main.c:2731` WinMain 訊息迴圈後 | `DestroyAcceleratorTable`／`Image_Free`／`FileList_Free`／`ROI_Destroy`／`GdiplusShutdown`／`CoUninitialize` | 在此處 `DeleteObject(s_ui_font)`（V9：所有視窗已銷毀） |
| `src/histpanel.c:360-420,636-642` | 統計文字（`Source:`/`Mean`/`StdDev`/`Median`/`Pixels`/`Level`）用 GDI `TextOutA/W` 畫在 `panel->back_dc`，**非子控制項**；`HistWndProc` 無 `WM_SETFONT` | V4：新增 `WM_SETFONT` 處理＋繪製時 `SelectObject`（P1-c） |
| `compare_snap.c:67` `CmpSnap_CreateScaledFont` | `MulDiv(lfHeight, dpi, 96)` 建縮放字型 | 不動（V12：System aware 下 scale=1，`MulDiv(-18,96,96)=-18` 直通；新字型 `LOGFONTA` 建立，ABI 相容） |
| 對話框（`IDD_ROTATE` 等 app.rc） | RC 對話框用系統字型 | 用 `DS_SHELLFONT`／`MS Shell Dlg 2` 字型聲明（P2 選做） |

主視窗控制項（`EnumChildWindows` 涵蓋）：狀態列、canvas、tabs、ListView（含 header）、Histogram 面板本體及其 combo/log、訊息區 STATIC、按鈕群。**Histogram 面板的 GDI 統計文字不在涵蓋內**（V4，見 P1-c）。

## 2. 方案

### 方案 A：P1 字型統一＋System DPI aware（本書主線，一次到位）

**P1-a 全域 UI 字型（治「字體很粗」）**

變數存放（V6）：`main.c` 檔頭 file-static，`WM_CREATE`／WinMain 皆可見。

```c
/* File: main.c —— 檔案層級（與 g_accelerators 同區） */
static HFONT s_ui_font;

static HFONT create_ui_font(void)
{
    LOGFONTA lf;          /* V7：純 ANSI，與 compare_snap.c:57 一致 */
    HDC screen;
    int dpi;

    screen = GetDC(NULL);
    dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    if (screen)
        ReleaseDC(NULL, screen);
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(9, dpi, 72);     /* 9pt；負值=em height */
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;        /* 中文 fallback→JhengHei */
    lf.lfQuality = CLEARTYPE_QUALITY;
    lstrcpyA(lf.lfFaceName, "Segoe UI");
    return CreateFontIndirectA(&lf);
}
```

套用 helper（V10）：

```c
/* File: main.c */
static BOOL CALLBACK set_font_proc(HWND child, LPARAM param)
{
    SendMessageA(child, WM_SETFONT, (WPARAM)param, TRUE);
    return TRUE;
}

static void App_SetControlFont(HWND parent, HFONT font)
{
    if (parent && font)
        EnumChildWindows(parent, set_font_proc, (LPARAM)font);
}
```

呼叫點：`MainWndProc` `WM_CREATE` 內，**`Layout()` 之前**（`main.c:2251` 前）：

```c
App_SetControlFont(hwnd, s_ui_font);   /* 狀態列/canvas/tabs/ListView/hist 面板本體 */
Layout();
```

`EnumChildWindows` 遞迴覆蓋全部子視窗（含 ListView header、hist 面板 combo/log 及面板本體——觸發 P1-c 的 `WM_SETFONT`）。**histpanel GDI 統計文字不在涵蓋內**（V4，見 P1-c）。

`Compare_Init(instance, s_ui_font)`（`main.c:2604` 改 1 行）。

**owner-draw 狀態列第 3 區（V11＋F1）**：`WM_DRAWITEM`（`main.c:2259`）在 `DrawTextA` 前後存還原字型（GDI 契約）：

```c
HFONT old_font = (HFONT)SelectObject(draw->hDC,
        s_ui_font ? s_ui_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
DrawTextA(draw->hDC, text ? text : "", -1, &draw->rcItem,
          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
SelectObject(draw->hDC, old_font);
```

**銷毀（V9）**：`WinMain` 訊息迴圈後（`main.c:2731` 區）`if (s_ui_font) DeleteObject(s_ui_font);`（不放 `WM_DESTROY`——那時子視窗仍存活）。

**建立時機（F2）**：`s_ui_font = create_ui_font();` 必須在 `WinMain` 的 `Compare_Init`（`main.c:2604`）**之前**（`CreateWindowExA` 在 2663，`WM_CREATE` 內看不到 WinMain 區域變數；且 `Compare_Init` 需先拿到字型）：

```c
App_InitCommonControls();
s_ui_font = create_ui_font();          /* <-- 必須在此，Compare_Init 之前 */
if (!Compare_Init(instance, s_ui_font)) { ... }
```

**P1-b manifest 加 System DPI aware（治「模糊」）**

```xml
<!-- File: src/app.manifest —— </assembly> 前插入 -->
<application xmlns="urn:schemas-microsoft-com:asm.v3">
  <windowsSettings>
    <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true</dpiAware>
  </windowsSettings>
</application>
```

效果：系統縮放 125%/150% 時座標/字型直接以實際 DPI 配置，無點陣放大。**副作用**：跨不同 DPI 的多螢幕拖曳時不會自動調整（顯示比例固定在啟動螢幕的 DPI）——這是方案 A 的已知限制，若使用者有跨 DPI 螢幕需求才升方案 B。

字型高度以啟動 DPI 計算（`GetDeviceCaps(LOGPIXELSY)`），與 System DPI aware 一致。

### P1-c histpanel 字型（V4，新增）

`histpanel.c` 加：

```c
/* hist_panel_t 追加欄位 */
HFONT font;
int   font_height;   /* 文字行高（由字型度量，供 update_layout/paint_stats） */
```

- `WM_SETFONT`（`HistPanelWndProc` 新增 case）：`panel->font = (HFONT)wparam;` 量 `font_height`（**F3**：`GetTextMetricsA` 的 `tm.tmHeight + 1`；9pt Segoe UI 在 96dpi 得 16，與現行版面一致；`GetObjectA` 的 `lfHeight` 絕對值只得 12，行距過擠）→ `InvalidateRect(hwnd, NULL, FALSE)`。
- `paint_stats`／`WM_PAINT` 的 `back_dc`：`old_font = SelectObject(panel->back_dc, panel->font ? panel->font : GetStockObject(DEFAULT_GUI_FONT))` 後才 `TextOutA/W`，**繪製完 `SelectObject(panel->back_dc, old_font)` 還原（F1）**。
- `update_layout`：`line_height` 由 `font_height` 推導（fallback 16）；`paint_stats` 的 `int line_height = 16;`（`histpanel.c:364`）改為動態值。
- 觸發來源：`App_SetControlFont` 的 `EnumChildWindows` 送 `WM_SETFONT` 到 hist 面板**本體**（它是主視窗子視窗），自動觸發，無需額外呼叫。

### 方案 B：Per-Monitor V2（選做，不在本書 P1/P2 主線）

P1＋manifest 改 Per-Monitor V2＋處理 `WM_DPICHANGED`（重算視窗大小＋全部版面＋字型重建）。改動範圍：主視窗版面函式、canvas、histogram 面板、比較視窗全部版面。**除非使用者提出跨 DPI 螢幕實際需求，否則不做**（ROI Analyzer 是桌面工具，主要單螢幕使用）。P2 完成後 `Snap_PhysicalScale` 的 thread-DPI trick 可簡化（scale 恆 1），**本書不動它**（既有路徑零風險）。

## 3. 實作分段

| 階段 | 內容 | 檔案 | 驗收 |
|---|---|---|---|
| **P1-a** | `s_ui_font`＋`create_ui_font`＋`App_SetControlFont`＋`WM_CREATE` 套用（`Layout()` 前）＋`WM_DRAWITEM` 選字型＋`Compare_Init` 傳入＋WinMain 訊息迴圈後 `DeleteObject` | `main.c`（±約 45 行） | 狀態列/ListView/訊息區/owner-draw 第 3 區變 Segoe UI 非粗體；比較視窗同步 |
| **P1-c** | `histpanel.c` 加 `font`／`font_height`＋`WM_SETFONT` case＋繪製時 `SelectObject`＋`update_layout` 用字高 | `histpanel.c`（±約 25 行） | Histogram 面板 combo/log/統計文字全 Segoe UI |
| **P1-b** | manifest 加 `<dpiAware>true</dpiAware>` | `app.manifest`（＋5 行） | 150% 縮放下 UI 銳利無點陣模糊；版面正常 |
| **P2（選做）** | RC 對話框 `DS_SHELLFONT`＋旋轉對話框字型檢查 | `app.rc` | 旋轉對話框文字同為 Segoe UI |

P1-a／P1-c 同一 commit（同屬「字型統一」）；P1-b 獨立 commit（DPI，可單獨回退、單獨手測）；P2 選做。

## 4. 生命週期與風險（v1.1 更新）

| 項目 | 處理 |
|---|---|
| `s_ui_font` 銷毀 | **WinMain 訊息迴圈後**（`main.c:2731` 區），非 `WM_DESTROY`（V9） |
| 比較視窗共用字型 | `Compare_Font()` 回傳 `s_ui_font`；比較視窗只 select 不 delete（現況如此）；`Compare_CloseAll()` 在主視窗 `WM_DESTROY` 內先執行，早於 `s_ui_font` 銷毀 |
| histpanel 字型 | 不持有所有權（只存指標 select），不 delete |
| DBCS/中文 | `DEFAULT_CHARSET`＋font linking（JhengHei fallback），與現況同機制 |
| `GetDeviceCaps` 時機 | WinMain 最早；方案 A 下生命週期 DPI 不變，只建一次 |
| `EnumChildWindows` 覆蓋範圍 | 建立後靜態子視窗全覆蓋；histpanel GDI 文字另由 P1-c 處理 |

## 5. 驗收清單

| # | 項目 | 預期 |
|---|---|---|
| T1 | 主視窗狀態列（含 owner-draw 第 3 區） | Segoe UI 非粗體，全區一致 |
| T2 | Histogram 面板（combo/log/**統計文字**） | 全 Segoe UI（P1-c 生效） |
| T3 | 比較視窗 V1/V2 工具列與狀態列 | 同上（因 `Compare_Init` 傳入新字型） |
| T4 | 150% 螢幕縮放 | UI 邊緣銳利無毛邊；字型大小與 100% 成正比（等比放大，非點陣糊化） |
| T5 | 中文 UI 字串 | 正常顯示（JhengHei fallback），無豆腐/亂碼 |
| T6 | 比較視窗 Snapshot | 功能照常（`CmpSnap_CreateScaledFont` 路徑不受影響） |
| T7 | 100% 縮放回歸 | 與改版前版面一致（除字型本身），無控制項錯位 |
| T8 | `ctest` | 2/2（UI 改動不觸發測試，僅回歸確認） |

## 6. 發包清單（gh 執行）

**Commit A（P1-a＋P1-c 字型統一）**
1. `main.c`：`static HFONT s_ui_font;`＋`create_ui_font()`（`LOGFONTA`／`CreateFontIndirectA`，Segoe UI 9pt ClearType）＋`set_font_proc`＋`App_SetControlFont`；**`s_ui_font = create_ui_font();` 放在 WinMain `Compare_Init`（2604）之前（F2）**；`WM_CREATE` 內 `Layout()` **前**呼叫 `App_SetControlFont`；`WM_DRAWITEM`（狀態列第 3 區）存還原字型（F1）；`Compare_Init` 傳 `s_ui_font`；WinMain 訊息迴圈後 `DeleteObject`。
2. `histpanel.c`：`font`／`font_height` 欄位＋`WM_SETFONT` case（`font_height` 用 `GetTextMetricsA` `tmHeight+1`，F3）＋`back_dc` 選字型並還原（F1）＋`update_layout`／`paint_stats` 用字高。
3. 建置零警告。

**Commit B（P1-b manifest）**
1. `app.manifest` 插入 `<application><windowsSettings><dpiAware>true</dpiAware></windowsSettings></application>`。
2. 建置（RC 嵌入確認）→ exe 產出。

gh：兩 commit 分開、只實作不 push、不驗證（Hermes 本地複驗＋使用者手測）。

## 7. agy review round 1 處置（V1–V12）

| # | 判定 | 處置 |
|---|---|---|
| V1 RC1 | PASS | 保留 |
| V2 RC2 | PASS | 保留 |
| V3 manifest XML/windres | PASS | 保留 |
| V4 histpanel GDI 文字 | FAIL | **採納**：新增 P1-c |
| V5 doc 誤稱動態建立 | FAIL | **採納**：§1 更正為 `WM_CREATE` 同步建立 |
| V6 `ui_font` 區域變數 | FAIL | **採納**：改 file-static `s_ui_font` |
| V7 LOGFONTW→LOGFONTA | WARN | **採納**：純 ANSI |
| V8 生命週期自相矛盾 | FAIL | **採納**：移除矛盾句 |
| V9 DeleteObject 時機 | WARN | **採納**：移到 WinMain 訊息迴圈後 |
| V10 套用須在 Layout 前 | WARN | **採納**：§2 P1-a 明示 |
| V11 owner-draw 狀態列第 3 區 | WARN | **採納**：§2 P1-a 納入 |
| V12 Snapshot 路徑相容 | PASS | 不動 |
| F1 `WM_DRAWITEM`／`back_dc` 字型還原 | WARN | **採納**：存還原 `old_font`（§2 P1-a／P1-c） |
| F2 `create_ui_font` 呼叫位置 | WARN | **採納**：WinMain `Compare_Init` 之前（§2 P1-a） |
| F3 `font_height` 度量方式 | WARN | **採納**：`GetTextMetricsA` `tmHeight+1`（§2 P1-c） |

## 8. agy review round 2（v1.2）最終判定

**GO**。v1.1 五項修訂全部複驗 PASS（V6 無符號碰撞、V4 三處驗證、V9 生命週期、V10 Layout 順序、V11 契約）；新增 F1/F2/F3 三個實作細節已併入本版 §2。
