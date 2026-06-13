#include <delaunator.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kEps = 1e-9;

struct Vec2 {
    double x{};
    double y{};
};

Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }

double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
double length(Vec2 a) { return std::sqrt(dot(a, a)); }
bool near(Vec2 a, Vec2 b, double eps = kEps) { return length(a - b) <= eps; }

struct Matrix {
    double a{1}, b{0}, c{0}, d{1}, e{0}, f{0};

    Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
};

Matrix operator*(const Matrix& lhs, const Matrix& rhs) {
    return {
        lhs.a * rhs.a + lhs.c * rhs.b,
        lhs.b * rhs.a + lhs.d * rhs.b,
        lhs.a * rhs.c + lhs.c * rhs.d,
        lhs.b * rhs.c + lhs.d * rhs.d,
        lhs.a * rhs.e + lhs.c * rhs.f + lhs.e,
        lhs.b * rhs.e + lhs.d * rhs.f + lhs.f,
    };
}

struct Color {
    double r{}, g{}, b{};
};

enum class FillRule { NonZero, EvenOdd };

struct Contour {
    std::vector<Vec2> points;
};

struct PathShape {
    std::string name;
    std::vector<Contour> contours;
    Color color;
    FillRule fillRule{FillRule::NonZero};
};

struct ParseOptions {
    int curveSegments{12};
    std::optional<double> curveTolerance;
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<double> number(std::string_view text) {
    std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str()) return std::nullopt;
    return value;
}

std::unordered_map<std::string, std::string> parseAttributes(std::string_view tag) {
    std::unordered_map<std::string, std::string> attrs;
    std::size_t i = 0;
    while (i < tag.size() && tag[i] != '<') ++i;
    while (i < tag.size() && !std::isspace(static_cast<unsigned char>(tag[i])) && tag[i] != '>') ++i;
    while (i < tag.size()) {
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
        if (i >= tag.size() || tag[i] == '>' || tag[i] == '/') break;
        const std::size_t keyStart = i;
        while (i < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[i])) || tag[i] == '-' || tag[i] == ':' || tag[i] == '_')) ++i;
        if (i == keyStart) { ++i; continue; }
        std::string key = lower(std::string(tag.substr(keyStart, i - keyStart)));
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
        if (i >= tag.size() || tag[i] != '=') continue;
        ++i;
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
        if (i >= tag.size()) break;
        std::string value;
        if (tag[i] == '\'' || tag[i] == '"') {
            const char quote = tag[i++];
            const std::size_t start = i;
            while (i < tag.size() && tag[i] != quote) ++i;
            value = std::string(tag.substr(start, i - start));
            if (i < tag.size()) ++i;
        } else {
            const std::size_t start = i;
            while (i < tag.size() && !std::isspace(static_cast<unsigned char>(tag[i])) && tag[i] != '>') ++i;
            value = std::string(tag.substr(start, i - start));
        }
        attrs[std::move(key)] = std::move(value);
    }
    if (const auto it = attrs.find("style"); it != attrs.end()) {
        std::stringstream style(it->second);
        std::string item;
        while (std::getline(style, item, ';')) {
            const auto colon = item.find(':');
            if (colon != std::string::npos) attrs[lower(trim(item.substr(0, colon)))] = trim(item.substr(colon + 1));
        }
    }
    return attrs;
}

std::vector<double> parseNumberList(std::string_view text) {
    std::vector<double> out;
    const char* p = text.data();
    const char* end = p + text.size();
    while (p < end) {
        while (p < end && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) ++p;
        if (p >= end) break;
        char* next = nullptr;
        const double value = std::strtod(p, &next);
        if (next == p) { ++p; continue; }
        out.push_back(value);
        p = next;
    }
    return out;
}

Matrix parseTransform(std::string_view text) {
    Matrix result;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (std::isspace(static_cast<unsigned char>(text[i])) || text[i] == ',')) ++i;
        const std::size_t nameStart = i;
        while (i < text.size() && std::isalpha(static_cast<unsigned char>(text[i]))) ++i;
        if (nameStart == i) { ++i; continue; }
        const std::string name = lower(std::string(text.substr(nameStart, i - nameStart)));
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i >= text.size() || text[i] != '(') continue;
        const std::size_t start = ++i;
        while (i < text.size() && text[i] != ')') ++i;
        const auto values = parseNumberList(text.substr(start, i - start));
        if (i < text.size()) ++i;
        Matrix m;
        if (name == "matrix" && values.size() >= 6) {
            m = {values[0], values[1], values[2], values[3], values[4], values[5]};
        } else if (name == "translate" && !values.empty()) {
            m.e = values[0]; m.f = values.size() > 1 ? values[1] : 0;
        } else if (name == "scale" && !values.empty()) {
            m.a = values[0]; m.d = values.size() > 1 ? values[1] : values[0];
        } else if (name == "rotate" && !values.empty()) {
            const double angle = values[0] * kPi / 180.0;
            Matrix rotation{std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle), 0, 0};
            if (values.size() >= 3) {
                Matrix to; to.e = values[1]; to.f = values[2];
                Matrix from; from.e = -values[1]; from.f = -values[2];
                m = to * rotation * from;
            } else m = rotation;
        } else if (name == "skewx" && !values.empty()) {
            m.c = std::tan(values[0] * kPi / 180.0);
        } else if (name == "skewy" && !values.empty()) {
            m.b = std::tan(values[0] * kPi / 180.0);
        }
        result = result * m;
    }
    return result;
}

std::optional<Color> parseColor(std::string value) {
    value = lower(trim(value));
    if (value.empty() || value == "none" || value == "transparent") return std::nullopt;
    if (value[0] == '#') {
        std::string hex = value.substr(1);
        if (hex.size() == 3 || hex.size() == 4) {
            std::string expanded;
            for (char c : hex.substr(0, 3)) { expanded.push_back(c); expanded.push_back(c); }
            hex = expanded;
        }
        if (hex.size() < 6) return std::nullopt;
        try {
            return Color{
                static_cast<double>(std::stoi(hex.substr(0, 2), nullptr, 16)) / 255.0,
                static_cast<double>(std::stoi(hex.substr(2, 2), nullptr, 16)) / 255.0,
                static_cast<double>(std::stoi(hex.substr(4, 2), nullptr, 16)) / 255.0,
            };
        } catch (...) { return std::nullopt; }
    }
    if (value.starts_with("rgb(")) {
        const auto close = value.find(')');
        if (close == std::string::npos) return std::nullopt;
        std::string body = value.substr(4, close - 4);
        std::replace(body.begin(), body.end(), ',', ' ');
        std::stringstream stream(body);
        std::array<std::string, 3> parts;
        if (!(stream >> parts[0] >> parts[1] >> parts[2])) return std::nullopt;
        Color c;
        double* channels[] = {&c.r, &c.g, &c.b};
        for (std::size_t i = 0; i < 3; ++i) {
            const bool percent = parts[i].ends_with('%');
            if (percent) parts[i].pop_back();
            const auto v = number(parts[i]);
            if (!v) return std::nullopt;
            *channels[i] = std::clamp(*v / (percent ? 100.0 : 255.0), 0.0, 1.0);
        }
        return c;
    }
    static const std::map<std::string, Color> named = {
        {"black", {0,0,0}}, {"white", {1,1,1}}, {"red", {1,0,0}},
        {"green", {0,0.5019608,0}}, {"blue", {0,0,1}}, {"yellow", {1,1,0}},
        {"gray", {0.5019608,0.5019608,0.5019608}}, {"grey", {0.5019608,0.5019608,0.5019608}},
        {"orange", {1,0.6470588,0}}, {"purple", {0.5019608,0,0.5019608}},
        {"pink", {1,0.7529412,0.7960784}}, {"cyan", {0,1,1}}, {"magenta", {1,0,1}},
    };
    if (const auto it = named.find(value); it != named.end()) return it->second;
    return std::nullopt;
}

double pointLineDistance(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 d = b - a;
    const double len = length(d);
    return len <= kEps ? length(p - a) : std::abs(cross(d, p - a)) / len;
}

void flattenCubic(std::vector<Vec2>& out, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, double tolerance, int depth = 0) {
    if (depth >= 18 || std::max(pointLineDistance(p1, p0, p3), pointLineDistance(p2, p0, p3)) <= tolerance) {
        out.push_back(p3); return;
    }
    const Vec2 p01 = (p0 + p1) * 0.5, p12 = (p1 + p2) * 0.5, p23 = (p2 + p3) * 0.5;
    const Vec2 p012 = (p01 + p12) * 0.5, p123 = (p12 + p23) * 0.5;
    const Vec2 p = (p012 + p123) * 0.5;
    flattenCubic(out, p0, p01, p012, p, tolerance, depth + 1);
    flattenCubic(out, p, p123, p23, p3, tolerance, depth + 1);
}

void flattenQuadratic(std::vector<Vec2>& out, Vec2 p0, Vec2 p1, Vec2 p2, double tolerance, int depth = 0) {
    if (depth >= 18 || pointLineDistance(p1, p0, p2) <= tolerance) { out.push_back(p2); return; }
    const Vec2 p01 = (p0 + p1) * 0.5, p12 = (p1 + p2) * 0.5, p = (p01 + p12) * 0.5;
    flattenQuadratic(out, p0, p01, p, tolerance, depth + 1);
    flattenQuadratic(out, p, p12, p2, tolerance, depth + 1);
}

class PathDataParser {
public:
    PathDataParser(std::string_view data, ParseOptions options) : data_(data), options_(options) {}

    std::vector<Contour> parse() {
        char command = 0;
        while (true) {
            skipSeparators();
            if (pos_ >= data_.size()) break;
            if (std::isalpha(static_cast<unsigned char>(data_[pos_]))) command = data_[pos_++];
            else if (!command) throw std::runtime_error("Path data begins without a command");
            execute(command);
        }
        finishContour();
        return contours_;
    }

private:
    std::string_view data_;
    ParseOptions options_;
    std::size_t pos_{};
    Vec2 current_{}, start_{}, lastCubicControl_{}, lastQuadraticControl_{};
    bool hasCurrent_{}, previousCubic_{}, previousQuadratic_{};
    Contour contour_;
    std::vector<Contour> contours_;

    void skipSeparators() {
        while (pos_ < data_.size() && (std::isspace(static_cast<unsigned char>(data_[pos_])) || data_[pos_] == ',')) ++pos_;
    }
    bool hasNumber() {
        skipSeparators();
        if (pos_ >= data_.size()) return false;
        const char c = data_[pos_];
        return c == '+' || c == '-' || c == '.' || std::isdigit(static_cast<unsigned char>(c));
    }
    double nextNumber() {
        skipSeparators();
        if (pos_ >= data_.size()) throw std::runtime_error("Missing path coordinate");
        const char* begin = data_.data() + pos_;
        char* end = nullptr;
        const double value = std::strtod(begin, &end);
        if (end == begin) throw std::runtime_error("Invalid path coordinate near offset " + std::to_string(pos_));
        pos_ += static_cast<std::size_t>(end - begin);
        return value;
    }
    Vec2 nextPoint(bool relative) {
        Vec2 p{nextNumber(), nextNumber()};
        if (relative) p = p + current_;
        return p;
    }
    void lineTo(Vec2 p) {
        if (contour_.points.empty()) contour_.points.push_back(current_);
        if (!near(contour_.points.back(), p)) contour_.points.push_back(p);
        current_ = p; hasCurrent_ = true;
    }
    void beginContour(Vec2 p) {
        finishContour();
        contour_.points.push_back(p); current_ = start_ = p; hasCurrent_ = true;
    }
    void finishContour() {
        if (contour_.points.size() >= 2 && near(contour_.points.front(), contour_.points.back())) contour_.points.pop_back();
        if (contour_.points.size() >= 3) contours_.push_back(std::move(contour_));
        contour_ = {};
    }
    void addCubic(Vec2 c1, Vec2 c2, Vec2 end) {
        if (options_.curveTolerance) flattenCubic(contour_.points, current_, c1, c2, end, *options_.curveTolerance);
        else for (int i = 1; i <= options_.curveSegments; ++i) {
            const double t = static_cast<double>(i) / options_.curveSegments, u = 1.0 - t;
            contour_.points.push_back(current_ * (u*u*u) + c1 * (3*u*u*t) + c2 * (3*u*t*t) + end * (t*t*t));
        }
        current_ = end; lastCubicControl_ = c2; previousCubic_ = true; previousQuadratic_ = false;
    }
    void addQuadratic(Vec2 c, Vec2 end) {
        if (options_.curveTolerance) flattenQuadratic(contour_.points, current_, c, end, *options_.curveTolerance);
        else for (int i = 1; i <= options_.curveSegments; ++i) {
            const double t = static_cast<double>(i) / options_.curveSegments, u = 1.0 - t;
            contour_.points.push_back(current_ * (u*u) + c * (2*u*t) + end * (t*t));
        }
        current_ = end; lastQuadraticControl_ = c; previousQuadratic_ = true; previousCubic_ = false;
    }
    void addArc(double rx, double ry, double rotation, bool largeArc, bool sweep, Vec2 end) {
        rx = std::abs(rx); ry = std::abs(ry);
        if (rx <= kEps || ry <= kEps || near(current_, end)) { lineTo(end); return; }
        const double phi = rotation * kPi / 180.0, cp = std::cos(phi), sp = std::sin(phi);
        const double dx = (current_.x - end.x) * 0.5, dy = (current_.y - end.y) * 0.5;
        const double xp = cp * dx + sp * dy, yp = -sp * dx + cp * dy;
        const double lambda = xp*xp/(rx*rx) + yp*yp/(ry*ry);
        if (lambda > 1) { const double s = std::sqrt(lambda); rx *= s; ry *= s; }
        const double numerator = std::max(0.0, rx*rx*ry*ry - rx*rx*yp*yp - ry*ry*xp*xp);
        const double denominator = rx*rx*yp*yp + ry*ry*xp*xp;
        double factor = denominator <= kEps ? 0.0 : std::sqrt(numerator / denominator);
        if (largeArc == sweep) factor = -factor;
        const double cxp = factor * rx * yp / ry, cyp = factor * -ry * xp / rx;
        const Vec2 center{cp*cxp - sp*cyp + (current_.x + end.x)*0.5, sp*cxp + cp*cyp + (current_.y + end.y)*0.5};
        auto angle = [](Vec2 u, Vec2 v) { return std::atan2(cross(u, v), dot(u, v)); };
        Vec2 u{(xp-cxp)/rx, (yp-cyp)/ry}, v{(-xp-cxp)/rx, (-yp-cyp)/ry};
        double startAngle = std::atan2(u.y, u.x), delta = angle(u, v);
        if (!sweep && delta > 0) delta -= 2*kPi;
        if (sweep && delta < 0) delta += 2*kPi;
        int segments = std::max(1, static_cast<int>(std::ceil(options_.curveSegments * std::abs(delta) / (2*kPi))));
        if (options_.curveTolerance) {
            const double radius = std::max(rx, ry);
            const double maxAngle = radius <= *options_.curveTolerance ? kPi : 2*std::acos(std::clamp(1-*options_.curveTolerance/radius, -1.0, 1.0));
            segments = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / std::max(maxAngle, 1e-4))));
        }
        for (int i = 1; i <= segments; ++i) {
            const double a = startAngle + delta * static_cast<double>(i) / segments;
            contour_.points.push_back({center.x + cp*rx*std::cos(a) - sp*ry*std::sin(a), center.y + sp*rx*std::cos(a) + cp*ry*std::sin(a)});
        }
        current_ = end; previousCubic_ = previousQuadratic_ = false;
    }
    void execute(char command) {
        const bool relative = std::islower(static_cast<unsigned char>(command));
        const char op = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
        if (op == 'Z') { if (hasCurrent_) current_ = start_; finishContour(); previousCubic_ = previousQuadratic_ = false; return; }
        bool first = true;
        while (hasNumber()) {
            if (op == 'M') {
                Vec2 p = nextPoint(relative);
                if (first) beginContour(p); else lineTo(p);
            } else if (op == 'L') lineTo(nextPoint(relative));
            else if (op == 'H') { double x = nextNumber(); lineTo({relative ? current_.x + x : x, current_.y}); }
            else if (op == 'V') { double y = nextNumber(); lineTo({current_.x, relative ? current_.y + y : y}); }
            else if (op == 'C') {
                Vec2 c1 = nextPoint(relative), c2 = nextPoint(relative), p = nextPoint(relative); addCubic(c1, c2, p);
            } else if (op == 'S') {
                Vec2 c1 = previousCubic_ ? current_ * 2 - lastCubicControl_ : current_;
                Vec2 c2 = nextPoint(relative), p = nextPoint(relative); addCubic(c1, c2, p);
            } else if (op == 'Q') {
                Vec2 c = nextPoint(relative), p = nextPoint(relative); addQuadratic(c, p);
            } else if (op == 'T') {
                Vec2 c = previousQuadratic_ ? current_ * 2 - lastQuadraticControl_ : current_;
                addQuadratic(c, nextPoint(relative));
            } else if (op == 'A') {
                const double rx = nextNumber(), ry = nextNumber(), rotation = nextNumber();
                const bool large = nextNumber() != 0, sweep = nextNumber() != 0;
                addArc(rx, ry, rotation, large, sweep, nextPoint(relative));
            } else throw std::runtime_error(std::string("Unsupported SVG path command: ") + op);
            first = false;
            if (op != 'C' && op != 'S') previousCubic_ = false;
            if (op != 'Q' && op != 'T') previousQuadratic_ = false;
        }
    }
};

struct SvgState {
    Matrix transform;
    std::string fill{"black"};
    FillRule fillRule{FillRule::NonZero};
    bool display{true};
};

std::vector<PathShape> loadSvg(const fs::path& path, const ParseOptions& options) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open input SVG: " + path.string());
    std::string xml((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::vector<PathShape> shapes;
    std::vector<SvgState> stack(1);
    std::size_t pos = 0, pathIndex = 0;
    while ((pos = xml.find('<', pos)) != std::string::npos) {
        const std::size_t end = xml.find('>', pos + 1);
        if (end == std::string::npos) break;
        std::string_view tag(xml.data() + pos, end - pos + 1);
        pos = end + 1;
        if (tag.starts_with("<!--")) { const auto close = xml.find("-->", end + 1); if (close != std::string::npos) pos = close + 3; continue; }
        if (tag.size() > 1 && (tag[1] == '?' || tag[1] == '!')) continue;
        const bool closing = tag.size() > 2 && tag[1] == '/';
        const bool selfClosing = tag.size() > 2 && tag[tag.size() - 2] == '/';
        std::size_t namePos = closing ? 2 : 1;
        while (namePos < tag.size() && std::isspace(static_cast<unsigned char>(tag[namePos]))) ++namePos;
        const std::size_t nameStart = namePos;
        while (namePos < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[namePos])) || tag[namePos] == ':' || tag[namePos] == '_')) ++namePos;
        std::string tagName = lower(std::string(tag.substr(nameStart, namePos - nameStart)));
        if (const auto colon = tagName.find(':'); colon != std::string::npos) tagName = tagName.substr(colon + 1);
        if (closing) { if ((tagName == "g" || tagName == "svg") && stack.size() > 1) stack.pop_back(); continue; }
        auto attrs = parseAttributes(tag);
        SvgState state = stack.back();
        if (const auto it = attrs.find("transform"); it != attrs.end()) state.transform = state.transform * parseTransform(it->second);
        if (const auto it = attrs.find("fill"); it != attrs.end() && lower(trim(it->second)) != "inherit") state.fill = it->second;
        if (const auto it = attrs.find("fill-rule"); it != attrs.end()) state.fillRule = lower(trim(it->second)) == "evenodd" ? FillRule::EvenOdd : FillRule::NonZero;
        if (const auto it = attrs.find("display"); it != attrs.end() && lower(trim(it->second)) == "none") state.display = false;
        if (const auto it = attrs.find("visibility"); it != attrs.end() && lower(trim(it->second)) == "hidden") state.display = false;
        if (tagName == "path" && state.display) {
            const auto d = attrs.find("d");
            const auto color = parseColor(state.fill);
            if (d != attrs.end() && color) {
                PathShape shape;
                shape.name = attrs.contains("id") ? attrs["id"] : "path_" + std::to_string(++pathIndex);
                shape.color = *color; shape.fillRule = state.fillRule;
                shape.contours = PathDataParser(d->second, options).parse();
                for (auto& contour : shape.contours) {
                    std::vector<Vec2> cleaned;
                    cleaned.reserve(contour.points.size());
                    for (auto point : contour.points) {
                        point = state.transform.apply(point);
                        point.y = -point.y;
                        if (cleaned.empty() || !near(cleaned.back(), point, 1e-7)) cleaned.push_back(point);
                    }
                    if (cleaned.size() > 1 && near(cleaned.front(), cleaned.back(), 1e-7)) cleaned.pop_back();
                    contour.points = std::move(cleaned);
                }
                std::erase_if(shape.contours, [](const Contour& contour) { return contour.points.size() < 3; });
                if (!shape.contours.empty()) shapes.push_back(std::move(shape));
            }
        }
        if (!selfClosing && (tagName == "g" || tagName == "svg")) stack.push_back(state);
    }
    return shapes;
}

int windingNumber(Vec2 p, const std::vector<Contour>& contours) {
    int winding = 0;
    for (const auto& contour : contours) for (std::size_t i = 0; i < contour.points.size(); ++i) {
        const Vec2 a = contour.points[i], b = contour.points[(i + 1) % contour.points.size()];
        if (a.y <= p.y) { if (b.y > p.y && cross(b - a, p - a) > 0) ++winding; }
        else if (b.y <= p.y && cross(b - a, p - a) < 0) --winding;
    }
    return winding;
}

bool inside(Vec2 p, const std::vector<Contour>& contours, FillRule rule) {
    const int winding = windingNumber(p, contours);
    return rule == FillRule::EvenOdd ? (std::abs(winding) % 2) == 1 : winding != 0;
}

std::uint64_t edgeKey(std::size_t a, std::size_t b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint64_t>(b);
}

struct Triangulation {
    std::vector<Vec2> points;
    std::vector<std::array<std::size_t, 3>> triangles;
    std::vector<Contour> contours;
};

void splitDisconnectedVertexFans(Triangulation& mesh) {
    struct Corner { std::size_t triangle{}, corner{}; };
    const auto originalTriangles = mesh.triangles;
    std::vector<std::vector<Corner>> incident(mesh.points.size());
    for (std::size_t ti = 0; ti < originalTriangles.size(); ++ti) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            incident[originalTriangles[ti][corner]].push_back({ti, corner});
        }
    }

    std::vector<Vec2> splitPoints;
    splitPoints.reserve(mesh.points.size());
    for (std::size_t vertex = 0; vertex < incident.size(); ++vertex) {
        const auto& corners = incident[vertex];
        if (corners.empty()) continue;
        std::vector<std::size_t> parent(corners.size());
        for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = i;
        auto root = [&](std::size_t value) {
            while (parent[value] != value) {
                parent[value] = parent[parent[value]];
                value = parent[value];
            }
            return value;
        };
        auto unite = [&](std::size_t a, std::size_t b) {
            a = root(a); b = root(b);
            if (a != b) parent[b] = a;
        };
        std::map<std::size_t, std::vector<std::size_t>> throughNeighbor;
        for (std::size_t local = 0; local < corners.size(); ++local) {
            const auto& corner = corners[local];
            const auto& triangle = originalTriangles[corner.triangle];
            throughNeighbor[triangle[(corner.corner + 1) % 3]].push_back(local);
            throughNeighbor[triangle[(corner.corner + 2) % 3]].push_back(local);
        }
        for (const auto& [neighbor, connected] : throughNeighbor) {
            (void)neighbor;
            for (std::size_t i = 1; i < connected.size(); ++i) unite(connected[0], connected[i]);
        }
        std::map<std::size_t, std::size_t> componentVertex;
        for (std::size_t local = 0; local < corners.size(); ++local) {
            const std::size_t component = root(local);
            auto [it, inserted] = componentVertex.emplace(component, splitPoints.size());
            if (inserted) splitPoints.push_back(mesh.points[vertex]);
            const auto& corner = corners[local];
            mesh.triangles[corner.triangle][corner.corner] = it->second;
        }
    }
    mesh.points = std::move(splitPoints);
}

Triangulation triangulate(PathShape shape) {
    // Delaunay is unconstrained. Repeatedly split any outline edge absent from
    // the triangulation; sufficiently short empty segments become Delaunay edges.
    for (int iteration = 0; iteration < 24; ++iteration) {
        std::vector<Vec2> points;
        std::vector<std::vector<std::size_t>> indices;
        std::map<std::pair<double, double>, std::size_t> pointIndices;
        for (const auto& contour : shape.contours) {
            indices.emplace_back();
            for (Vec2 p : contour.points) {
                const auto key = std::make_pair(p.x, p.y);
                const auto [it, inserted] = pointIndices.emplace(key, points.size());
                if (inserted) points.push_back(p);
                indices.back().push_back(it->second);
            }
        }
        if (points.size() < 3) throw std::runtime_error("Path has too few distinct points");
        std::vector<double> coords; coords.reserve(points.size() * 2);
        for (Vec2 p : points) { coords.push_back(p.x); coords.push_back(p.y); }
        delaunator::Delaunator d(coords);
        std::set<std::uint64_t> edges;
        for (std::size_t i = 0; i + 2 < d.triangles.size(); i += 3) {
            const auto a = d.triangles[i], b = d.triangles[i+1], c = d.triangles[i+2];
            edges.insert(edgeKey(a,b)); edges.insert(edgeKey(b,c)); edges.insert(edgeKey(c,a));
        }
        bool split = false;
        for (std::size_t ci = 0; ci < shape.contours.size(); ++ci) {
            auto& contour = shape.contours[ci];
            std::vector<Vec2> refined;
            refined.reserve(contour.points.size() * 2);
            for (std::size_t i = 0; i < contour.points.size(); ++i) {
                const std::size_t j = (i + 1) % contour.points.size();
                refined.push_back(contour.points[i]);
                if (!edges.contains(edgeKey(indices[ci][i], indices[ci][j]))) {
                    const Vec2 a = contour.points[i], b = contour.points[j];
                    const Vec2 segment = b - a;
                    const double segmentLengthSquared = dot(segment, segment);
                    Vec2 splitPoint = (a + b) * 0.5;
                    double bestDistanceFromMiddle = std::numeric_limits<double>::max();
                    if (segmentLengthSquared > kEps * kEps) {
                        const double segmentLength = std::sqrt(segmentLengthSquared);
                        for (Vec2 candidate : points) {
                            const double t = dot(candidate - a, segment) / segmentLengthSquared;
                            if (t <= 1e-9 || t >= 1.0 - 1e-9) continue;
                            if (std::abs(cross(segment, candidate - a)) > segmentLength * 1e-7) continue;
                            const double distanceFromMiddle = std::abs(t - 0.5);
                            if (distanceFromMiddle < bestDistanceFromMiddle) {
                                splitPoint = candidate;
                                bestDistanceFromMiddle = distanceFromMiddle;
                            }
                        }
                    }
                    if (near(splitPoint, contour.points[i], 1e-12) || near(splitPoint, contour.points[j], 1e-12))
                        throw std::runtime_error("Could not recover a polygon boundary edge");
                    if (iteration + 1 < 24) refined.push_back(splitPoint);
                    split = true;
                }
            }
            contour.points = std::move(refined);
        }
        if (split && iteration + 1 < 24) continue;
        Triangulation result;
        result.points = std::move(points);
        result.contours = std::move(shape.contours);
        for (std::size_t i = 0; i + 2 < d.triangles.size(); i += 3) {
            std::array<std::size_t,3> tri{d.triangles[i], d.triangles[i+1], d.triangles[i+2]};
            const Vec2 centroid = (result.points[tri[0]] + result.points[tri[1]] + result.points[tri[2]]) * (1.0/3.0);
            if (inside(centroid, result.contours, shape.fillRule)) result.triangles.push_back(tri);
        }
        splitDisconnectedVertexFans(result);
        return result;
    }
    throw std::runtime_error("Could not triangulate SVG path");
}

std::string safeName(std::string value) {
    for (char& c : value) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) c = '_';
    if (value.empty()) value = "path";
    return value;
}

struct MeshWriter {
    std::ofstream obj;
    std::ofstream mtl;
    std::size_t nextVertex{1};
    std::map<std::array<int, 3>, std::string> materialsByColor;

    MeshWriter(const fs::path& objPath, const fs::path& mtlPath) : obj(objPath), mtl(mtlPath) {
        if (!obj) throw std::runtime_error("Cannot create OBJ: " + objPath.string());
        if (!mtl) throw std::runtime_error("Cannot create MTL: " + mtlPath.string());
        obj << std::setprecision(10); mtl << std::setprecision(8);
        obj << "# Generated by svg2obj\nmtllib " << mtlPath.filename().generic_string() << "\n\n";
        mtl << "# Generated by svg2obj\n\n";
    }

    void write(const PathShape& shape, const Triangulation& tri, double height, std::size_t materialIndex) {
        const std::string objectName = safeName(shape.name) + "_" + std::to_string(materialIndex);
        const std::array<int, 3> colorKey{
            static_cast<int>(std::lround(std::clamp(shape.color.r, 0.0, 1.0) * 255.0)),
            static_cast<int>(std::lround(std::clamp(shape.color.g, 0.0, 1.0) * 255.0)),
            static_cast<int>(std::lround(std::clamp(shape.color.b, 0.0, 1.0) * 255.0)),
        };
        auto [material, inserted] = materialsByColor.try_emplace(
            colorKey,
            "color_" + std::to_string(colorKey[0]) + "_" + std::to_string(colorKey[1]) + "_" + std::to_string(colorKey[2]));
        if (inserted) {
            mtl << "newmtl " << material->second << "\nKd " << shape.color.r << ' ' << shape.color.g << ' ' << shape.color.b
                << "\nKa 0 0 0\nKs 0 0 0\nd 1\nillum 1\n\n";
        }
        obj << "o " << objectName << "\nusemtl " << material->second << "\n";
        const std::size_t base = nextVertex;
        for (Vec2 p : tri.points) obj << "v " << p.x << ' ' << p.y << " 0\n";
        for (Vec2 p : tri.points) obj << "v " << p.x << ' ' << p.y << ' ' << height << "\n";
        const std::size_t count = tri.points.size();
        auto face = [&](std::size_t a, std::size_t b, std::size_t c) { obj << "f " << a << ' ' << b << ' ' << c << "\n"; };
        for (auto t : tri.triangles) {
            const double area = cross(tri.points[t[1]] - tri.points[t[0]], tri.points[t[2]] - tri.points[t[0]]);
            if (area < 0) std::swap(t[1], t[2]);
            face(base + count + t[0], base + count + t[1], base + count + t[2]);
            face(base + t[2], base + t[1], base + t[0]);
        }
        struct BoundaryEdge { std::size_t a{}, b{}, uses{}; };
        std::map<std::uint64_t, BoundaryEdge> boundaryEdges;
        for (auto t : tri.triangles) {
            if (cross(tri.points[t[1]] - tri.points[t[0]], tri.points[t[2]] - tri.points[t[0]]) < 0) std::swap(t[1], t[2]);
            for (std::size_t i = 0; i < 3; ++i) {
                const std::size_t a = t[i], b = t[(i + 1) % 3];
                auto& edge = boundaryEdges[edgeKey(a, b)];
                if (edge.uses++ == 0) { edge.a = a; edge.b = b; }
            }
        }
        for (const auto& [key, edge] : boundaryEdges) {
            (void)key;
            if (edge.uses != 1) continue;
            const std::size_t ba = base + edge.a, bb = base + edge.b;
            const std::size_t ta = ba + count, tb = bb + count;
            face(ba, bb, tb);
            face(ba, tb, ta);
        }
        obj << "\n";
        nextVertex += count * 2;
    }
};

void printUsage(std::ostream& out) {
    out << "Usage: svg2obj <input.svg> <output.obj> <height> [options]\n\n"
        << "Options:\n"
        << "  --curve-segments <n>   Samples per Bezier curve (default: 12)\n"
        << "  --curve-tolerance <d>  Adaptive flatness tolerance; overrides segments\n"
        << "  --help                 Show this help\n";
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") { printUsage(std::cout); return 0; }
        if (argc < 4) { printUsage(std::cerr); return 2; }
        const fs::path inputPath = argv[1], outputPath = argv[2];
        const auto heightValue = number(argv[3]);
        if (!heightValue || !std::isfinite(*heightValue) || *heightValue <= 0) throw std::runtime_error("Extrude height must be a positive number");
        ParseOptions options;
        for (int i = 4; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--curve-segments" && i + 1 < argc) {
                const auto value = number(argv[++i]);
                if (!value || *value < 1 || *value > 10000 || std::floor(*value) != *value) throw std::runtime_error("--curve-segments must be an integer from 1 to 10000");
                options.curveSegments = static_cast<int>(*value);
            } else if (arg == "--curve-tolerance" && i + 1 < argc) {
                const auto value = number(argv[++i]);
                if (!value || !std::isfinite(*value) || *value <= 0) throw std::runtime_error("--curve-tolerance must be positive");
                options.curveTolerance = *value;
            } else if (arg == "--help") { printUsage(std::cout); return 0; }
            else throw std::runtime_error("Unknown or incomplete option: " + arg);
        }
        if (lower(outputPath.extension().string()) != ".obj") throw std::runtime_error("Output file must have an .obj extension");
        fs::path mtlPath = outputPath; mtlPath.replace_extension(".mtl");
        const auto shapes = loadSvg(inputPath, options);
        if (shapes.empty()) throw std::runtime_error("No visible filled SVG path elements were found");
        MeshWriter writer(outputPath, mtlPath);
        std::size_t triangleCount = 0;
        for (std::size_t i = 0; i < shapes.size(); ++i) {
            Triangulation tri;
            try {
                tri = triangulate(shapes[i]);
            } catch (const std::exception& error) {
                throw std::runtime_error("Path " + std::to_string(i + 1) + " (" + shapes[i].name + "): " + error.what());
            }
            triangleCount += tri.triangles.size();
            writer.write(shapes[i], tri, *heightValue, i + 1);
        }
        std::cout << "Converted " << shapes.size() << " path(s), " << triangleCount << " top triangles.\n"
                  << "OBJ: " << fs::absolute(outputPath).string() << "\nMTL: " << fs::absolute(mtlPath).string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "svg2obj: error: " << error.what() << '\n';
        return 1;
    }
}
