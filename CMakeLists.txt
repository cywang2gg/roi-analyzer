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
    src/table.c
    src/export.c
    src/histogram.c
    src/histpanel.c
    src/compare_image.c
    src/compare_core.c
    src/compare_snap.c
    src/compare_v1.c
    src/compare_v2.c
    src/rotate.c
    src/image_save.c
    src/settings.c
    src/rename.c
    src/monitor.c
    src/metrics.c
    src/masks.c
    src/metrics_v2.c
    src/ranking.c
    src/views.c
    src/fft.c
    src/report.c
    src/metrics_async.c
)
target_compile_options(roi_analyzer PRIVATE "$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra>")
target_include_directories(roi_analyzer PRIVATE src)
target_compile_definitions(roi_analyzer PRIVATE NOMINMAX)
target_link_libraries(roi_analyzer user32 gdi32 kernel32 comctl32 comdlg32 gdiplus shell32 shlwapi windowscodecs ole32 uuid m)
set_target_properties(roi_analyzer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin LINK_FLAGS "-mwindows")
