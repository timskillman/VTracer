#include "vtracer_cpp/vtracer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "stb_image.h"

namespace vtracer {

namespace {

struct QuantizedColor {
    int r = 0;
    int g = 0;
    int b = 0;

    [[nodiscard]] bool operator==(const QuantizedColor& other) const = default;
};

struct QuantizedColorHash {
    [[nodiscard]] std::size_t operator()(const QuantizedColor& color) const noexcept {
        std::size_t seed = static_cast<std::size_t>(color.r);
        seed = (seed * 1315423911u) ^ static_cast<std::size_t>(color.g);
        seed = (seed * 1315423911u) ^ static_cast<std::size_t>(color.b);
        return seed;
    }
};

struct RegionKey {
    QuantizedColor color{};
    bool active = false;

    [[nodiscard]] bool operator==(const RegionKey& other) const = default;
};

struct Region {
    QuantizedColor key{};
    Color fill_color{};
    std::vector<int> pixels;
    std::vector<int> holes;
    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int max_y = std::numeric_limits<int>::min();

    [[nodiscard]] std::size_t area() const {
        return pixels.size();
    }
};

struct PointI {
    int x = 0;
    int y = 0;

    [[nodiscard]] bool operator==(const PointI& other) const = default;
};

struct PointIHash {
    [[nodiscard]] std::size_t operator()(const PointI& point) const noexcept {
        const auto x = static_cast<std::uint32_t>(point.x);
        const auto y = static_cast<std::uint32_t>(point.y);
        return (static_cast<std::size_t>(x) << 32U) ^ static_cast<std::size_t>(y);
    }
};

struct PointD {
    double x = 0.0;
    double y = 0.0;
};

struct Loop {
    std::vector<PointD> points;
    double signed_area = 0.0;
};

struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;
};

struct BinaryImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    BinaryImage() = default;

    BinaryImage(const int width_value, const int height_value)
        : width(width_value),
          height(height_value),
          pixels(static_cast<std::size_t>(width_value) * static_cast<std::size_t>(height_value), 0) {}

    [[nodiscard]] bool get(const int x, const int y) const {
        return pixels[static_cast<std::size_t>(y * width + x)] != 0;
    }

    [[nodiscard]] bool get_safe(const int x, const int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return false;
        }
        return get(x, y);
    }

    void set(const int x, const int y, const bool value) {
        pixels[static_cast<std::size_t>(y * width + x)] = value ? 1U : 0U;
    }

    [[nodiscard]] BinaryImage negative() const {
        BinaryImage result(width, height);
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            result.pixels[i] = pixels[i] == 0 ? 1U : 0U;
        }
        return result;
    }
};

struct BinaryComponent {
    BinaryImage image{};
    PointI offset{};
};

struct BoundingRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    [[nodiscard]] int width() const {
        return right - left;
    }

    [[nodiscard]] int height() const {
        return bottom - top;
    }

    [[nodiscard]] bool is_empty() const {
        return width() == 0 && height() == 0;
    }

    void add_x_y(const int x, const int y) {
        if (is_empty()) {
            left = x;
            right = x + 1;
            top = y;
            bottom = y + 1;
            return;
        }

        if (x < left) {
            left = x;
        } else if (x + 1 > right) {
            right = x + 1;
        }

        if (y < top) {
            top = y;
        } else if (y + 1 > bottom) {
            bottom = y + 1;
        }
    }

    void merge(const BoundingRect& other) {
        if (other.is_empty()) {
            return;
        }
        if (is_empty()) {
            *this = other;
            return;
        }

        left = std::min(left, other.left);
        top = std::min(top, other.top);
        right = std::max(right, other.right);
        bottom = std::max(bottom, other.bottom);
    }

    void clear() {
        left = 0;
        top = 0;
        right = 0;
        bottom = 0;
    }
};

struct ColorSum {
    std::uint64_t r = 0;
    std::uint64_t g = 0;
    std::uint64_t b = 0;
    std::uint64_t a = 0;
    std::uint64_t counter = 0;

    void add(const Color& color) {
        r += color.r;
        g += color.g;
        b += color.b;
        a += color.a;
        ++counter;
    }

    void merge(const ColorSum& other) {
        r += other.r;
        g += other.g;
        b += other.b;
        a += other.a;
        counter += other.counter;
    }

    [[nodiscard]] Color average() const {
        if (counter == 0) {
            return {};
        }

        return {
            static_cast<std::uint8_t>(r / counter),
            static_cast<std::uint8_t>(g / counter),
            static_cast<std::uint8_t>(b / counter),
            static_cast<std::uint8_t>(a / counter),
        };
    }

    void clear() {
        r = 0;
        g = 0;
        b = 0;
        a = 0;
        counter = 0;
    }
};

enum class KeyingAction {
    Keep,
    Discard,
};

struct ColorCluster {
    std::vector<int> indices;
    std::vector<int> holes;
    std::uint32_t num_holes = 0;
    std::uint32_t depth = 0;
    ColorSum sum{};
    ColorSum residue_sum{};
    BoundingRect rect{};
    std::uint32_t merged_into = 0;

    void add(const int pixel_index, const Color& color, const int x, const int y) {
        indices.push_back(pixel_index);
        sum.add(color);
        rect.add_x_y(x, y);
    }

    [[nodiscard]] std::size_t area() const {
        return indices.size();
    }

    [[nodiscard]] Color color() const {
        return sum.average();
    }

    [[nodiscard]] Color residue_color() const {
        return residue_sum.average();
    }
};

struct RunnerConfig {
    bool diagonal = false;
    std::uint32_t hierarchical = std::numeric_limits<std::uint32_t>::max();
    std::size_t good_min_area = 16;
    std::size_t good_max_area = 256U * 256U;
    int is_same_color_a = 4;
    int is_same_color_b = 1;
    int deepen_diff = 64;
    std::size_t hollow_neighbours = 1;
    Color key_color{};
    bool has_key_color = false;
    KeyingAction keying_action = KeyingAction::Keep;
};

struct ClusterRun {
    int width = 0;
    int height = 0;
    const std::vector<Color>* pixels = nullptr;
    std::vector<ColorCluster> clusters{1};
    std::vector<std::uint32_t> cluster_indices;
    std::vector<std::uint32_t> clusters_output;
    std::vector<std::uint32_t> neighbour_marks;
    std::uint32_t neighbour_stamp = 1;
};

[[nodiscard]] PointI operator+(const PointI& a, const PointI& b) {
    return {a.x + b.x, a.y + b.y};
}

[[nodiscard]] PointI operator-(const PointI& a, const PointI& b) {
    return {a.x - b.x, a.y - b.y};
}

[[nodiscard]] double DegToRad(const int degrees) {
    return static_cast<double>(degrees) * std::numbers::pi_v<double> / 180.0;
}

[[nodiscard]] std::string ToLower(std::string_view input) {
    std::string result(input);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

[[nodiscard]] int ClampInt(const int value, const int minimum, const int maximum) {
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] double ClampDouble(const double value, const double minimum, const double maximum) {
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] double Distance(const PointD& a, const PointD& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

[[nodiscard]] PointD operator+(const PointD& a, const PointD& b) {
    return {a.x + b.x, a.y + b.y};
}

[[nodiscard]] PointD operator-(const PointD& a, const PointD& b) {
    return {a.x - b.x, a.y - b.y};
}

[[nodiscard]] PointD operator*(const PointD& a, const double scalar) {
    return {a.x * scalar, a.y * scalar};
}

[[nodiscard]] PointD operator/(const PointD& a, const double scalar) {
    return {a.x / scalar, a.y / scalar};
}

[[nodiscard]] double Dot(const PointD& a, const PointD& b) {
    return a.x * b.x + a.y * b.y;
}

[[nodiscard]] double Cross(const PointD& a, const PointD& b) {
    return a.x * b.y - a.y * b.x;
}

[[nodiscard]] double PolygonSignedArea(const std::vector<PointD>& points) {
    if (points.size() < 3) {
        return 0.0;
    }

    double area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const PointD& current = points[i];
        const PointD& next = points[(i + 1) % points.size()];
        area += (current.x * next.y) - (next.x * current.y);
    }
    return area * 0.5;
}

[[nodiscard]] PointD Normalize(const PointD& point) {
    const double length = std::sqrt(point.x * point.x + point.y * point.y);
    if (length <= std::numeric_limits<double>::epsilon()) {
        return {};
    }
    return point / length;
}

[[nodiscard]] double SegmentDistance(const PointD& point, const PointD& a, const PointD& b) {
    const PointD ab = b - a;
    const double length_sq = Dot(ab, ab);
    if (length_sq <= std::numeric_limits<double>::epsilon()) {
        return Distance(point, a);
    }
    const double t = ClampDouble(Dot(point - a, ab) / length_sq, 0.0, 1.0);
    const PointD projection = a + (ab * t);
    return Distance(point, projection);
}

[[nodiscard]] bool IsCollinear(const PointD& a, const PointD& b, const PointD& c) {
    const PointD ab = b - a;
    const PointD bc = c - b;
    return std::abs(Cross(ab, bc)) < 1e-6;
}

[[nodiscard]] double InteriorAngle(const PointD& previous, const PointD& current, const PointD& next) {
    const PointD incoming = Normalize(previous - current);
    const PointD outgoing = Normalize(next - current);
    const double dot = ClampDouble(Dot(incoming, outgoing), -1.0, 1.0);
    return std::acos(dot);
}

[[nodiscard]] bool IsCorner(
    const PointD& previous,
    const PointD& current,
    const PointD& next,
    const double threshold_radians
) {
    if (Distance(previous, current) < 1e-6 || Distance(current, next) < 1e-6) {
        return false;
    }
    const double turn_angle = std::numbers::pi_v<double> - InteriorAngle(previous, current, next);
    return turn_angle >= threshold_radians;
}

[[nodiscard]] int QuantizeChannel(const std::uint8_t channel, const int step) {
    if (step <= 1) {
        return channel;
    }
    const int rounded = ((static_cast<int>(channel) + (step / 2)) / step) * step;
    return ClampInt(rounded, 0, 255);
}

[[nodiscard]] Color CompositeAlpha(const Color& color) {
    if (color.a == 255) {
        return color;
    }

    const auto alpha = static_cast<int>(color.a);
    const auto blend = [alpha](const std::uint8_t channel) -> std::uint8_t {
        const int value = (static_cast<int>(channel) * alpha + 255 * (255 - alpha)) / 255;
        return static_cast<std::uint8_t>(ClampInt(value, 0, 255));
    };

    return {blend(color.r), blend(color.g), blend(color.b), 255};
}

[[nodiscard]] ImageData LoadImageFile(const std::filesystem::path& input_path) {
    int width = 0;
    int height = 0;
    int components = 0;
    unsigned char* raw = stbi_load(input_path.string().c_str(), &width, &height, &components, 4);
    if (raw == nullptr) {
        throw std::runtime_error("Unable to load input image: " + input_path.string());
    }

    std::vector<Color> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = {
            raw[(i * 4) + 0],
            raw[(i * 4) + 1],
            raw[(i * 4) + 2],
            raw[(i * 4) + 3],
        };
    }
    stbi_image_free(raw);

    return {width, height, std::move(pixels)};
}

[[nodiscard]] RegionKey MakeRegionKey(const Color& pixel, const Config& config) {
    if (pixel.a == 0) {
        return {};
    }

    if (config.color_mode == ColorMode::Binary) {
        const bool foreground = pixel.r < 128;
        return {QuantizedColor{foreground ? 0 : 255, foreground ? 0 : 255, foreground ? 0 : 255}, foreground};
    }

    const Color composited = CompositeAlpha(pixel);
    const int precision_step = 1 << ClampInt(8 - config.color_precision, 0, 7);
    const int layer_step = std::max(1, config.layer_difference);
    const int effective_step = std::max(precision_step, layer_step);

    return {
        QuantizedColor{
            QuantizeChannel(composited.r, effective_step),
            QuantizeChannel(composited.g, effective_step),
            QuantizeChannel(composited.b, effective_step),
        },
        true,
    };
}

[[nodiscard]] std::vector<int> NeighborOffsets(const Config& config, const int width) {
    std::vector<int> offsets{-1, 1, -width, width};
    if (config.color_mode == ColorMode::Color && config.layer_difference == 0) {
        offsets.push_back(-width - 1);
        offsets.push_back(-width + 1);
        offsets.push_back(width - 1);
        offsets.push_back(width + 1);
    }
    return offsets;
}

[[nodiscard]] bool IsNeighborValid(
    const int current_index,
    const int neighbor_index,
    const int width,
    const int height
) {
    if (neighbor_index < 0 || neighbor_index >= width * height) {
        return false;
    }

    const int current_x = current_index % width;
    const int neighbor_x = neighbor_index % width;
    const int current_y = current_index / width;
    const int neighbor_y = neighbor_index / width;

    return std::abs(current_x - neighbor_x) <= 1 && std::abs(current_y - neighbor_y) <= 1;
}

[[nodiscard]] int ColorDiff(const Color& a, const Color& b) {
    return std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)) +
        std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)) +
        std::abs(static_cast<int>(a.b) - static_cast<int>(b.b));
}

[[nodiscard]] bool ColorSame(const Color& a, const Color& b, const int shift, const int threshold) {
    return std::abs((static_cast<int>(a.r) >> shift) - (static_cast<int>(b.r) >> shift)) <= threshold &&
        std::abs((static_cast<int>(a.g) >> shift) - (static_cast<int>(b.g) >> shift)) <= threshold &&
        std::abs((static_cast<int>(a.b) >> shift) - (static_cast<int>(b.b) >> shift)) <= threshold;
}

void CombineColorClusters(ClusterRun& run, const std::uint32_t from, const std::uint32_t to) {
    if (from == 0 || to == 0 || from == to || from >= run.clusters.size() || to >= run.clusters.size()) {
        return;
    }

    ColorCluster& from_cluster = run.clusters[from];
    ColorCluster& to_cluster = run.clusters[to];
    for (const int pixel_index : from_cluster.indices) {
        run.cluster_indices[static_cast<std::size_t>(pixel_index)] = to;
    }

    to_cluster.indices.insert(
        to_cluster.indices.end(),
        std::make_move_iterator(from_cluster.indices.begin()),
        std::make_move_iterator(from_cluster.indices.end()));
    from_cluster.indices.clear();

    to_cluster.sum.merge(from_cluster.sum);
    to_cluster.rect.merge(from_cluster.rect);
    from_cluster.sum.clear();
    from_cluster.rect.clear();
}

void CombineColorClustersClone(ClusterRun& run, const std::uint32_t from, const std::uint32_t to) {
    const ColorSum original_sum = run.clusters[from].sum;
    const BoundingRect original_rect = run.clusters[from].rect;
    const std::vector<int> original_indices = run.clusters[from].indices;
    CombineColorClusters(run, from, to);
    run.clusters[from].sum = original_sum;
    run.clusters[from].rect = original_rect;
    run.clusters[from].indices = original_indices;
}

[[nodiscard]] BinaryImage BuildClusterImage(const ColorCluster& cluster, const int parent_width, const bool include_holes) {
    BinaryImage image(cluster.rect.width(), cluster.rect.height());
    for (const int pixel_index : cluster.indices) {
        const int x = (pixel_index % parent_width) - cluster.rect.left;
        const int y = (pixel_index / parent_width) - cluster.rect.top;
        image.set(x, y, true);
    }

    if (include_holes) {
        for (const int pixel_index : cluster.holes) {
            const int x = (pixel_index % parent_width) - cluster.rect.left;
            const int y = (pixel_index / parent_width) - cluster.rect.top;
            if (x >= 0 && y >= 0 && x < image.width && y < image.height) {
                image.set(x, y, false);
            }
        }
    }

    return image;
}

[[nodiscard]] std::size_t ClusterPerimeter(const ColorCluster& cluster, const int parent_width) {
    const BinaryImage image = BuildClusterImage(cluster, parent_width, true);
    std::size_t boundary_pixels = 0;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (image.get(x, y) && (!image.get_safe(x - 1, y) ||
                    !image.get_safe(x + 1, y) ||
                    !image.get_safe(x, y - 1) ||
                    !image.get_safe(x, y + 1))) {
                ++boundary_pixels;
            }
        }
    }
    return boundary_pixels;
}

[[nodiscard]] bool PatchGood(
    const ClusterRun& run,
    const ColorCluster& cluster,
    const std::size_t good_min_area,
    const std::size_t good_max_area
) {
    return good_min_area < cluster.area() &&
        cluster.area() < good_max_area &&
        (good_min_area == 0 || ClusterPerimeter(cluster, run.width) < cluster.area());
}

[[nodiscard]] std::vector<std::uint32_t> ClusterNeighbours(
    ClusterRun& run,
    const ColorCluster& cluster,
    const std::uint32_t myself
) {
    if (run.neighbour_marks.size() < run.clusters.size()) {
        run.neighbour_marks.assign(run.clusters.size(), 0);
    }

    if (run.neighbour_stamp == std::numeric_limits<std::uint32_t>::max()) {
        std::fill(run.neighbour_marks.begin(), run.neighbour_marks.end(), 0);
        run.neighbour_stamp = 1;
    }

    const std::uint32_t mark = run.neighbour_stamp++;
    std::vector<std::uint32_t> neighbours;
    neighbours.reserve(8);
    static constexpr std::array<PointI, 4> offsets{{{0, -1}, {0, 1}, {-1, 0}, {1, 0}}};

    for (const int pixel_index : cluster.indices) {
        const int x = pixel_index % run.width;
        const int y = pixel_index / run.width;
        for (const PointI& offset : offsets) {
            const int nx = x + offset.x;
            const int ny = y + offset.y;
            if (nx < 0 || ny < 0 || nx >= run.width || ny >= run.height) {
                continue;
            }

            const std::uint32_t neighbour = run.cluster_indices[static_cast<std::size_t>(ny * run.width + nx)];
            if (neighbour == 0 || neighbour == myself) {
                continue;
            }
            if (run.neighbour_marks[neighbour] == mark) {
                continue;
            }

            run.neighbour_marks[neighbour] = mark;
            neighbours.push_back(neighbour);
        }
    }

    std::sort(neighbours.begin(), neighbours.end());
    return neighbours;
}

void MergeColorClusterInto(
    ClusterRun& run,
    const std::uint32_t from,
    const std::uint32_t to,
    const bool deepen,
    const bool hollow
) {
    if (!deepen) {
        run.clusters[to].residue_sum.merge(run.clusters[from].residue_sum);
        CombineColorClusters(run, from, to);
        return;
    }

    CombineColorClustersClone(run, from, to);
    if (hollow) {
        ColorCluster& target = run.clusters[to];
        target.holes.insert(
            target.holes.end(),
            run.clusters[from].indices.begin(),
            run.clusters[from].indices.end());
        ++target.num_holes;
    }

    run.clusters[from].merged_into = to;
    ++run.clusters[to].depth;
}

[[nodiscard]] std::map<std::size_t, std::size_t> BuildClusterAreaCounts(ClusterRun& run) {
    std::map<std::size_t, std::size_t> counts;
    for (ColorCluster& cluster : run.clusters) {
        if (cluster.area() > 0) {
            cluster.residue_sum = cluster.sum;
            ++counts[cluster.area()];
        }
    }
    return counts;
}

[[nodiscard]] bool HasMoreAreas(const std::map<std::size_t, std::size_t>& counts, std::map<std::size_t, std::size_t>::const_iterator current) {
    while (current != counts.end()) {
        if (current->second > 0) {
            return true;
        }
        ++current;
    }
    return false;
}

[[nodiscard]] ClusterRun RunColorCluster(
    const int width,
    const int height,
    const std::vector<Color>& pixels,
    const RunnerConfig& config
) {
    ClusterRun run;
    run.width = width;
    run.height = height;
    run.pixels = &pixels;
    run.clusters = std::vector<ColorCluster>(1);
    run.cluster_indices.assign(pixels.size(), 0);
    const std::vector<Color>& source_pixels = *run.pixels;

    const int shift = ClampInt(config.is_same_color_a, 0, 7);
    for (int index = 0; index < width * height; ++index) {
        const int x = index % width;
        const int y = index / width;
        const Color current = source_pixels[static_cast<std::size_t>(index)];
        const bool has_up = y > 0;
        const bool has_left = x > 0;
        const bool has_up_left = has_up && has_left;

        const Color up = has_up
            ? source_pixels[static_cast<std::size_t>(index - width)]
            : Color{};
        const Color left = has_left
            ? source_pixels[static_cast<std::size_t>(index - 1)]
            : Color{};
        const Color up_left = has_up_left
            ? source_pixels[static_cast<std::size_t>(index - width - 1)]
            : Color{};

        std::uint32_t cluster_up = y > 0 ? run.cluster_indices[static_cast<std::size_t>((y - 1) * width + x)] : 0;
        std::uint32_t cluster_left = x > 0 ? run.cluster_indices[static_cast<std::size_t>(y * width + (x - 1))] : 0;
        const std::uint32_t cluster_up_left = x > 0 && y > 0
            ? run.cluster_indices[static_cast<std::size_t>((y - 1) * width + (x - 1))]
            : 0;
        const bool same_left_up = has_left && has_up && ColorSame(left, up, shift, config.is_same_color_b);
        const bool same_current_left = has_left && ColorSame(current, left, shift, config.is_same_color_b);
        const bool same_current_up = has_up && ColorSame(current, up, shift, config.is_same_color_b);
        const bool same_current_up_left = has_up_left && ColorSame(current, up_left, shift, config.is_same_color_b);

        if (cluster_left != cluster_up &&
            same_left_up &&
            (config.diagonal ||
                (same_current_left && same_current_up))) {
            if (run.clusters[cluster_left].area() <= run.clusters[cluster_up].area()) {
                CombineColorClusters(run, cluster_left, cluster_up);
                cluster_left = cluster_up;
            } else {
                CombineColorClusters(run, cluster_up, cluster_left);
                cluster_up = cluster_left;
            }
        }

        if (config.has_key_color &&
            current.r == config.key_color.r &&
            current.g == config.key_color.g &&
            current.b == config.key_color.b) {
            if (config.keying_action == KeyingAction::Keep) {
                run.clusters[0].add(index, current, x, y);
            }
        } else if (same_current_up && same_current_up_left) {
            run.cluster_indices[static_cast<std::size_t>(index)] = cluster_up;
            run.clusters[cluster_up].add(index, current, x, y);
        } else if (same_current_left && same_current_up_left) {
            run.cluster_indices[static_cast<std::size_t>(index)] = cluster_left;
            run.clusters[cluster_left].add(index, current, x, y);
        } else if (config.diagonal && same_current_up_left) {
            run.cluster_indices[static_cast<std::size_t>(index)] = cluster_up_left;
            run.clusters[cluster_up_left].add(index, current, x, y);
        } else {
            const std::uint32_t new_index = static_cast<std::uint32_t>(run.clusters.size());
            run.clusters.emplace_back();
            run.cluster_indices[static_cast<std::size_t>(index)] = new_index;
            run.clusters.back().add(index, current, x, y);
        }
    }

    std::map<std::size_t, std::size_t> area_counts = BuildClusterAreaCounts(run);
    run.neighbour_marks.assign(run.clusters.size(), 0);
    run.neighbour_stamp = 1;
    for (auto area_it = area_counts.begin(); HasMoreAreas(area_counts, area_it); ++area_it) {
        if (area_it == area_counts.end()) {
            break;
        }
        if (area_it->second == 0) {
            continue;
        }

        const std::size_t current_area = area_it->first;
        const bool is_last_area = std::next(area_it) == area_counts.end();
        for (std::uint32_t cluster_index = 1; cluster_index < run.clusters.size(); ++cluster_index) {
            ColorCluster& cluster = run.clusters[cluster_index];
            if (cluster.area() != current_area) {
                continue;
            }

            if (current_area > config.hierarchical) {
                run.clusters_output.push_back(cluster_index);
                continue;
            }

            struct NeighbourInfo {
                std::uint32_t index = 0;
                int diff = 0;
            };

            std::vector<NeighbourInfo> infos;
            for (const std::uint32_t neighbour : ClusterNeighbours(run, cluster, cluster_index)) {
                infos.push_back({neighbour, ColorDiff(cluster.color(), run.clusters[neighbour].color())});
            }

            if (infos.empty()) {
                const bool can_discard_pixels = config.keying_action == KeyingAction::Discard && config.has_key_color;
                if (is_last_area || can_discard_pixels) {
                    run.clusters_output.push_back(cluster_index);
                }
                continue;
            }

            std::sort(infos.begin(), infos.end(), [](const NeighbourInfo& left, const NeighbourInfo& right) {
                return std::tie(left.diff, left.index) < std::tie(right.diff, right.index);
            });

            const std::uint32_t target = infos.front().index;
            const bool deepen = config.hierarchical == std::numeric_limits<std::uint32_t>::max() &&
                PatchGood(run, cluster, config.good_min_area, config.good_max_area) &&
                infos.front().diff > config.deepen_diff;
            const bool hollow = infos.size() <= config.hollow_neighbours;

            if (deepen) {
                run.clusters_output.push_back(cluster_index);
            }

            area_counts[run.clusters[target].area()] -= 1;
            MergeColorClusterInto(run, cluster_index, target, deepen, hollow);
            area_counts[run.clusters[target].area()] += 1;
        }
    }

    return run;
}

[[nodiscard]] bool ColorExistsInImage(const std::vector<Color>& pixels, const Color& color) {
    return std::any_of(pixels.begin(), pixels.end(), [&color](const Color& pixel) {
        return pixel.r == color.r && pixel.g == color.g && pixel.b == color.b;
    });
}

[[nodiscard]] Color FindUnusedColorInImage(const std::vector<Color>& pixels) {
    const std::array<Color, 6> candidates{{
        {255, 0, 0, 255},
        {0, 255, 0, 255},
        {0, 0, 255, 255},
        {255, 255, 0, 255},
        {0, 255, 255, 255},
        {255, 0, 255, 255},
    }};

    for (const Color& candidate : candidates) {
        if (!ColorExistsInImage(pixels, candidate)) {
            return candidate;
        }
    }

    for (int r = 0; r <= 255; r += 17) {
        for (int g = 0; g <= 255; g += 17) {
            for (int b = 0; b <= 255; b += 17) {
                const Color candidate{
                    static_cast<std::uint8_t>(r),
                    static_cast<std::uint8_t>(g),
                    static_cast<std::uint8_t>(b),
                    255,
                };
                if (!ColorExistsInImage(pixels, candidate)) {
                    return candidate;
                }
            }
        }
    }

    return {255, 0, 255, 255};
}

[[nodiscard]] bool ShouldKeyImage(const int width, const int height, const std::vector<Color>& pixels) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    const int threshold = static_cast<int>(static_cast<double>(width * 2) * 0.2);
    int transparent_pixels = 0;
    const std::array<int, 5> rows{{0, height / 4, height / 2, (3 * height) / 4, height - 1}};
    for (const int y : rows) {
        for (int x = 0; x < width; ++x) {
            if (pixels[static_cast<std::size_t>(y * width + x)].a == 0) {
                ++transparent_pixels;
            }
            if (transparent_pixels >= threshold) {
                return true;
            }
        }
    }

    return false;
}

[[nodiscard]] std::vector<Color> RenderClusterOutputImage(const ClusterRun& run) {
    std::vector<Color> rendered(static_cast<std::size_t>(run.width) * static_cast<std::size_t>(run.height));
    for (auto it = run.clusters_output.rbegin(); it != run.clusters_output.rend(); ++it) {
        const Color color = run.clusters[*it].residue_color();
        for (const int pixel_index : run.clusters[*it].indices) {
            rendered[static_cast<std::size_t>(pixel_index)] = color;
        }
    }
    return rendered;
}

[[nodiscard]] Region BuildRegionFromCluster(const ColorCluster& cluster) {
    Region region;
    region.fill_color = cluster.residue_color();
    region.pixels = cluster.indices;
    region.holes = cluster.holes;
    region.min_x = cluster.rect.left;
    region.min_y = cluster.rect.top;
    region.max_x = cluster.rect.right - 1;
    region.max_y = cluster.rect.bottom - 1;
    return region;
}

[[nodiscard]] std::vector<Region> ExtractColorRegionsPort(
    const int width,
    const int height,
    std::vector<Color> pixels,
    const Config& config,
    TraceStats& stats
) {
    RunnerConfig runner_config;
    runner_config.diagonal = config.layer_difference == 0;
    runner_config.hierarchical = std::numeric_limits<std::uint32_t>::max();
    runner_config.good_min_area = config.filter_speckle * config.filter_speckle;
    runner_config.good_max_area = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    runner_config.is_same_color_a = 8 - ClampInt(config.color_precision, 1, 8);
    runner_config.is_same_color_b = 1;
    runner_config.deepen_diff = config.layer_difference;
    runner_config.hollow_neighbours = 1;
    runner_config.keying_action = config.hierarchical == Hierarchical::Cutout ? KeyingAction::Keep : KeyingAction::Discard;

    if (ShouldKeyImage(width, height, pixels)) {
        runner_config.key_color = FindUnusedColorInImage(pixels);
        runner_config.has_key_color = true;
        for (Color& pixel : pixels) {
            if (pixel.a == 0) {
                pixel = runner_config.key_color;
            }
        }
    }

    ClusterRun run = RunColorCluster(width, height, pixels, runner_config);
    stats.total_regions = std::count_if(run.clusters.begin(), run.clusters.end(), [](const ColorCluster& cluster) {
        return cluster.area() > 0;
    });

    if (config.hierarchical == Hierarchical::Cutout) {
        RunnerConfig cutout_config;
        cutout_config.diagonal = false;
        cutout_config.hierarchical = 64;
        cutout_config.good_min_area = 0;
        cutout_config.good_max_area = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        cutout_config.is_same_color_a = 0;
        cutout_config.is_same_color_b = 1;
        cutout_config.deepen_diff = 0;
        cutout_config.hollow_neighbours = 0;
        cutout_config.key_color = runner_config.key_color;
        cutout_config.has_key_color = runner_config.has_key_color;
        cutout_config.keying_action = KeyingAction::Discard;
        run = RunColorCluster(width, height, RenderClusterOutputImage(run), cutout_config);
    }

    std::vector<Region> regions;
    regions.reserve(run.clusters_output.size());
    for (auto it = run.clusters_output.rbegin(); it != run.clusters_output.rend(); ++it) {
        if (run.clusters[*it].area() == 0) {
            continue;
        }
        regions.push_back(BuildRegionFromCluster(run.clusters[*it]));
    }

    stats.filtered_regions = stats.total_regions > regions.size() ? stats.total_regions - regions.size() : 0;
    return regions;
}

[[nodiscard]] std::vector<Region> ExtractRegions(
    const int width,
    const int height,
    const std::vector<Color>& pixels,
    const Config& config,
    TraceStats& stats
) {
    const std::size_t size = pixels.size();
    std::vector<RegionKey> keys(size);
    for (std::size_t i = 0; i < size; ++i) {
        keys[i] = MakeRegionKey(pixels[i], config);
    }

    std::vector<std::uint8_t> visited(size, 0);
    std::vector<Region> regions;
    const std::vector<int> offsets = NeighborOffsets(config, width);
    std::queue<int> queue;

    for (int index = 0; index < width * height; ++index) {
        if (visited[static_cast<std::size_t>(index)] != 0 || !keys[static_cast<std::size_t>(index)].active) {
            continue;
        }

        Region region;
        region.key = keys[static_cast<std::size_t>(index)].color;
        std::uint64_t sum_r = 0;
        std::uint64_t sum_g = 0;
        std::uint64_t sum_b = 0;

        visited[static_cast<std::size_t>(index)] = 1;
        queue.push(index);

        while (!queue.empty()) {
            const int current = queue.front();
            queue.pop();

            region.pixels.push_back(current);
            const int x = current % width;
            const int y = current / width;
            region.min_x = std::min(region.min_x, x);
            region.min_y = std::min(region.min_y, y);
            region.max_x = std::max(region.max_x, x);
            region.max_y = std::max(region.max_y, y);

            const Color composited = CompositeAlpha(pixels[static_cast<std::size_t>(current)]);
            sum_r += composited.r;
            sum_g += composited.g;
            sum_b += composited.b;

            for (const int offset : offsets) {
                const int neighbor = current + offset;
                if (!IsNeighborValid(current, neighbor, width, height)) {
                    continue;
                }
                if (visited[static_cast<std::size_t>(neighbor)] != 0) {
                    continue;
                }
                if (!keys[static_cast<std::size_t>(neighbor)].active) {
                    continue;
                }
                if (!(keys[static_cast<std::size_t>(neighbor)].color == region.key)) {
                    continue;
                }

                visited[static_cast<std::size_t>(neighbor)] = 1;
                queue.push(neighbor);
            }
        }

        const std::size_t area = region.area();
        region.fill_color = config.color_mode == ColorMode::Binary
            ? Color{0, 0, 0, 255}
            : Color{
                static_cast<std::uint8_t>(sum_r / std::max<std::size_t>(1, area)),
                static_cast<std::uint8_t>(sum_g / std::max<std::size_t>(1, area)),
                static_cast<std::uint8_t>(sum_b / std::max<std::size_t>(1, area)),
                255,
            };
        regions.push_back(std::move(region));
    }

    stats.total_regions = regions.size();
    return regions;
}

[[nodiscard]] BinaryImage BuildRegionMask(const Region& region, const int image_width) {
    const int local_width = (region.max_x - region.min_x) + 1;
    const int local_height = (region.max_y - region.min_y) + 1;
    BinaryImage mask(local_width, local_height);

    for (const int index : region.pixels) {
        const int global_x = index % image_width;
        const int global_y = index / image_width;
        mask.set(global_x - region.min_x, global_y - region.min_y, true);
    }

    for (const int index : region.holes) {
        const int global_x = index % image_width;
        const int global_y = index / image_width;
        const int local_x = global_x - region.min_x;
        const int local_y = global_y - region.min_y;
        if (local_x >= 0 && local_y >= 0 && local_x < local_width && local_y < local_height) {
            mask.set(local_x, local_y, false);
        }
    }

    return mask;
}

[[nodiscard]] std::vector<BinaryComponent> ExtractBinaryComponents(
    const BinaryImage& image,
    const bool target_value
) {
    const std::size_t pixel_count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    std::vector<std::uint8_t> visited(pixel_count, 0);
    std::vector<BinaryComponent> components;
    std::queue<int> queue;
    const std::array<PointI, 4> offsets{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const int start_index = y * image.width + x;
            if (visited[static_cast<std::size_t>(start_index)] != 0 || image.get(x, y) != target_value) {
                continue;
            }

            visited[static_cast<std::size_t>(start_index)] = 1;
            queue.push(start_index);

            std::vector<int> pixels;
            int min_x = x;
            int min_y = y;
            int max_x = x;
            int max_y = y;

            while (!queue.empty()) {
                const int current = queue.front();
                queue.pop();
                pixels.push_back(current);

                const int current_x = current % image.width;
                const int current_y = current / image.width;
                min_x = std::min(min_x, current_x);
                min_y = std::min(min_y, current_y);
                max_x = std::max(max_x, current_x);
                max_y = std::max(max_y, current_y);

                for (const PointI& offset : offsets) {
                    const int next_x = current_x + offset.x;
                    const int next_y = current_y + offset.y;
                    if (next_x < 0 || next_y < 0 || next_x >= image.width || next_y >= image.height) {
                        continue;
                    }

                    const int next_index = next_y * image.width + next_x;
                    if (visited[static_cast<std::size_t>(next_index)] != 0 || image.get(next_x, next_y) != target_value) {
                        continue;
                    }

                    visited[static_cast<std::size_t>(next_index)] = 1;
                    queue.push(next_index);
                }
            }

            BinaryImage component_image((max_x - min_x) + 1, (max_y - min_y) + 1);
            for (const int current : pixels) {
                const int current_x = current % image.width;
                const int current_y = current / image.width;
                component_image.set(current_x - min_x, current_y - min_y, true);
            }

            components.push_back({std::move(component_image), {min_x, min_y}});
        }
    }

    return components;
}

[[nodiscard]] bool TouchesImageBorder(const BinaryComponent& component, const BinaryImage& parent) {
    return component.offset.x == 0 ||
        component.offset.y == 0 ||
        component.offset.x + component.image.width == parent.width ||
        component.offset.y + component.image.height == parent.height;
}

void FillComponentIntoImage(BinaryImage& image, const BinaryComponent& component) {
    for (int y = 0; y < component.image.height; ++y) {
        for (int x = 0; x < component.image.width; ++x) {
            if (component.image.get(x, y)) {
                image.set(component.offset.x + x, component.offset.y + y, true);
            }
        }
    }
}

[[nodiscard]] bool IsBoundaryPixel(const BinaryImage& image, const int x, const int y) {
    return image.get(x, y) &&
        (!image.get_safe(x - 1, y) ||
            !image.get_safe(x + 1, y) ||
            !image.get_safe(x, y - 1) ||
            !image.get_safe(x, y + 1));
}

[[nodiscard]] std::optional<PointI> FindBoundaryStart(const BinaryImage& image) {
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (IsBoundaryPixel(image, x, y)) {
                return PointI{x, y};
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] PointI AheadOf(const PointI& current, const int dir) {
    switch (dir) {
        case 0:
            return {current.x, current.y - 1};
        case 2:
            return {current.x + 1, current.y};
        case 4:
            return {current.x, current.y + 1};
        case 6:
            return {current.x - 1, current.y};
        default:
            throw std::invalid_argument("Unsupported boundary direction.");
    }
}

[[nodiscard]] std::pair<PointI, PointI> SideVectors(const int dir) {
    switch (dir) {
        case 0:
            return {{-1, -1}, {0, -1}};
        case 2:
            return {{0, 0}, {0, -1}};
        case 4:
            return {{-1, 0}, {0, 0}};
        case 6:
            return {{-1, 0}, {-1, -1}};
        default:
            throw std::invalid_argument("Unsupported side vector direction.");
    }
}

[[nodiscard]] std::vector<PointI> WalkBoundaryPath(
    const BinaryImage& image,
    const PointI& start,
    const bool clockwise
) {
    std::vector<PointI> path;
    path.push_back(start);

    PointI current = start;
    PointI previous = start;
    PointI previous_previous = start;
    std::uint32_t length = 0;

    while (true) {
        if (current == start && length > 0) {
            break;
        }

        int direction = -1;
        while (true) {
            int next_direction = -1;
            const std::array<int, 4> search_order = clockwise
                ? std::array<int, 4>{0, 2, 4, 6}
                : std::array<int, 4>{6, 4, 2, 0};

            for (const int candidate : search_order) {
                if (AheadOf(current, candidate) == previous || AheadOf(current, candidate) == previous_previous) {
                    continue;
                }

                const auto [a, b] = SideVectors(candidate);
                if (image.get_safe(current.x + a.x, current.y + a.y) != image.get_safe(current.x + b.x, current.y + b.y)) {
                    next_direction = candidate;
                    break;
                }
            }

            if (next_direction == -1) {
                throw std::runtime_error("Boundary walker could not continue tracing the region.");
            }

            if (direction != -1 && direction != next_direction) {
                break;
            }

            direction = next_direction;
            previous_previous = previous;
            previous = current;
            current = AheadOf(current, next_direction);
            ++length;
        }

        if (length > 1'000'000U) {
            throw std::runtime_error("Boundary walker exceeded the maximum path length.");
        }

        path.push_back(current);
    }

    return path;
}

[[nodiscard]] int ManhattanDistance(const PointI& a, const PointI& b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

[[nodiscard]] int SignedArea(const PointI& a, const PointI& b, const PointI& c) {
    return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
}

[[nodiscard]] std::vector<PointI> RemoveStaircase(const std::vector<PointI>& path, const bool clockwise) {
    const std::size_t count = path.size();
    std::vector<PointI> result;
    if (count == 0) {
        return result;
    }

    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t next = (i + 1) % count;
        const std::size_t previous = i > 0 ? i - 1 : count - 1;

        bool keep = true;
        if (i != 0 && i != count - 1 &&
            (ManhattanDistance(path[i], path[previous]) == 1 || ManhattanDistance(path[i], path[next]) == 1)) {
            const int area = SignedArea(path[previous], path[i], path[next]);
            keep = area != 0 && ((area > 0) == clockwise);
        }

        if (keep) {
            result.push_back(path[i]);
        }
    }

    return result;
}

[[nodiscard]] double EvaluatePenalty(const PointI& a, const PointI& b, const PointI& c) {
    const auto sq = [](const int value) {
        return static_cast<double>(value * value);
    };

    const double l1 = std::sqrt(sq(a.x - b.x) + sq(a.y - b.y));
    const double l2 = std::sqrt(sq(b.x - c.x) + sq(b.y - c.y));
    const double l3 = std::sqrt(sq(c.x - a.x) + sq(c.y - a.y));
    if (l3 <= std::numeric_limits<double>::epsilon()) {
        return 0.0;
    }

    const double p = (l1 + l2 + l3) / 2.0;
    const double area = std::sqrt(std::max(0.0, p * (p - l1) * (p - l2) * (p - l3)));
    return (area * area) / l3;
}

[[nodiscard]] std::vector<PointI> LimitPenalties(const std::vector<PointI>& path) {
    constexpr double kTolerance = 1.0;
    const std::size_t count = path.size();
    std::vector<PointI> result;
    if (count == 0) {
        return result;
    }

    auto past_delta = [&path](const std::size_t from, const std::size_t to) {
        double max_penalty = 0.0;
        for (std::size_t i = from + 1; i < to; ++i) {
            max_penalty = std::max(max_penalty, EvaluatePenalty(path[from], path[i], path[to]));
        }
        return max_penalty;
    };

    result.reserve(count);
    std::size_t last = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (i == 0) {
            result.push_back(path[i]);
        } else if (i == last + 1) {
            continue;
        } else if (past_delta(last, i) >= kTolerance) {
            last = i - 1;
            result.push_back(path[i - 1]);
        }

        if (i == count - 1) {
            result.push_back(path[i]);
        }
    }

    return result;
}

[[nodiscard]] std::vector<PointI> SimplifyPolygonPath(const std::vector<PointI>& path, const bool clockwise) {
    return LimitPenalties(RemoveStaircase(path, clockwise));
}

[[nodiscard]] double AngleOfUnitVector(const PointD& point) {
    return point.y < 0.0 ? -std::acos(point.x) : std::acos(point.x);
}

[[nodiscard]] double SignedAngleDifference(const double from, const double to) {
    double adjusted_to = to;
    if (from > adjusted_to) {
        adjusted_to += 2.0 * std::numbers::pi_v<double>;
    }

    const double diff = adjusted_to - from;
    return diff > std::numbers::pi_v<double> ? diff - (2.0 * std::numbers::pi_v<double>) : diff;
}

[[nodiscard]] std::vector<PointD> ToPointDPath(const std::vector<PointI>& path) {
    std::vector<PointD> result;
    result.reserve(path.size());
    for (const PointI& point : path) {
        result.push_back({static_cast<double>(point.x), static_cast<double>(point.y)});
    }
    return result;
}

[[nodiscard]] std::vector<bool> FindCorners(const std::vector<PointD>& path, const double threshold_radians) {
    if (path.size() < 2) {
        return {};
    }

    const std::size_t count = path.size() - 1;
    std::vector<bool> corners(count, false);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t previous = i == 0 ? count - 1 : i - 1;
        const std::size_t next = (i + 1) % count;

        const PointD incoming = Normalize(path[i] - path[previous]);
        const PointD outgoing = Normalize(path[next] - path[i]);
        const double angle_in = AngleOfUnitVector(incoming);
        const double angle_out = AngleOfUnitVector(outgoing);
        const double angle_diff = std::abs(SignedAngleDifference(angle_in, angle_out));
        if (angle_diff >= threshold_radians) {
            corners[i] = true;
        }
    }

    return corners;
}

[[nodiscard]] PointD MidPoint(const PointD& a, const PointD& b) {
    return {(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
}

[[nodiscard]] PointD FindSubdivisionPoint(
    const PointD& current,
    const PointD& next,
    const PointD& previous_anchor,
    const PointD& next_anchor,
    const double outset_ratio
) {
    const PointD outer_mid = MidPoint(current, next);
    const PointD inner_mid = MidPoint(previous_anchor, next_anchor);
    const PointD vector = outer_mid - inner_mid;
    const double magnitude = Distance(outer_mid, inner_mid) / outset_ratio;
    if (magnitude < std::numeric_limits<double>::epsilon()) {
        return outer_mid;
    }

    return outer_mid + (Normalize(vector) * magnitude);
}

struct SmoothSubdivision {
    std::vector<PointD> path;
    std::vector<bool> corners;
    bool can_terminate = true;
};

[[nodiscard]] SmoothSubdivision SubdivideKeepCorners(
    const std::vector<PointD>& path,
    const std::vector<bool>& corners,
    const double outset_ratio,
    const double segment_length
) {
    const std::size_t count = path.size() - 1;
    SmoothSubdivision result;
    result.can_terminate = true;

    for (std::size_t i = 0; i < count; ++i) {
        result.path.push_back(path[i]);
        result.corners.push_back(corners[i]);

        const std::size_t next = (i + 1) % count;
        const double current_length = Distance(path[i], path[next]);
        if (current_length <= segment_length) {
            continue;
        }

        std::size_t previous = i == 0 ? count - 1 : i - 1;
        std::size_t next_anchor = (next + 1) % count;

        const double previous_length = Distance(path[previous], path[i]);
        const double next_length = Distance(path[next_anchor], path[next]);
        if (previous_length / current_length >= 2.0 || next_length / current_length >= 2.0) {
            continue;
        }

        if (corners[i]) {
            previous = i;
        }
        if (corners[next]) {
            next_anchor = next;
        }
        if (previous == i && next_anchor == next) {
            continue;
        }

        const PointD new_point = FindSubdivisionPoint(path[i], path[next], path[previous], path[next_anchor], outset_ratio);
        result.path.push_back(new_point);
        result.corners.push_back(false);

        if (Distance(path[i], new_point) > segment_length || Distance(path[next], new_point) > segment_length) {
            result.can_terminate = false;
        }
    }

    if (!result.path.empty()) {
        result.path.push_back(result.path.front());
    }

    return result;
}

[[nodiscard]] std::vector<PointD> SmoothClosedPath(const std::vector<PointI>& path, const Config& config) {
    constexpr double kOutsetRatio = 8.0;

    if (path.size() <= 4) {
        return ToPointDPath(path);
    }

    std::vector<PointD> smoothed = ToPointDPath(path);
    std::vector<bool> corners = FindCorners(smoothed, DegToRad(config.corner_threshold));
    for (std::size_t iteration = 0; iteration < std::max<std::size_t>(1, config.max_iterations); ++iteration) {
        const SmoothSubdivision subdivision =
            SubdivideKeepCorners(smoothed, corners, kOutsetRatio, config.length_threshold);
        smoothed = subdivision.path;
        corners = subdivision.corners;
        if (subdivision.can_terminate) {
            break;
        }
    }

    return smoothed;
}

[[nodiscard]] bool NearlyEqual(const double left, const double right, const double tolerance = 1.0) {
    return std::abs(left - right) <= tolerance;
}

void ClampPointsToCanvas(std::vector<PointD>& path, const int canvas_width, const int canvas_height) {
    for (PointD& point : path) {
        point.x = ClampDouble(point.x, 0.0, static_cast<double>(canvas_width));
        point.y = ClampDouble(point.y, 0.0, static_cast<double>(canvas_height));

        if (NearlyEqual(point.x, 0.0)) {
            point.x = 0.0;
        } else if (NearlyEqual(point.x, static_cast<double>(canvas_width))) {
            point.x = static_cast<double>(canvas_width);
        }

        if (NearlyEqual(point.y, 0.0)) {
            point.y = 0.0;
        } else if (NearlyEqual(point.y, static_cast<double>(canvas_height))) {
            point.y = static_cast<double>(canvas_height);
        }
    }
}

void OffsetPath(std::vector<PointD>& path, const PointI& offset) {
    for (PointD& point : path) {
        point.x += static_cast<double>(offset.x);
        point.y += static_cast<double>(offset.y);
    }
}

[[nodiscard]] std::vector<Loop> BuildLoopsForBinaryImage(
    const BinaryImage& image,
    const PointI& image_offset,
    const int canvas_width,
    const int canvas_height,
    const Config& config
) {
    BinaryImage outer = image;
    std::vector<std::pair<BinaryImage, PointI>> boundaries;

    std::vector<BinaryComponent> holes = ExtractBinaryComponents(image.negative(), true);
    for (const BinaryComponent& hole : holes) {
        if (TouchesImageBorder(hole, image)) {
            continue;
        }
        FillComponentIntoImage(outer, hole);
    }

    boundaries.emplace_back(std::move(outer), image_offset);
    for (BinaryComponent& hole : holes) {
        if (!TouchesImageBorder(hole, image)) {
            boundaries.emplace_back(std::move(hole.image), image_offset + hole.offset);
        }
    }

    std::vector<Loop> loops;
    for (std::size_t index = 0; index < boundaries.size(); ++index) {
        const bool clockwise = index == 0;
        auto& [boundary_image, boundary_offset] = boundaries[index];
        const std::optional<PointI> start = FindBoundaryStart(boundary_image);
        if (!start.has_value()) {
            continue;
        }

        std::vector<PointI> path = WalkBoundaryPath(boundary_image, *start, clockwise);
        if (config.mode != PathMode::Pixel) {
            path = SimplifyPolygonPath(path, clockwise);
        }
        if (path.size() < 4) {
            continue;
        }

        std::vector<PointD> points = config.mode == PathMode::Spline ? SmoothClosedPath(path, config) : ToPointDPath(path);
        if (points.size() > 1 && Distance(points.front(), points.back()) < 1e-6) {
            points.pop_back();
        }
        if (points.size() < 3) {
            continue;
        }

        OffsetPath(points, boundary_offset);
        ClampPointsToCanvas(points, canvas_width, canvas_height);
        loops.push_back({points, PolygonSignedArea(points)});
    }

    return loops;
}

void AppendFormattedNumber(std::string& output, const double value, const std::optional<unsigned int> precision) {
    char buffer[64];

    const auto result = precision.has_value()
        ? std::to_chars(
            buffer,
            buffer + std::size(buffer),
            value,
            std::chars_format::fixed,
            static_cast<int>(*precision))
        : std::to_chars(buffer, buffer + std::size(buffer), value, std::chars_format::general);

    if (result.ec == std::errc{}) {
        output.append(buffer, result.ptr);
        return;
    }

    std::ostringstream stream;
    if (precision.has_value()) {
        stream << std::fixed << std::setprecision(static_cast<int>(*precision));
    }
    stream << value;
    output += stream.str();
}

void AppendCommand(
    std::string& output,
    std::string_view command,
    const PointD& point,
    const std::optional<unsigned int> precision
) {
    if (!output.empty()) {
        output.push_back(' ');
    }
    output += command;
    output.push_back(' ');
    AppendFormattedNumber(output, point.x, precision);
    output.push_back(' ');
    AppendFormattedNumber(output, point.y, precision);
}

[[nodiscard]] PointD LimitHandle(const PointD& anchor, const PointD& handle, const double max_distance) {
    const double distance = Distance(anchor, handle);
    if (distance <= max_distance || distance <= 1e-6) {
        return handle;
    }
    const PointD direction = Normalize(handle - anchor);
    return anchor + (direction * max_distance);
}

[[nodiscard]] PointD ClampPointToCanvas(const PointD& point, const int canvas_width, const int canvas_height) {
    return {
        ClampDouble(point.x, 0.0, static_cast<double>(canvas_width)),
        ClampDouble(point.y, 0.0, static_cast<double>(canvas_height)),
    };
}

void ConstrainBorderHandles(
    const PointD& p1,
    PointD& c1,
    PointD& c2,
    const PointD& p2,
    const int canvas_width,
    const int canvas_height
) {
    const auto border_x = [&](const PointD& a, const PointD& b, const double value) {
        return NearlyEqual(a.x, value, 1e-6) && NearlyEqual(b.x, value, 1e-6);
    };
    const auto border_y = [&](const PointD& a, const PointD& b, const double value) {
        return NearlyEqual(a.y, value, 1e-6) && NearlyEqual(b.y, value, 1e-6);
    };

    if (border_x(p1, p2, 0.0)) {
        c1.x = 0.0;
        c2.x = 0.0;
    } else if (border_x(p1, p2, static_cast<double>(canvas_width))) {
        c1.x = static_cast<double>(canvas_width);
        c2.x = static_cast<double>(canvas_width);
    }

    if (border_y(p1, p2, 0.0)) {
        c1.y = 0.0;
        c2.y = 0.0;
    } else if (border_y(p1, p2, static_cast<double>(canvas_height))) {
        c1.y = static_cast<double>(canvas_height);
        c2.y = static_cast<double>(canvas_height);
    }
}

[[nodiscard]] std::string BuildLoopPathData(
    const Loop& loop,
    const int canvas_width,
    const int canvas_height,
    const Config& config
) {
    if (loop.points.empty()) {
        return {};
    }

    std::string path;
    path.reserve(loop.points.size() * (config.mode == PathMode::Spline ? 64U : 24U));
    AppendCommand(path, "M", loop.points.front(), config.path_precision);

    if (config.mode == PathMode::Spline && loop.points.size() >= 4) {
        const double corner_threshold = DegToRad(config.corner_threshold);
        const double splice_threshold = DegToRad(config.splice_threshold);
        const double base_handle = std::max(1.0, config.length_threshold * 0.9);

        for (std::size_t i = 0; i < loop.points.size(); ++i) {
            const PointD& p0 = loop.points[(i + loop.points.size() - 1) % loop.points.size()];
            const PointD& p1 = loop.points[i];
            const PointD& p2 = loop.points[(i + 1) % loop.points.size()];
            const PointD& p3 = loop.points[(i + 2) % loop.points.size()];

            const bool splice_start = IsCorner(p0, p1, p2, splice_threshold);
            const bool splice_end = IsCorner(p1, p2, p3, splice_threshold);
            const bool corner_start = IsCorner(p0, p1, p2, corner_threshold);
            const bool corner_end = IsCorner(p1, p2, p3, corner_threshold);

            const double factor = 1.0 / 6.0;
            PointD c1 = p1 + ((p2 - p0) * factor);
            PointD c2 = p2 - ((p3 - p1) * factor);

            if (corner_start || splice_start) {
                c1 = p1;
            }
            if (corner_end || splice_end) {
                c2 = p2;
            }

            c1 = LimitHandle(p1, c1, base_handle);
            c2 = LimitHandle(p2, c2, base_handle);
            c1 = ClampPointToCanvas(c1, canvas_width, canvas_height);
            c2 = ClampPointToCanvas(c2, canvas_width, canvas_height);
            ConstrainBorderHandles(p1, c1, c2, p2, canvas_width, canvas_height);

            path += " C ";
            AppendFormattedNumber(path, c1.x, config.path_precision);
            path.push_back(' ');
            AppendFormattedNumber(path, c1.y, config.path_precision);
            path.push_back(' ');
            AppendFormattedNumber(path, c2.x, config.path_precision);
            path.push_back(' ');
            AppendFormattedNumber(path, c2.y, config.path_precision);
            path.push_back(' ');
            AppendFormattedNumber(path, p2.x, config.path_precision);
            path.push_back(' ');
            AppendFormattedNumber(path, p2.y, config.path_precision);
        }
    } else {
        for (std::size_t i = 1; i < loop.points.size(); ++i) {
            AppendCommand(path, "L", loop.points[i], config.path_precision);
        }
    }

    path += " Z";
    return path;
}

[[nodiscard]] std::string BuildPathDataForRegion(
    const Region& region,
    const int image_width,
    const int image_height,
    const Config& config
) {
    const BinaryImage mask = BuildRegionMask(region, image_width);
    const std::vector<BinaryComponent> components = ExtractBinaryComponents(mask, true);

    std::vector<Loop> loops;
    for (const BinaryComponent& component : components) {
        const PointI component_offset{
            region.min_x + component.offset.x,
            region.min_y + component.offset.y,
        };
        std::vector<Loop> component_loops =
            BuildLoopsForBinaryImage(component.image, component_offset, image_width, image_height, config);
        loops.insert(
            loops.end(),
            std::make_move_iterator(component_loops.begin()),
            std::make_move_iterator(component_loops.end()));
    }

    std::string result;
    for (const Loop& loop : loops) {
        if (loop.points.size() < 3) {
            continue;
        }

        const std::string loop_path = BuildLoopPathData(loop, image_width, image_height, config);
        if (!loop_path.empty()) {
            if (!result.empty()) {
                result.push_back(' ');
            }
            result += loop_path;
        }
    }

    return result;
}

[[nodiscard]] std::string BuildPathData(std::vector<Loop> loops, const Config& config) {
    if (loops.empty()) {
        return {};
    }

    std::string result;
    for (const Loop& loop : loops) {
        if (loop.points.size() < 3) {
            continue;
        }
        const std::string loop_path = BuildLoopPathData(loop, std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), config);
        if (!loop_path.empty()) {
            if (!result.empty()) {
                result.push_back(' ');
            }
            result += loop_path;
        }
    }
    return result;
}

[[nodiscard]] std::string BuildSvgDocument(
    const int width,
    const int height,
    const std::vector<SvgPath>& paths
) {
    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<!-- Generator: vtracer-cpp -->\n";
    svg << "<svg version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\" width=\""
        << width
        << "\" height=\""
        << height
        << "\" viewBox=\"0 0 "
        << width
        << ' '
        << height
        << "\">\n";

    for (const SvgPath& path : paths) {
        svg << "  <path d=\""
            << path.data
            << "\" fill=\""
            << ToHexColor(path.color)
            << "\"/>\n";
    }
    svg << "</svg>\n";
    return svg.str();
}

}  // namespace

Config Config::FromPreset(const std::string_view preset_name) {
    const std::string preset = ToLower(preset_name);
    if (preset == "bw") {
        Config config;
        config.color_mode = ColorMode::Binary;
        return config;
    }
    if (preset == "poster") {
        Config config;
        config.color_precision = 8;
        return config;
    }
    if (preset == "photo") {
        Config config;
        config.filter_speckle = 10;
        config.color_precision = 8;
        config.layer_difference = 48;
        config.corner_threshold = 180;
        return config;
    }

    throw std::invalid_argument("Unknown preset: " + std::string(preset_name));
}

TraceResult TraceImageFile(const std::filesystem::path& input_path, const Config& config) {
    const ImageData image = LoadImageFile(input_path);
    return TraceRgbaImage(image.width, image.height, image.pixels, config);
}

TraceResult TraceRgbaImage(
    const int width,
    const int height,
    const std::vector<Color>& pixels,
    const Config& config
) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Input image dimensions must be positive.");
    }
    if (static_cast<std::size_t>(width) * static_cast<std::size_t>(height) != pixels.size()) {
        throw std::invalid_argument("Input image size does not match width * height.");
    }

    TraceResult result;
    result.width = width;
    result.height = height;

    std::vector<Region> regions = config.color_mode == ColorMode::Color
        ? ExtractColorRegionsPort(width, height, pixels, config, result.stats)
        : ExtractRegions(width, height, pixels, config, result.stats);

    if (config.color_mode == ColorMode::Binary) {
        std::sort(regions.begin(), regions.end(), [](const Region& lhs, const Region& rhs) {
            return lhs.area() > rhs.area();
        });
    }

    const std::size_t minimum_area = config.filter_speckle * config.filter_speckle;
    for (const Region& region : regions) {
        if (config.color_mode == ColorMode::Binary && region.area() < minimum_area) {
            ++result.stats.filtered_regions;
            continue;
        }

        const std::string data = BuildPathDataForRegion(region, width, height, config);
        if (data.empty()) {
            ++result.stats.filtered_regions;
            continue;
        }

        result.paths.push_back({data, region.fill_color});
    }

    result.stats.traced_regions = result.paths.size();
    result.stats.output_paths = result.paths.size();
    result.svg = BuildSvgDocument(result.width, result.height, result.paths);
    return result;
}

void WriteSvgFile(const TraceResult& result, const std::filesystem::path& output_path) {
    std::ofstream output(output_path, std::ios::binary);
    if (!output.is_open()) {
        throw std::runtime_error("Unable to create output SVG: " + output_path.string());
    }
    output << result.svg;
}

std::string ToHexColor(const Color& color) {
    std::ostringstream stream;
    stream << '#'
           << std::uppercase
           << std::hex
           << std::setw(2)
           << std::setfill('0')
           << static_cast<int>(color.r)
           << std::setw(2)
           << static_cast<int>(color.g)
           << std::setw(2)
           << static_cast<int>(color.b);
    return stream.str();
}

}  // namespace vtracer
