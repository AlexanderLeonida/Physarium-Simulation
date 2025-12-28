#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <unordered_map>
#include <cmath>

// TODO: restructure to change this forward declaration
struct Agent;

/**
 * high performance spatial hash grid for O(1) neighbor lookup
 * inspired by the N body simulation optimization techniques
 * replaces O(n^2) agent interactions with O(1) spatial queries
 */
class SpatialGrid
{
public:
    // optimized cell size based on typical agent sensor range
    static constexpr float CELL_SIZE = 50.0f;
    static constexpr size_t ESTIMATED_AGENTS_PER_CELL = 16;

    struct Cell
    {
        std::vector<size_t> agentIndices;

        Cell()
        {
            agentIndices.reserve(ESTIMATED_AGENTS_PER_CELL);
        }

        void clear()
        {
            agentIndices.clear();
        }

        void addAgent(size_t agentIndex)
        {
            agentIndices.push_back(agentIndex);
        }
    };

private:
    std::unordered_map<uint64_t, Cell> cells_;
    int width_;
    int height_;
    float invCellSize_; // precomputed for faster division

    // high quality hash function for spatial coordinates
    static constexpr uint64_t hashPosition(int x, int y)
    {
        // optimized hash combining two large primes
        return ((static_cast<uint64_t>(x) * 92837111ULL) ^
                (static_cast<uint64_t>(y) * 689287499ULL)) *
               15485863ULL;
    }

    // convert world position to grid coordinates
    inline std::pair<int, int> worldToGrid(float x, float y) const
    {
        return {
            static_cast<int>(std::floor(x * invCellSize_)),
            static_cast<int>(std::floor(y * invCellSize_))};
    }

public:
    SpatialGrid(int width, int height);
    ~SpatialGrid() = default;

    // core operations
    void clear();
    void insertAgent(size_t agentIndex, float x, float y);
    void rebuild(const std::vector<Agent> &agents);

    // neighbor queries (the performance magic happens here)
    std::vector<size_t> getNeighbors(float x, float y, float radius) const;
    std::vector<size_t> getNeighborsInCell(float x, float y) const;

    // optimized queries for common agent operations
    std::vector<size_t> getSensingNeighbors(float x, float y, float sensorDistance) const;
    std::vector<size_t> getNearbyAgents(float x, float y, float radius) const; // alias for compatibility

    // statistics and debugging
    size_t getCellCount() const { return cells_.size(); }
    size_t getTotalAgentEntries() const;
    void printStatistics() const;

    // memory management
    void reserve(size_t expectedAgents);
};
