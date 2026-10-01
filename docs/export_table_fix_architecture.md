# Export Table 格式修正架構（v3.2 範疇）

> 狀態：待 agy 審查 → gh 實作。只做加法，不動呼叫端（`main.c` 三處呼叫不變）。

## 1. 目標格式（每批 Export 一個區塊）

```
<空行分隔，僅續寫時>
# ==== export [YYYY-MM-DD hh:mm:ss] image=<path> size=WxH mode=<drag|grid3x3|grid5x5> rois=N
id	Ym	Rm	Gm	Bm	Ys	Rs	Gs	Bs	L	A	B	rect	count
1	77.51	65.12	72.69	134.79	34.57	39.18	35.09	30.22	33.13	15.37	-35.85	(526,1641)-(1496,2812)	1138012
...（每 ROI 一行）
```

順序鐵律：`# ==== export` 第一行 → 表頭第二行 → 數據其後。
（前一版錯誤：表頭寫在 `# ====` 之前，導致首行是表頭。）

## 2. `Export_Log`（src/export.c）寫入流程，每個 mode 檔：

1. `fopen(path, "ab")` → `fseek END` → `is_new = (ftell(file) == 0)`
2. `is_new` → `fwrite(BOM EF BB BF)`（Excel 直接開啟中文不亂碼；`report.c` 同手法）
   `!is_new` → `fwrite("\r\n")` 空行分隔（`line[0]=0x0D; line[1]=0x0A; fwrite 2 bytes`，避開 patch 工具對 `\r\n` 字串的斷行破壞）
3. 寫 `# ==== export [...]` 行（原格式不變，`#` 開頭相容舊解析）
4. 寫表頭 `id\tYm\t...count\r\n`（每次 Export 都寫，新檔舊檔一致）
5. 數據行 `%d\t%.2f×11\t(...)\t%d\r\n`，欄序 `id, Ym, Rm, Gm, Bm, Ys, Rs, Gs, Bs, L, A, B, rect, count`（`%.2f` 保留、`clean_zero` 照用）

## 3. 已知損壞（gh 必修）

- 第 142 行表頭字串被 patch 工具寫壞：`\t` 變真 tab、`...count\r\n");` 斷成兩行 → `missing terminating " character`，目前编不過。
- 修法：該行還原為單行 C 字串（`\t` 全為反斜線-t），或改用 `fwrite` 逐段寫表頭避開長字串。

## 4. 失敗處理

- BOM／空行／表頭任一步失敗 → `failed=1`＋`fclose`＋`break`（與現有風格一致）
- `# ====` 行失敗維持原行為（`failed=1`，不 `break`，由迴圈條件收尾）

## 5. 驗證

- `gcc -c -Wall -Wextra` 零警告；`git diff --check` 乾淨
- 完整 `cmake --build build` 過（注意先關舊 exe，否則 link `Permission denied`）
- 手測：新檔（BOM＋順序）／舊檔續寫（空行＋表頭重寫）／貼 Excel 14 欄

## 6. 約束

純 C＋ANSI 全 A 版；CRLF；`_snprintf`＋`write_utf8` 不動；只寫 code 不 commit 不 push。
