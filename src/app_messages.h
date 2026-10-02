#ifndef ROI_APP_MESSAGES_H
#define ROI_APP_MESSAGES_H

#include <windows.h>

/*
 * Existing assignments: canvas +1; compare +0x40/+0x41; monitor +101;
 * main +102; metrics +103; histogram panel +0x100/+0x101.
 */
#define WM_APP_DETECT_DONE (WM_APP + 104)
#define WM_APP_DETECT_INIT (WM_APP + 105)

#endif
