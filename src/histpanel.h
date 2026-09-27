#ifndef ROI_HISTPANEL_H
#define ROI_HISTPANEL_H

#include <windows.h>

#include "histogram.h"

typedef enum {
    HCH_RGB,
    HCH_Y,
    HCH_R,
    HCH_G,
    HCH_B,
    HCH_COUNT
} hist_channel_t;

#define HPN_CHANNELCHANGED (WM_APP + 0x100)
#define HPN_LOGSCALECHANGED (WM_APP + 0x101)

typedef struct {
    NMHDR hdr;
    hist_channel_t channel;
    BOOL log_scale;
} nm_histpanel_t;

BOOL HistPanel_Register(HINSTANCE hinst);
HWND HistPanel_Create(HWND parent, int ctrl_id);
void HistPanel_SetSource(HWND hp, const image_t *img, const RECT *rc,
                         const wchar_t *label, unsigned int img_gen);
void HistPanel_SetLabel(HWND hp, const wchar_t *label);
void HistPanel_ClearSource(HWND hp);
void HistPanel_SetChannel(HWND hp, hist_channel_t ch);
hist_channel_t HistPanel_GetChannel(HWND hp);
void HistPanel_SetLogScale(HWND hp, BOOL on);

#define HISTPANEL_DEF_WIDTH 300
#define HISTPANEL_MIN_WIDTH 220

#endif
