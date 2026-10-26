#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

enum class ActivityKind { Idle, Walk, Sit, Sleep };

struct ActivitySpec {
    ActivityKind kind;
    std::string animation;
    double minimum, maximum, weight;
};

inline std::string findAnimation(const std::vector<std::string>& animations,
                                 std::initializer_list<const char*> aliases) {
    auto lower = [](std::string name) {
        for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return name;
    };
    for (const auto* alias : aliases)
        for (const auto& name : animations)
            if (lower(name) == alias) return name;
    return {};
}

inline const std::string& chooseInteraction(const std::string& interact,
                                             const std::string& special,
                                             std::mt19937& random) {
    // Draw independently for each press. Missing actions never become invalid
    // animation names; an empty result leaves the current animation running.
    return !special.empty() && std::uniform_int_distribution<int>(0, 4)(random) == 0
        ? special : interact;
}

inline std::vector<ActivitySpec> activityPlan(const std::vector<std::string>& animations,
                                               const std::string& fallback) {
    std::string idle = findAnimation(animations, {"relax", "idle", "idle1", "idle_01", "stand", "default"});
    if (idle.empty()) idle = fallback;
    std::vector<ActivitySpec> plan{{ActivityKind::Idle, idle, 5, 12, 4}};
    auto append = [&](ActivityKind kind, std::string name, double min, double max, double weight) {
        if (!name.empty()) plan.push_back({kind, std::move(name), min, max, weight});
    };
    append(ActivityKind::Walk, findAnimation(animations, {"move", "walk"}), 3, 7, 3);
    append(ActivityKind::Sit, findAnimation(animations, {"sit"}), 6, 14, 2);
    append(ActivityKind::Sleep, findAnimation(animations, {"sleep"}), 12, 24, 1);
    return plan;
}

// Time and position are independent of rendering FPS. The host supplies monitor
// bounds and persists positions; this class never accesses windows or files.
class ActivityDirector {
public:
    ActivityDirector(std::vector<ActivitySpec> plan, double right, std::uint32_t seed)
        : m_plan(std::move(plan)), m_random(seed), m_right(right) {
        m_remaining = randomDuration(2, 4);
    }

    const std::string& animation() const { return m_plan[m_current].animation; }
    ActivityKind kind() const { return m_plan[m_current].kind; }
    bool walking() const { return !m_paused && kind() == ActivityKind::Walk; }
    bool facingRight() const { return m_direction < 0; }
    bool paused() const { return m_paused; }
    double right() const { return m_right; }
    double remaining() const { return m_remaining; }

    void pause() {
        m_paused = true;
        m_current = 0;
    }
    void resumeAt(double right) {
        m_right = right;
        m_current = 0;
        m_remaining = randomDuration(2, 4);
        m_paused = false;
    }

    // Returns whether an activity ended. Any number of elapsed state boundaries
    // is handled, so 15 FPS and 120 FPS follow the same trajectory for one seed.
    bool advance(double elapsed, double maximumRight, double speed) {
        maximumRight = std::max(0.0, maximumRight);
        m_right = std::clamp(m_right, 0.0, maximumRight);
        if (m_paused || !std::isfinite(elapsed) || elapsed <= 0) return false;
        bool changed = false;
        while (elapsed > 1e-9) {
            double step = std::min(elapsed, m_remaining);
            if (walking() && maximumRight > 0) {
                m_right += m_direction * std::max(0.0, speed) * step;
                while (m_right < 0 || m_right > maximumRight) {
                    if (m_right < 0) { m_right = -m_right; m_direction = 1; }
                    if (m_right > maximumRight) { m_right = 2*maximumRight-m_right; m_direction = -1; }
                }
            }
            elapsed -= step;
            m_remaining -= step;
            if (m_remaining < 1e-9) {
                chooseNext(maximumRight);
                changed = true;
            }
        }
        return changed;
    }

private:
    std::vector<ActivitySpec> m_plan;
    std::mt19937 m_random;
    std::size_t m_current = 0;
    double m_remaining = 0, m_right = 0;
    int m_direction = 1;
    bool m_paused = false, m_firstWalk = true;

    double randomDuration(double min, double max) {
        return std::uniform_real_distribution<double>(min, max)(m_random);
    }
    void chooseNext(double maximumRight) {
        auto walk = std::find_if(m_plan.begin(), m_plan.end(), [](const auto& a) {
            return a.kind == ActivityKind::Walk;
        });
        if (m_firstWalk && walk != m_plan.end() && maximumRight > 0) {
            m_current = static_cast<std::size_t>(walk-m_plan.begin());
        } else {
            std::vector<double> weights;
            for (std::size_t i = 0; i < m_plan.size(); ++i) {
                bool allowed = (i != m_current || m_plan.size() == 1) &&
                    (m_plan[i].kind != ActivityKind::Walk || maximumRight > 0);
                weights.push_back(allowed ? m_plan[i].weight : 0);
            }
            if (std::any_of(weights.begin(), weights.end(), [](double w) {return w > 0;}))
                m_current = std::discrete_distribution<std::size_t>(weights.begin(), weights.end())(m_random);
            else m_current = 0;
        }
        m_firstWalk = false;
        const auto& activity = m_plan[m_current];
        m_remaining = randomDuration(activity.minimum, activity.maximum);
        if (activity.kind == ActivityKind::Walk) {
            m_direction = std::uniform_int_distribution<int>(0, 1)(m_random) ? 1 : -1;
            if (m_right < 1) m_direction = 1;
            if (m_right > maximumRight-1) m_direction = -1;
        }
    }
};
