#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vtracer {

enum class ColorMode {
    Color,
    Binary,
};

enum class Hierarchical {
    Stacked,
    Cutout,
};

enum class PathMode {
    Pixel,
    Polygon,
    Spline,
};

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct Config {
    ColorMode color_mode = ColorMode::Color;
    Hierarchical hierarchical = Hierarchical::Stacked;
    std::size_t filter_speckle = 4;
    int color_precision = 6;
    int layer_difference = 16;
    PathMode mode = PathMode::Spline;
    int corner_threshold = 60;
    double length_threshold = 4.0;
    std::size_t max_iterations = 10;
    int splice_threshold = 45;
    std::optional<unsigned int> path_precision = 2;

    static Config FromPreset(std::string_view preset_name);
};

struct TraceStats {
    std::size_t total_regions = 0;
    std::size_t traced_regions = 0;
    std::size_t filtered_regions = 0;
    std::size_t output_paths = 0;
};

struct SvgPath {
    std::string data;
    Color color;
};

struct TraceResult {
    int width = 0;
    int height = 0;
    std::vector<SvgPath> paths;
    std::string svg;
    TraceStats stats;
};

TraceResult TraceImageFile(const std::filesystem::path& input_path, const Config& config);
TraceResult TraceRgbaImage(
    int width,
    int height,
    const std::vector<Color>& pixels,
    const Config& config
);
void WriteSvgFile(const TraceResult& result, const std::filesystem::path& output_path);
std::string ToHexColor(const Color& color);

}  // namespace vtracer
