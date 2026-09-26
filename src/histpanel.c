#include "histpanel.h"

#include <math.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windowsx.h>
#include <commctrl.h>

#define IDC_HIST_CHANNEL 1
#define IDC_HIST_LOG     2

typedef struct {
    HWND combo;
    HWND log;
    hist_channel_t channel;
    BOOL log_scale;
    histogram_t hist;
    wchar_t label[160];
    int hover_level;
    BOOL selecting;
    int sel_lo, sel_hi;
    RECT graph;
    RECT ramp;
    RECT stats;
    BOOL tracking;
} hist_panel_t;

static const char *const g_channel_names[] = {
    "RGB", "Luminosity (Y)", "Red", "Green", "Blue"
};

static BOOL g_create_failure_reported;

static void report_create_failure(const char *control)
{
    char message[160];
    DWORD error = GetLastError();
    snprintf(message, sizeof(message), "Create %s failed (GetLastError=%lu).",
             control, (unsigned long)error);
    MessageBoxA(NULL, message, "ROI Analyzer", MB_OK | MB_ICONERROR);
    g_create_failure_reported = TRUE;
}

static hist_panel_t *panel_state(HWND hwnd)
{
    return (hist_panel_t *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

static void update_layout(HWND hwnd, hist_panel_t *panel)
{
    RECT rc;
    int width, height, graph_bottom;
    GetClientRect(hwnd, &rc);
    width = rc.right;
    height = rc.bottom;
    SetWindowPos(panel->combo, NULL, 8, 3, width > 160 ? width - 100 : 60, 220,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(panel->log, NULL, width - 82, 4, 74, 22,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    panel->graph.left = 8;
    panel->graph.right = width - 8;
    panel->graph.top = 52;
    graph_bottom = height - 132;
    if (graph_bottom < panel->graph.top + 20)
        graph_bottom = panel->graph.top + 20;
    panel->graph.bottom = graph_bottom;
    panel->ramp.left = 8;
    panel->ramp.right = width - 8;
    panel->ramp.top = graph_bottom + 8;
    panel->ramp.bottom = panel->ramp.top + 10;
    panel->stats.left = 8;
    panel->stats.top = panel->ramp.bottom + 8;
    panel->stats.right = width - 8;
    panel->stats.bottom = height - 4;
}

static int current_channel_index(hist_channel_t channel)
{
    switch (channel) {
    case HCH_Y: return HIST_Y;
    case HCH_R: return HIST_R;
    case HCH_G: return HIST_G;
    case HCH_B: return HIST_B;
    case HCH_RGB:
    default: return HIST_R;
    }
}

static COLORREF channel_color(hist_channel_t channel)
{
    switch (channel) {
    case HCH_R: return RGB(230, 60, 60);
    case HCH_G: return RGB(60, 200, 60);
    case HCH_B: return RGB(70, 110, 240);
    case HCH_Y:
    default: return RGB(200, 200, 200);
    }
}

static unsigned int dib_color(COLORREF color)
{
    return ((unsigned int)GetRValue(color) << 16) |
           ((unsigned int)GetGValue(color) << 8) |
           (unsigned int)GetBValue(color);
}

static int level_height(unsigned int value, unsigned int maximum, int height,
                        BOOL logarithmic)
{
    double ratio;
    if (value == 0 || maximum == 0 || height <= 0)
        return 0;
    if (logarithmic)
        ratio = log(1.0 + value) / log(1.0 + maximum);
    else
        ratio = (double)value / maximum;
    return (int)(ratio * height + 0.5);
}

static unsigned int bucket_max(const histogram_t *hist, int channel,
                              int x, int width)
{
    int lo = (int)((long long)x * 256 / width);
    int hi = (int)((long long)(x + 1) * 256 / width) - 1;
    unsigned int value = 0;
    int level;
    if (lo > 255)
        lo = 255;
    if (hi < lo)
        hi = lo;
    if (hi > 255)
        hi = 255;
    for (level = lo; level <= hi; level++) {
        if (hist->bin[channel][level] > value)
            value = hist->bin[channel][level];
    }
    return value;
}

static void paint_chart(HDC hdc, hist_panel_t *panel)
{
    int width = panel->graph.right - panel->graph.left;
    int height = panel->graph.bottom - panel->graph.top;
    BITMAPINFO bmi;
    HBITMAP bitmap;
    void *bits = NULL;
    HDC memory;
    HGDIOBJ old_bitmap;
    unsigned int maximum = 0;
    unsigned int *pixels;
    int x, y, ch;

    if (width <= 0 || height <= 0)
        return;
    if (panel->channel == HCH_RGB) {
        for (ch = HIST_R; ch <= HIST_B; ch++)
            if (panel->hist.max_bin[ch] > maximum)
                maximum = panel->hist.max_bin[ch];
    } else {
        maximum = panel->hist.max_bin[current_channel_index(panel->channel)];
    }
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    memory = CreateCompatibleDC(hdc);
    if (!bitmap || !memory || !bits) {
        if (bitmap)
            DeleteObject(bitmap);
        if (memory)
            DeleteDC(memory);
        FillRect(hdc, &panel->graph, GetSysColorBrush(COLOR_WINDOW));
        return;
    }
    old_bitmap = SelectObject(memory, bitmap);
    pixels = (unsigned int *)bits;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int level = (int)((long long)x * 256 / width);
            BOOL active[3] = { FALSE, FALSE, FALSE };
            COLORREF color = RGB(40, 40, 40);
            if (level > 255)
                level = 255;
            if (panel->sel_lo >= 0 && level >= panel->sel_lo &&
                level <= panel->sel_hi)
                color = RGB(70, 70, 70);
            if (panel->hist.valid) {
                if (panel->channel == HCH_RGB) {
                    for (ch = HIST_R; ch <= HIST_B; ch++) {
                        unsigned int value = bucket_max(&panel->hist, ch, x, width);
                        active[ch] = height - y <= level_height(value, maximum,
                                                  height, panel->log_scale);
                    }
                    if (active[0] && active[1] && active[2])
                        color = RGB(200, 200, 200);
                    else if (active[0] && active[1])
                        color = RGB(230, 210, 60);
                    else if (active[0] && active[2])
                        color = RGB(220, 80, 220);
                    else if (active[1] && active[2])
                        color = RGB(60, 210, 220);
                    else if (active[0])
                        color = RGB(230, 60, 60);
                    else if (active[1])
                        color = RGB(60, 200, 60);
                    else if (active[2])
                        color = RGB(70, 110, 240);
                } else {
                    int index = current_channel_index(panel->channel);
                    unsigned int value = bucket_max(&panel->hist, index, x, width);
                    if (height - y <= level_height(value, maximum, height,
                                                   panel->log_scale))
                        color = channel_color(panel->channel);
                }
            }
            pixels[(size_t)y * (size_t)width + (size_t)x] =
                dib_color(color);
        }
    }
    if (panel->hover_level >= 0 && panel->hover_level <= 255) {
        int hover_x = (int)((long long)panel->hover_level * width / 256);
        if (hover_x >= width)
            hover_x = width - 1;
        for (y = 0; y < height; y++)
            pixels[(size_t)y * (size_t)width + (size_t)hover_x] =
                dib_color(RGB(255, 255, 255));
    }
    BitBlt(hdc, panel->graph.left, panel->graph.top, width, height,
           memory, 0, 0, SRCCOPY);
    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

static void paint_ramp(HDC hdc, const hist_panel_t *panel)
{
    int width = panel->ramp.right - panel->ramp.left;
    int x;
    for (x = 0; x < width; x++) {
        int level = width > 1 ? x * 255 / (width - 1) : 0;
        COLORREF color;
        if (panel->channel == HCH_R)
            color = RGB(level, 0, 0);
        else if (panel->channel == HCH_G)
            color = RGB(0, level, 0);
        else if (panel->channel == HCH_B)
            color = RGB(0, 0, level);
        else
            color = RGB(level, level, level);
        SetPixelV(hdc, panel->ramp.left + x, panel->ramp.top, color);
        if (panel->ramp.bottom - panel->ramp.top > 1) {
            RECT stripe = { panel->ramp.left + x, panel->ramp.top + 1,
                            panel->ramp.left + x + 1, panel->ramp.bottom };
            HBRUSH brush = CreateSolidBrush(color);
            if (brush) {
                FillRect(hdc, &stripe, brush);
                DeleteObject(brush);
            }
        }
    }
}

static void paint_stats(HDC hdc, hist_panel_t *panel)
{
    char line[160];
    int y = panel->stats.top;
    int line_height = 16;
    int ch, channels[3], count = panel->channel == HCH_RGB ? 3 : 1;
    static const char *const short_names[] = { "R", "G", "B", "Y" };
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    if (!panel->hist.valid) {
        TextOutA(hdc, panel->stats.left, y, "No image", 8);
        return;
    }
    if (panel->channel == HCH_RGB) {
        channels[0] = HIST_R;
        channels[1] = HIST_G;
        channels[2] = HIST_B;
    } else {
        channels[0] = current_channel_index(panel->channel);
    }
    for (ch = 0; ch < count && y + line_height <= panel->stats.bottom; ch++) {
        int index = channels[ch];
        _snprintf(line, sizeof(line), "%s Mean: %.2f  StdDev: %.2f  Median: %d",
                  short_names[index], panel->hist.mean[index],
                  panel->hist.std[index], panel->hist.median[index]);
        line[sizeof(line) - 1] = '\0';
        TextOutA(hdc, panel->stats.left, y, line, (int)strlen(line));
        y += line_height;
    }
    if (panel->channel != HCH_RGB && y + line_height <= panel->stats.bottom) {
        _snprintf(line, sizeof(line), "Pixels: %u", panel->hist.count);
        TextOutA(hdc, panel->stats.left, y, line, (int)strlen(line));
        y += line_height;
    }
    if (panel->sel_lo >= 0 && y + line_height <= panel->stats.bottom) {
        hist_range_t range;
        int index = current_channel_index(panel->channel);
        Hist_RangeStats(&panel->hist, index, panel->sel_lo, panel->sel_hi, &range);
        _snprintf(line, sizeof(line), "Level: %d..%d  Count: %u  Pct: %.2f%%",
                  panel->sel_lo, panel->sel_hi, range.count, range.percentile);
        TextOutA(hdc, panel->stats.left, y, line, (int)strlen(line));
    } else if (panel->hover_level >= 0 && y + line_height <= panel->stats.bottom) {
        if (panel->channel == HCH_RGB) {
            _snprintf(line, sizeof(line), "Level: %d  R:%u G:%u B:%u",
                      panel->hover_level,
                      panel->hist.bin[HIST_R][panel->hover_level],
                      panel->hist.bin[HIST_G][panel->hover_level],
                      panel->hist.bin[HIST_B][panel->hover_level]);
        } else {
            hist_range_t range;
            int index = current_channel_index(panel->channel);
            Hist_RangeStats(&panel->hist, index, panel->hover_level,
                            panel->hover_level, &range);
            _snprintf(line, sizeof(line), "Level: %d  Count: %u  Pct: %.2f%%",
                      panel->hover_level, range.count, range.percentile);
        }
        line[sizeof(line) - 1] = '\0';
        TextOutA(hdc, panel->stats.left, y, line, (int)strlen(line));
    }
}

static void notify_channel(HWND hwnd, hist_panel_t *panel)
{
    nm_histpanel_t notify;
    notify.hdr.hwndFrom = hwnd;
    notify.hdr.idFrom = (UINT_PTR)GetWindowLongPtr(hwnd, GWLP_ID);
    notify.hdr.code = HPN_CHANNELCHANGED;
    notify.channel = panel->channel;
    SendMessage(GetParent(hwnd), WM_NOTIFY, notify.hdr.idFrom,
                (LPARAM)&notify);
}

static void notify_log_scale(HWND hwnd, hist_panel_t *panel)
{
    nm_histpanel_t notify;
    notify.hdr.hwndFrom = hwnd;
    notify.hdr.idFrom = (UINT_PTR)GetWindowLongPtr(hwnd, GWLP_ID);
    notify.hdr.code = HPN_LOGSCALECHANGED;
    notify.channel = panel->channel;
    notify.log_scale = panel->log_scale;
    SendMessage(GetParent(hwnd), WM_NOTIFY, notify.hdr.idFrom,
                (LPARAM)&notify);
}

static int pointer_level(const hist_panel_t *panel, int x)
{
    int width = panel->graph.right - panel->graph.left;
    int level;
    if (width <= 0)
        return -1;
    level = (int)((long long)(x - panel->graph.left) * 256 / width);
    if (level < 0)
        level = 0;
    if (level > 255)
        level = 255;
    return level;
}

static LRESULT CALLBACK HistPanelWndProc(HWND hwnd, UINT message,
                                         WPARAM wparam, LPARAM lparam)
{
    hist_panel_t *panel = panel_state(hwnd);
    switch (message) {
    case WM_CREATE: {
        CREATESTRUCTA *create = (CREATESTRUCTA *)lparam;
        panel = (hist_panel_t *)calloc(1, sizeof(*panel));
        if (!panel) {
            MessageBoxA(NULL, "Could not initialize Histogram panel (out of memory).",
                        "ROI Analyzer", MB_OK | MB_ICONERROR);
            g_create_failure_reported = TRUE;
            return -1;
        }
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)panel);
        panel->hover_level = -1;
        panel->sel_lo = -1;
        panel->combo = CreateWindowExA(0, WC_COMBOBOXA, "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                0, 0, 0, 0, hwnd, (HMENU)IDC_HIST_CHANNEL,
                create->hInstance, NULL);
        if (!panel->combo) {
            report_create_failure("Histogram channel selector");
            return -1;
        }
        panel->log = CreateWindowExA(0, "BUTTON", "Log",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                0, 0, 0, 0, hwnd, (HMENU)IDC_HIST_LOG,
                create->hInstance, NULL);
        if (!panel->log) {
            report_create_failure("Histogram Log button");
            return -1;
        }
        {
            int i;
            for (i = 0; i < HCH_COUNT; i++)
                SendMessageA(panel->combo, CB_ADDSTRING, 0,
                             (LPARAM)g_channel_names[i]);
            SendMessage(panel->combo, CB_SETCURSEL, HCH_RGB, 0);
        }
        update_layout(hwnd, panel);
        return 0;
    }
    case WM_SIZE:
        if (panel) {
            update_layout(hwnd, panel);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_COMMAND:
        if (panel && LOWORD(wparam) == IDC_HIST_CHANNEL &&
            HIWORD(wparam) == CBN_SELCHANGE) {
            panel->channel = (hist_channel_t)SendMessage(panel->combo,
                                                          CB_GETCURSEL, 0, 0);
            InvalidateRect(hwnd, NULL, FALSE);
            notify_channel(hwnd, panel);
        } else if (panel && LOWORD(wparam) == IDC_HIST_LOG &&
                   HIWORD(wparam) == BN_CLICKED) {
            panel->log_scale = SendMessage(panel->log, BM_GETCHECK, 0, 0) ==
                               BST_CHECKED;
            InvalidateRect(hwnd, NULL, FALSE);
            notify_log_scale(hwnd, panel);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (panel) {
            POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            if (!panel->tracking) {
                TRACKMOUSEEVENT track;
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                track.dwHoverTime = 0;
                panel->tracking = TrackMouseEvent(&track);
            }
            if (PtInRect(&panel->graph, point)) {
                panel->hover_level = pointer_level(panel, point.x);
                if (panel->selecting)
                    panel->sel_hi = panel->hover_level;
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (!panel->selecting && panel->hover_level >= 0) {
                panel->hover_level = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (panel) {
            panel->tracking = FALSE;
            if (!panel->selecting) {
                panel->hover_level = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (panel) {
            POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            if (PtInRect(&panel->graph, point)) {
                panel->hover_level = pointer_level(panel, point.x);
                panel->sel_lo = panel->sel_hi = panel->hover_level;
                panel->selecting = TRUE;
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (panel && panel->selecting) {
            int lo, hi;
            POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            panel->hover_level = pointer_level(panel, point.x);
            panel->sel_hi = panel->hover_level;
            lo = panel->sel_lo < panel->sel_hi ? panel->sel_lo : panel->sel_hi;
            hi = panel->sel_lo > panel->sel_hi ? panel->sel_lo : panel->sel_hi;
            panel->sel_lo = lo;
            panel->sel_hi = hi;
            panel->selecting = FALSE;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_RBUTTONDOWN:
        if (panel) {
            POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            if (PtInRect(&panel->graph, point)) {
                panel->sel_lo = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_PAINT:
        if (panel) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            char source_prefix[] = "Source: ";
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            TextOutA(hdc, 8, 32, source_prefix, (int)strlen(source_prefix));
            if (panel->label[0])
                TextOutW(hdc, 56, 32, panel->label,
                         (int)wcslen(panel->label));
            else
                TextOutA(hdc, 56, 32, "No image", 8);
            paint_chart(hdc, panel);
            paint_ramp(hdc, panel);
            paint_stats(hdc, panel);
            EndPaint(hwnd, &ps);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (panel) {
            free(panel);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

BOOL HistPanel_Register(HINSTANCE hinst)
{
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = HistPanelWndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = "RoiAnalyzerHistogram";
    return RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND HistPanel_Create(HWND parent, int ctrl_id)
{
    HWND hwnd;
    g_create_failure_reported = FALSE;
    hwnd = CreateWindowExA(WS_EX_CLIENTEDGE, "RoiAnalyzerHistogram", "",
                           WS_CHILD | WS_CLIPSIBLINGS,
                           0, 0, 0, 0, parent, (HMENU)(INT_PTR)ctrl_id,
                           (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE),
                           NULL);
    if (!hwnd && !g_create_failure_reported)
        report_create_failure("Histogram");
    return hwnd;
}

void HistPanel_SetSource(HWND hwnd, const image_t *img, const RECT *rc,
                         const wchar_t *label, unsigned int img_gen)
{
    hist_panel_t *panel = panel_state(hwnd);
    RECT source;
    BOOL whole;
    if (!panel)
        return;
    whole = rc == NULL;
    if (whole) {
        source.left = 0;
        source.top = 0;
        source.right = img && img->valid ? img->w - 1 : -1;
        source.bottom = img && img->valid ? img->h - 1 : -1;
    } else {
        source = *rc;
    }
    if (label) {
        wcsncpy(panel->label, label,
                sizeof(panel->label) / sizeof(panel->label[0]) - 1);
        panel->label[sizeof(panel->label) / sizeof(panel->label[0]) - 1] = L'\0';
    } else {
        panel->label[0] = L'\0';
    }
    if (panel->hist.valid && panel->hist.img_gen == img_gen &&
        panel->hist.whole == whole &&
        memcmp(&panel->hist.src, &source, sizeof(source)) == 0) {
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    Hist_Compute(img, whole ? NULL : &source, img_gen, &panel->hist);
    panel->sel_lo = -1;
    panel->sel_hi = -1;
    panel->hover_level = -1;
    InvalidateRect(hwnd, NULL, FALSE);
}

void HistPanel_ClearSource(HWND hwnd)
{
    hist_panel_t *panel = panel_state(hwnd);
    if (!panel)
        return;
    Hist_Reset(&panel->hist);
    panel->label[0] = L'\0';
    panel->sel_lo = -1;
    panel->sel_hi = -1;
    panel->hover_level = -1;
    InvalidateRect(hwnd, NULL, FALSE);
}

void HistPanel_SetChannel(HWND hwnd, hist_channel_t channel)
{
    hist_panel_t *panel = panel_state(hwnd);
    if (!panel || channel < HCH_RGB || channel >= HCH_COUNT)
        return;
    panel->channel = channel;
    SendMessage(panel->combo, CB_SETCURSEL, channel, 0);
    InvalidateRect(hwnd, NULL, FALSE);
}

hist_channel_t HistPanel_GetChannel(HWND hwnd)
{
    hist_panel_t *panel = panel_state(hwnd);
    return panel ? panel->channel : HCH_RGB;
}

void HistPanel_SetLogScale(HWND hwnd, BOOL on)
{
    hist_panel_t *panel = panel_state(hwnd);
    if (!panel)
        return;
    panel->log_scale = on;
    SendMessage(panel->log, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    InvalidateRect(hwnd, NULL, FALSE);
}
