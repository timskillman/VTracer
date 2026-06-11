#pragma once

#include <wchar.h>

#ifdef _WIN32
#define VTRACER_API __declspec(dllexport)
#else
#define VTRACER_API
#endif

extern "C" {

enum VTracerColorMode {
    VTRACER_COLOR_MODE_COLOR = 0,
    VTRACER_COLOR_MODE_BINARY = 1,
};

enum VTracerHierarchicalMode {
    VTRACER_HIERARCHICAL_STACKED = 0,
    VTRACER_HIERARCHICAL_CUTOUT = 1,
};

enum VTracerPathMode {
    VTRACER_PATH_MODE_PIXEL = 0,
    VTRACER_PATH_MODE_POLYGON = 1,
    VTRACER_PATH_MODE_SPLINE = 2,
};

struct VTracerNativeConfig {
    int color_mode;
    int hierarchical;
    int filter_speckle;
    int color_precision;
    int layer_difference;
    int mode;
    int corner_threshold;
    double length_threshold;
    int max_iterations;
    int splice_threshold;
    int path_precision;
    int has_path_precision;
};

struct VTracerNativeStats {
    int total_regions;
    int traced_regions;
    int filtered_regions;
    int output_paths;
};

VTRACER_API void vtracer_fill_default_config(struct VTracerNativeConfig* config);
VTRACER_API int vtracer_trace_file_w(
    const wchar_t* input_path,
    const wchar_t* output_path,
    const struct VTracerNativeConfig* config,
    struct VTracerNativeStats* stats,
    wchar_t* error_buffer,
    int error_buffer_length
);

}
