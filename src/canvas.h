#ifndef ROI_CANVAS_H
#define ROI_CANVAS_H

#include <windows.h>

BOOL Canvas_Register(HINSTANCE instance);
LRESULT CALLBACK CanvasWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

#endif
