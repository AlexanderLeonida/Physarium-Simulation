#pragma once
#include <SFML/Graphics.hpp>
#include <cmath>

// the food pellet structure for the goal seeking behavior
struct FoodPellet
{
    sf::Vector2f position;
    float strength;  // positive = attractive | negative = repulsive
    float radius;    // effective range
    float decayRate; // how fast the pellet diminishes over time
    int pelletType;  // 0 = universal, 1+ = species-specific attraction/repulsion

    FoodPellet(float x, float y, float str, float rad = 100.0f, float decay = 0.98f, int type = 0)
        : position(x, y), strength(str), radius(rad), decayRate(decay), pelletType(type) {}

    bool isExpired() const { return std::abs(strength) < 1.0f; }
    void update() { strength *= decayRate; }
};
