#include "vtracer_cpp/vtracer_c_api.h"

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

#include "vtracer_cpp/vtracer.h"

namespace {

[[nodiscard]] vtracer::ColorMode ToColorMode(const int value) {
    return value == VTRACER_COLOR_MODE_BINARY
        ? vtracer::ColorMode::Binary
        : vtracer::ColorMode::Color;
}

[[nodiscard]] vtracer::Hierarchical ToHierarchical(const int value) {
    return value == VTRACER_HIERARCHICAL_CUTOUT
        ? vtracer::Hierarchical::Cutout
        : vtracer::Hierarchical::Stacked;
}

[[nodiscard]] vtracer::PathMode ToPathMode(const int value) {
    switch (value) {
    case VTRACER_PATH_MODE_PIXEL:
        return vtracer::PathMode::Pixel;
    case VTRACER_PATH_MODE_POLYGON:
        return vtracer::PathMode::Polygon;
    case VTRACER_PATH_MODE_SPLINE:
    default:
        return vtracer::PathMode::Spline;
    }
}

[[nodiscard]] std::wstring WidenAscii(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

void WriteError(const std::wstring& message, wchar_t* buffer, const int buffer_length) {
    if (buffer == nullptr || buffer_length <= 0) {
        return;
    }

    const std::size_t max_chars = static_cast<std::size_t>(buffer_length - 1);
    const std::size_t copy_count = std::min(max_chars, message.size());
    std::wmemcpy(buffer, message.c_str(), copy_count);
    buffer[copy_count] = L'\0';
}

[[nodiscard]] vtracer::Config ToConfig(const VTracerNativeConfig& native_config) {
    vtracer::Config config;
    config.color_mode = ToColorMode(native_config.color_mode);
    config.hierarchical = ToHierarchical(native_config.hierarchical);
    config.filter_speckle = native_config.filter_speckle < 0 ? 0 : static_cast<std::size_t>(native_config.filter_speckle);
    config.color_precision = native_config.color_precision;
    config.layer_difference = native_config.layer_difference;
    config.mode = ToPathMode(native_config.mode);
    config.corner_threshold = native_config.corner_threshold;
    config.length_threshold = native_config.length_threshold;
    config.max_iterations = native_config.max_iterations < 0 ? 0 : static_cast<std::size_t>(native_config.max_iterations);
    config.splice_threshold = native_config.splice_threshold;
    config.path_precision = native_config.has_path_precision != 0
        ? std::optional<unsigned int>(static_cast<unsigned int>(native_config.path_precision))
        : std::nullopt;
    return config;
}

void CopyStats(const vtracer::TraceStats& from, VTracerNativeStats* to) {
    if (to == nullptr) {
        return;
    }

    to->total_regions = static_cast<int>(from.total_regions);
    to->traced_regions = static_cast<int>(from.traced_regions);
    to->filtered_regions = static_cast<int>(from.filtered_regions);
    to->output_paths = static_cast<int>(from.output_paths);
}

}  // namespace

extern "C" {

void vtracer_fill_default_config(VTracerNativeConfig* config) {
    if (config == nullptr) {
        return;
    }

    const vtracer::Config defaults;
    config->color_mode = VTRACER_COLOR_MODE_COLOR;
    config->hierarchical = VTRACER_HIERARCHICAL_STACKED;
    config->filter_speckle = static_cast<int>(defaults.filter_speckle);
    config->color_precision = defaults.color_precision;
    config->layer_difference = defaults.layer_difference;
    config->mode = VTRACER_PATH_MODE_SPLINE;
    config->corner_threshold = defaults.corner_threshold;
    config->length_threshold = defaults.length_threshold;
    config->max_iterations = static_cast<int>(defaults.max_iterations);
    config->splice_threshold = defaults.splice_threshold;
    config->path_precision = defaults.path_precision.has_value()
        ? static_cast<int>(*defaults.path_precision)
        : 0;
    config->has_path_precision = defaults.path_precision.has_value() ? 1 : 0;
}

int vtracer_trace_file_w(
    const wchar_t* input_path,
    const wchar_t* output_path,
    const VTracerNativeConfig* config,
    VTracerNativeStats* stats,
    wchar_t* error_buffer,
    const int error_buffer_length
) {
    try {
        if (input_path == nullptr || output_path == nullptr || config == nullptr) {
            WriteError(L"Input path, output path, and config are required.", error_buffer, error_buffer_length);
            return 0;
        }

        const auto input = std::filesystem::path(input_path);
        const auto output = std::filesystem::path(output_path);
        const vtracer::TraceResult result = vtracer::TraceImageFile(input, ToConfig(*config));
        vtracer::WriteSvgFile(result, output);
        CopyStats(result.stats, stats);
        WriteError(L"", error_buffer, error_buffer_length);
        return 1;
    } catch (const std::exception& error) {
        WriteError(WidenAscii(error.what()), error_buffer, error_buffer_length);
        return 0;
    } catch (...) {
        WriteError(L"Unknown tracing failure.", error_buffer, error_buffer_length);
        return 0;
    }
}

}
