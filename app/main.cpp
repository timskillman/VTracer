#include "vtracer_cpp/vtracer.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using vtracer::ColorMode;
using vtracer::Config;
using vtracer::Hierarchical;
using vtracer::PathMode;

[[nodiscard]] std::string ToLower(std::string_view input) {
    std::string result(input);
    for (char& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

[[nodiscard]] ColorMode ParseColorMode(std::string_view value) {
    const std::string lower = ToLower(value);
    if (lower == "color") {
        return ColorMode::Color;
    }
    if (lower == "bw" || lower == "binary") {
        return ColorMode::Binary;
    }
    throw std::invalid_argument("Unknown color mode: " + std::string(value));
}

[[nodiscard]] Hierarchical ParseHierarchical(std::string_view value) {
    const std::string lower = ToLower(value);
    if (lower == "stacked") {
        return Hierarchical::Stacked;
    }
    if (lower == "cutout") {
        return Hierarchical::Cutout;
    }
    throw std::invalid_argument("Unknown hierarchical mode: " + std::string(value));
}

[[nodiscard]] PathMode ParsePathMode(std::string_view value) {
    const std::string lower = ToLower(value);
    if (lower == "pixel" || lower == "none") {
        return PathMode::Pixel;
    }
    if (lower == "polygon") {
        return PathMode::Polygon;
    }
    if (lower == "spline") {
        return PathMode::Spline;
    }
    throw std::invalid_argument("Unknown curve mode: " + std::string(value));
}

[[nodiscard]] int ParseInt(std::string_view option_name, std::string_view value) {
    try {
        return std::stoi(std::string(value));
    } catch (const std::exception&) {
        throw std::invalid_argument("Expected integer for " + std::string(option_name));
    }
}

[[nodiscard]] double ParseDouble(std::string_view option_name, std::string_view value) {
    try {
        return std::stod(std::string(value));
    } catch (const std::exception&) {
        throw std::invalid_argument("Expected number for " + std::string(option_name));
    }
}

void PrintHelp() {
    std::cout
        << "visioncortex VTracer C++ port\n"
        << "Usage:\n"
        << "  vtracer_cli --input <input> --output <output> [options]\n\n"
        << "Options:\n"
        << "  -i, --input <path>                Path to input raster image\n"
        << "  -o, --output <path>               Path to output SVG file\n"
        << "      --colormode <color|bw>        True color or binary mode\n"
        << "      --hierarchical <stacked|cutout>\n"
        << "                                    Layering strategy for color mode\n"
        << "      --preset <bw|poster|photo>    Apply a preset configuration\n"
        << "  -f, --filter_speckle <n>          Discard regions smaller than n*n pixels\n"
        << "  -p, --color_precision <1-8>       Significant bits per RGB channel\n"
        << "  -g, --gradient_step <0-255>       Color bucketing step\n"
        << "  -c, --corner_threshold <deg>      Preserve corners sharper than this angle\n"
        << "  -l, --segment_length <value>      Smoothing/simplification scale\n"
        << "  -s, --splice_threshold <deg>      Angle where spline handles are cut back\n"
        << "  -m, --mode <pixel|polygon|spline> Output path mode\n"
        << "      --path_precision <digits>     Decimal places in SVG coordinates\n"
        << "      --help                        Show this help text\n"
        << "      --version                     Show version information\n";
}

void PrintVersion() {
    std::cout << "vtracer_cli 0.1.0\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Config config;
        std::optional<std::filesystem::path> input_path;
        std::optional<std::filesystem::path> output_path;

        std::vector<std::string> args(argv + 1, argv + argc);
        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string& arg = args[i];
            const auto require_value = [&](std::string_view option_name) -> std::string {
                if (i + 1 >= args.size()) {
                    throw std::invalid_argument("Missing value for " + std::string(option_name));
                }
                ++i;
                return args[i];
            };

            if (arg == "--help" || arg == "-h") {
                PrintHelp();
                return EXIT_SUCCESS;
            }
            if (arg == "--version" || arg == "-V") {
                PrintVersion();
                return EXIT_SUCCESS;
            }
            if (arg == "--input" || arg == "-i") {
                input_path = require_value(arg);
                continue;
            }
            if (arg == "--output" || arg == "-o") {
                output_path = require_value(arg);
                continue;
            }
            if (arg == "--preset") {
                config = Config::FromPreset(require_value(arg));
                continue;
            }
            if (arg == "--colormode") {
                config.color_mode = ParseColorMode(require_value(arg));
                continue;
            }
            if (arg == "--hierarchical") {
                config.hierarchical = ParseHierarchical(require_value(arg));
                continue;
            }
            if (arg == "--mode" || arg == "-m") {
                config.mode = ParsePathMode(require_value(arg));
                continue;
            }
            if (arg == "--filter_speckle" || arg == "-f") {
                config.filter_speckle = static_cast<std::size_t>(ParseInt(arg, require_value(arg)));
                continue;
            }
            if (arg == "--color_precision" || arg == "-p") {
                config.color_precision = ParseInt(arg, require_value(arg));
                continue;
            }
            if (arg == "--gradient_step" || arg == "-g") {
                config.layer_difference = ParseInt(arg, require_value(arg));
                continue;
            }
            if (arg == "--corner_threshold" || arg == "-c") {
                config.corner_threshold = ParseInt(arg, require_value(arg));
                continue;
            }
            if (arg == "--segment_length" || arg == "-l") {
                config.length_threshold = ParseDouble(arg, require_value(arg));
                continue;
            }
            if (arg == "--splice_threshold" || arg == "-s") {
                config.splice_threshold = ParseInt(arg, require_value(arg));
                continue;
            }
            if (arg == "--path_precision") {
                config.path_precision = static_cast<unsigned int>(ParseInt(arg, require_value(arg)));
                continue;
            }

            throw std::invalid_argument("Unknown argument: " + arg);
        }

        if (!input_path.has_value()) {
            throw std::invalid_argument("Input path is required. Use --input <path>.");
        }
        if (!output_path.has_value()) {
            throw std::invalid_argument("Output path is required. Use --output <path>.");
        }

        vtracer::TraceResult result = vtracer::TraceImageFile(*input_path, config);
        vtracer::WriteSvgFile(result, *output_path);

        std::cout << "Input:  " << input_path->string() << '\n';
        std::cout << "Output: " << output_path->string() << '\n';
        std::cout << "Size:   " << result.width << "x" << result.height << '\n';
        std::cout << "Regions detected: " << result.stats.total_regions << '\n';
        std::cout << "Regions filtered: " << result.stats.filtered_regions << '\n';
        std::cout << "Paths written:    " << result.stats.output_paths << '\n';
        std::cout << "Conversion successful.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Conversion failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
