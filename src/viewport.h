#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

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

// The reference pose controls scale; other poses only enlarge the transparent
// surface. Coordinates and the ground line stay fixed for the whole session.
struct ModelViewport {
    double scale = 1, minX = 0, minY = 0;
    int width = 1, height = 1;

    ModelViewport(const ModelBounds& bounds, double referenceHeight, int logicalHeight) {
        if (!bounds.valid() || !std::isfinite(referenceHeight) || referenceHeight <= 0)
            throw std::runtime_error("模型没有可见的高度基准");
        scale = logicalHeight / referenceHeight;
        const double padding = std::max(4.0, std::ceil(logicalHeight * .04));
        const double w = std::ceil(bounds.width() * scale + 2 * padding);
        const double h = std::ceil(bounds.height() * scale + 2 * padding);
        if (!std::isfinite(w) || !std::isfinite(h) || w > 16384 || h > 16384 || w*h > 32*1024*1024)
            throw std::runtime_error("模型动作范围过大，请减小人物高度");
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
};
