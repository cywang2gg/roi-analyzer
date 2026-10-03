#ifndef ROI_CC_CV_H
#define ROI_CC_CV_H

#include <windows.h>

#include <opencv2/core/core_c.h>
#include <opencv2/imgproc/imgproc_c.h>

#include "cc_locate.h"

#define CC_HOUGH_MAX_SEGMENTS 4096
#define CC_CV_BORDER 2

typedef void (*cc_pfn_cvt_color)(const CvArr *src, CvArr *dst, int code);
typedef void (*cc_pfn_smooth)(const CvArr *src, CvArr *dst, int smoothtype,
                              int size1, int size2, double sigma1,
                              double sigma2);
typedef void (*cc_pfn_canny)(const CvArr *image, CvArr *edges,
                             double threshold1, double threshold2,
                             int aperture_size);
typedef CvSeq *(*cc_pfn_hough_lines2)(CvArr *image, void *line_storage,
                                      int method, double rho, double theta,
                                      int threshold, double param1,
                                      double param2, double min_theta,
                                      double max_theta);
typedef IplImage *(*cc_pfn_create_image)(CvSize size, int depth, int channels);
typedef void (*cc_pfn_release_image)(IplImage **image);
typedef CvMemStorage *(*cc_pfn_create_mem_storage)(int block_size);
typedef void (*cc_pfn_release_mem_storage)(CvMemStorage **storage);
typedef schar *(*cc_pfn_get_seq_elem)(const CvSeq *seq, int index);

typedef struct {
    HMODULE core;
    HMODULE imgproc;
    cc_pfn_cvt_color cv_cvt_color;
    cc_pfn_smooth cv_smooth;
    cc_pfn_canny cv_canny;
    cc_pfn_hough_lines2 cv_hough_lines2;
    cc_pfn_create_image cv_create_image;
    cc_pfn_release_image cv_release_image;
    cc_pfn_create_mem_storage cv_create_mem_storage;
    cc_pfn_release_mem_storage cv_release_mem_storage;
    cc_pfn_get_seq_elem cv_get_seq_elem;
    int loaded;
} cc_cv_api_t;

int cc_cv_load(void);
void cc_cv_unload(void);
const cc_cv_api_t *cc_cv_api(void);

int cc_gray_blur_canny(cc_image_view_t bgr, cc_image_t *edge);
int cc_hough_lines_p(cc_image_view_t edge, cc_segment_t *segments,
                     size_t segment_capacity, size_t *segment_count);

#endif
