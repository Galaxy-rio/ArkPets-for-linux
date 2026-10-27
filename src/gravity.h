#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

struct StandingEdge {
    // Includes the window's suspension revision. After a pause only pets on
    // this window detach, even if low FPS missed its temporarily absent edges.
    std::string id;
    double left = 0, right = 0, y = 0, originX = 0;
};

// Local monitor coordinates, with y pointing down. The Default body supplies
// the footprint; effect bounds and the current animation never affect physics.
class GravityBody {
public:
    double x = 0, y = 0, halfWidth = 0, height = 0;
    double velocity = 0;

    bool grounded() const { return m_grounded; }
    bool onWindow() const { return m_grounded && !m_support.id.empty(); }
    double walkLeft() const { return m_support.left + halfWidth; }
    double walkRight() const { return m_support.right - halfWidth; }
    void release() { m_grounded = false; m_support = {}; velocity = 0; }

    void step(double dt, const std::vector<StandingEdge>& edges, double width, double floor,
              bool dropWindows = false) {
        dt = std::clamp(dt, 0.0, 1.0);
        auto fits = [&](const StandingEdge& e, double center) {
            return center-halfWidth >= e.left-.6 && center+halfWidth <= e.right+.6 &&
                   e.y >= std::min(height, floor)-.6 && e.y <= floor+.6;
        };
        double crossedFrom = y;
        if (onWindow()) {
            bool retained = false;
            if (!dropWindows) for (const auto& e : edges) {
                if (e.id != m_support.id) continue;
                const double nextX = x + e.originX-m_support.originX;
                if (!fits(e,nextX)) continue;
                x = nextX; y = e.y; m_support = e; retained = true; break;
            }
            if (!retained) {
                // A withdrawn support must not catch the pet at exactly the
                // same height when a short IPC suspension ends between frames.
                crossedFrom = y + 1;
                release();
            }
        }
        if (m_grounded) return;
        constexpr double acceleration = 1800;
        const double nextY = y + velocity*dt + .5*acceleration*dt*dt;
        velocity += acceleration*dt;
        const StandingEdge* landing = nullptr;
        for (const auto& e : edges) {
            if (fits(e,x) && e.y >= crossedFrom-.6 && e.y <= nextY &&
                (!landing || e.y < landing->y)) landing = &e;
        }
        if (landing) {
            y = landing->y; m_support = *landing; velocity = 0; m_grounded = true;
        } else if (nextY >= floor) {
            y = floor; m_support = {"",0,width,floor,0}; velocity = 0; m_grounded = true;
        } else y = nextY;
    }

private:
    StandingEdge m_support;
    bool m_grounded = false;
};

// Bounded, line-based pipe protocol shared by the Qt manager and GTK renderer.
// The generation covers source resets; edge IDs carry per-window suspensions.
inline bool readStandingEdges(const std::string& line, unsigned long long& generation,
                              std::vector<StandingEdge>& edges) {
    std::istringstream in(line);
    in.imbue(std::locale::classic());
    std::string command; unsigned long long nextGeneration = 0; int count = 0;
    if (!(in >> command >> nextGeneration >> count) || command != "ARKPETS_EDGES" || count < 0 || count > 4096) return false;
    std::vector<StandingEdge> next;
    for (int i=0; i<count; ++i) {
        StandingEdge edge;
        if (!(in >> std::quoted(edge.id) >> edge.left >> edge.right >> edge.y >> edge.originX) ||
            edge.id.size()>128 || !std::isfinite(edge.left) || !std::isfinite(edge.right) ||
            !std::isfinite(edge.y) || !std::isfinite(edge.originX) || edge.left>=edge.right) return false;
        next.push_back(std::move(edge));
    }
    in >> std::ws;
    if (!in.eof()) return false;
    generation = nextGeneration; edges = std::move(next); return true;
}
