#pragma once

#include <cmath>

namespace RadiantCursorTrail
{

struct Vector
{
    float x = 0.0f;
    float y = 0.0f;
};

inline Vector positionBehind(Vector head, Vector movement, float distanceBehind, float lateralOffset)
{
    const float length = std::hypot(movement.x, movement.y);
    if (length <= 0.001f) {
        return head;
    }

    const Vector forward{movement.x / length, movement.y / length};
    const Vector perpendicular{-forward.y, forward.x};
    return {
        head.x - forward.x * distanceBehind + perpendicular.x * lateralOffset,
        head.y - forward.y * distanceBehind + perpendicular.y * lateralOffset,
    };
}

} // namespace RadiantCursorTrail
