# Win32 群組框標題殘影— 問題、原因、根治對策（可移植說明書）

> 來源：roi-analyzer V2 視窗實戰案例（已根治，2026-10-08）。
> 完整過程記錄：`compareformV1V2_architecture_.md` §15（v1.2，codex＋agy 兩輪 review）。
> 適用條件：父視窗含 `WS_CLIPCHILDREN` ＋ `BS_GROUPBOX` 子視窗＋程式會移動/縮放這些子視窗（版面配置、DPI 縮放、最大化）。

---

## 1. 問題現象

- 視窗**最大化或大幅 resize 後**，`BS_GROUPBOX` 的**標題文字出現殘影**（舊位置的字元碎片黏在新標題左側，如 `FPan Sync`、`AActions`）。
- 群組框內會出現**灰色矩形殘塊**（舊控制項位置的殘留）。
- **非最大化時正常**；殘影只在新舊位置差異大時出現。
- 點選視窗（active/inactive 切換）會看到「**上下兩層**」——殘影像是疊在底層。
- 最左邊、位置不動的群組框通常乾淨；會位移的才髒。

## 2. 根本原因：「擦除真空」

兩條 Win32 規則同時作用，造成一個**沒有任何人會擦除的矩形區域**：

| 層 | 規則 | 結果 |
|---|---|---|
| 父視窗 | `WS_CLIPCHILDREN` 使 GDI 在擦父視窗背景時，**扣除所有子視窗的矩形** | 父視窗**永不擦**群組框佔據的區域 |
| 子視窗 | `BS_GROUPBOX`（BUTTON 類別，`hbrBackground = NULL`）只畫蝕刻框＋透明背景的標題文字，**不填自己的內部** | 群組框**也不擦**自己區域 |

→ 舊位置的文字/邊框像素永久留在螢幕表面。版面重排（`MoveWindow`）後群組框移到新位置、以**透明**方式畫上新框與字 → 舊像素透出來＝殘影。

**為何最大化才爆**：位移量小時舊新位置幾乎重疊看不出；最大化時群組框水平位移數百 px，殘影明顯。位置不動的第一個群組框（錨定在原點）自然乾淨。

**為何「點選見兩層」**：啟動/停用切換觸發子視窗層重畫（新 caption 再描一次），但父層的殘留像素不動——兩層差異因此可見。

### 常見錯誤修法（實測無效，勿浪費時間）

| 嘗試 | 為何無效 |
|---|---|
| 父視窗 `InvalidateRect(hwnd, NULL, TRUE)` | `WS_CLIPCHILDREN` 下**子視窗矩形仍被裁掉**，父層還是擦不到 |
| `RedrawWindow(... RDW_INVALIDATE\|RDW_ERASE\|RDW_ALLCHILDREN)` | 會重畫子視窗，但 `BS_GROUPBOX` 的 `WM_ERASEBKGND` 是 no-op——**重畫≠擦除**，子視窗依然不填內部（roi-analyzer 實測失敗） |
| `MoveWindow` 先擦後移、或 `SWP_NOREDRAW`＋事後一次重畫 | 順序不是成因；只要真空區存在，誰都不會 `FillRect` 它 |
| 加 `WS_CLIPSIBLINGS` | 只裁兄弟重疊，不解擦除真空 |
| 父 `WM_ERASEBKGND` 手動 `FillRect` 全客戶區 | GDI 給你的 DC **已被 CLIPCHILDREN 裁過**，填不到子矩形 |
| 父視窗加 `WS_EX_COMPOSITED` | 可能掩蓋，但拖曳/縮放效能風險高，且不解所有權問題 |
| `WM_CTLCOLOR*` handler | 只影響控制項配色，與殘影無關（非根因） |

## 3. 根治對策 C：群組框改父視窗自繪

**核心**：`BS_GROUPBOX` 是純裝飾（不收輸入、不收焦點、無子控制項隸屬），沒有必要做成 HWND。刪掉這些 HWND → 擦除真空消失 → 父視窗背景刷正常擦整個控制列 → 父視窗 `WM_PAINT` 自己畫框＋標題（畫在子控制項之下）。

### 3.1 步驟（以 roi-analyzer `compare_v2.c` 為例）

1. **狀態**：`HWND grp[4]` → `RECT grp_rc[4]`（純座標，layout 計算結果）。
2. **版面函式**：`MoveWindow(state->grp[k], x, y, w, h, TRUE)` → `SetRect(&state->grp_rc[k], x, y, x+w, y+h)`。
3. **`WM_CREATE`**：刪除群組框的 `CreateWindowExA`、null 檢查、`WM_SETFONT`。**互動控制項（按鈕/滑桿/checkbox）全部保留**，parent 維持頂層視窗（本來就是群組框的兄弟，不是子）。
4. **父 `WM_PAINT`（新增）**：

```c
/* 控制列整條先填背景刷——真空區就此消失 */
static void paint_control_bar(HWND hwnd, HDC dc, const RECT *group_rc,
                              const char *const *titles, int count)
{
    RECT client, bar;
    HGDIOBJ old_font;
    int k;

    GetClientRect(hwnd, &client);
    bar.left = 0; bar.top = 0;
    bar.right = client.right;
    bar.bottom = group_rc[0].bottom + /* 或控制列固定高 */;
    FillRect(dc, &bar, (HBRUSH)(COLOR_BTNFACE + 1));

    old_font = SelectObject(dc, ui_font());
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    for (k = 0; k < count; k++) {
        SIZE ts;
        RECT frame, text;
        int len = lstrlenA(titles[k]);
        if (group_rc[k].right <= group_rc[k].left)
            continue;
        GetTextExtentPoint32A(dc, titles[k], len, &ts);
        /* 框：上緣下移半行，留出標題缺口 */
        frame = group_rc[k];
        frame.top += ts.cy / 2;
        DrawEdge(dc, &frame, EDGE_ETCHED, BF_RECT);
        /* 標題：不透明背景蓋掉框線交疊處 */
        text.left   = group_rc[k].left + scale(8);
        text.top    = group_rc[k].top;
        text.right  = text.left + ts.cx + scale(4);
        text.bottom = text.top + ts.cy;
        ExtTextOutA(dc, text.left + scale(2), text.top,
                    ETO_OPAQUE, &text, titles[k], len, NULL);
    }
    SelectObject(dc, old_font);
}

case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    paint_control_bar(hwnd, dc, state->grp_rc, titles, 4);
    EndPaint(hwnd, &ps);
    return 0;
}
```

5. **`WM_SIZE` 重畫**：`InvalidateRect(hwnd, NULL, TRUE)` 即可（沒有子 HWND 後無擦除真空；保留 `RedrawWindow+RDW_ALLCHILDREN` 亦可，無害）。

### 3.2 關鍵細節

- **繪製順序天然正確**：父 `WM_PAINT` 先於子視窗繪製 → 框/標題在控制項之下。
- **caption 不透明背景**（`ETO_OPAQUE`＋BTNFACE）蓋掉框線穿過標題的部分——這是 groupbox 標題缺口的正規畫法。
- **標題字型用與控制項同一顆**（roi-analyzer 用 `Compare_Font()`），座標全走 DPI 縮放函式。
- **Snapshot／離屏渲染路徑零影響**：若既有程式有獨立的快照渲染函式，它不經過父 `WM_PAINT`，行為不變。

## 4. 遷移檢查清單

1. 確認所有群組框**無輸入用途**（無 `BN_CLICKED`、無 tooltip 綁定、無 `EnableWindow` 邏輯）——有的話改綁到代表性子控制項。
2. grep 該 HWND 的所有引用：`CreateWindow`／`MoveWindow`／`SendMessage(WM_SETFONT)`／null 檢查／Destroy 清理，全數移除或改 `RECT`。
3. 父視窗需有 `WM_PAINT`（原本可能靠 DefWindowProc）——**若父視窗原本沒有自繪**，新增後要檢查原本類別背景刷是否還需要。
4. 互動控制項的 parent 維持頂層視窗（不要改成 groupbox 的子——Win32 groupbox 本來就不該當 parent）。
5. 編譯零警告＋手測：最大化/還原/拖曳 resize 連續操作，殘影不再出現；點選切換無兩層感。

## 5. 何时不用此對策

- 群組框**不會被移動**（固定版面、程式生命週期不 resize）→ 殘影不會出現，不必改。
- 專案用 Per-Monitor V2＋`WM_DPICHANGED` 全重繪管線 → 仍可能有同類問題，此對策同樣適用。
- 若群組框有互動（如點標題摺疊）→ 保留 HWND，改走「點擊區自算」再考慮自繪。
