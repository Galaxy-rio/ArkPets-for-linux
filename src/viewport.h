#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

struct ModelBounds {
    float minX = std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();

    void include(float x, float y) {
        minX = std::min(minX, x); minY = std::min(minY, y);
        maxX = std::max(maxX, x); maxY = std::max(maxY, y);
    }
    void include(const ModelBounds& other) {
        if (!other.valid()) return;
        include(other.minX, other.minY); include(other.maxX, other.maxY);
    }
    float width() const { return maxX - minX; }
    float height() const { return maxY - minY; }
    bool valid() const {
        return std::isfinite(minX) && std::isfinite(minY) &&
               std::isfinite(maxX) && std::isfinite(maxY) && width() > 0 && height() > 0;
    }
    void includeMirror() {
        maxX = std::max(std::abs(minX), std::abs(maxX)); minX = -maxX;
    }
};

struct MovementLimits {
    int minRight, maxRight, minBottom, maxBottom;
};

// All models use the requested model-unit to logical-pixel scale. Animation
// bounds only determine the transparent surface, never the character's scale.
struct ModelViewport {
    double scale = 1, minX = 0, minY = 0;
    int width = 1, height = 1;

    ModelViewport(const ModelBounds& bounds, double requestedScale) : scale(requestedScale) {
        if (!bounds.valid() || !std::isfinite(scale) || scale <= 0)
            throw std::runtime_error("模型范围或缩放倍率无效");
        const double padding = std::max(4.0, std::ceil(16 * scale));
        const double w = std::ceil(bounds.width() * scale + 2 * padding);
        const double h = std::ceil(bounds.height() * scale + 2 * padding);
        if (!std::isfinite(w) || !std::isfinite(h) || w > 16384 || h > 16384 || w*h > 32*1024*1024)
            throw std::runtime_error("模型动作范围过大，请减小缩放倍率");
        width = static_cast<int>(w); height = static_cast<int>(h);
        // Center horizontal rounding slack so mirrored artwork stays aligned.
        minX = (bounds.minX + bounds.maxX) / 2.0 - width / (2 * scale);
        minY = bounds.minY - padding / scale;
    }
    float projectX(float x, int logicalWidth) const {
        return static_cast<float>((x - minX) * scale * 2 / logicalWidth - 1);
    }
    float projectY(float y, int logicalHeight) const {
        return static_cast<float>((y - minY) * scale * 2 / logicalHeight - 1);
    }
    MovementLimits movementLimits(const ModelBounds& body, int screenWidth, int screenHeight, bool flipped) const {
        // Layer margins describe the full canvas, which may extend offscreen.
        // Only the Default pose must fit inside the monitor, in either facing.
        const double left = ((flipped ? -body.maxX : body.minX) - minX) * scale;
        const double right = ((flipped ? -body.minX : body.maxX) - minX) * scale;
        const double bottom = (body.minY - minY) * scale;
        const double top = (body.maxY - minY) * scale;
        auto range = [](double low, double high) {
            int first = static_cast<int>(std::ceil(low)), last = static_cast<int>(std::floor(high));
            // If the character itself is larger than the monitor, center it.
            if (first > last) first = last = static_cast<int>(std::lround((low + high) / 2));
            return std::pair{first, last};
        };
        auto horizontal = range(right - width, screenWidth - width + left);
        auto vertical = range(-bottom, screenHeight - top);
        return {horizontal.first, horizontal.second, vertical.first, vertical.second};
    }
};
