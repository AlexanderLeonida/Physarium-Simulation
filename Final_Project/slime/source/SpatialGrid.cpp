#include "SpatialGrid.h"
#include "Agent.h"
#include <iostream>
#include <algorithm>

SpatialGrid::SpatialGrid(int width, int height)
    : width_(width), height_(height), invCellSize_(1.0f / CELL_SIZE)
{
    // pre allocate the expected cell count
    size_t expectedCells = (width / CELL_SIZE + 1) * (height / CELL_SIZE + 1);
    cells_.reserve(expectedCells);
}

void SpatialGrid::clear()
{
    for (auto &[key, cell] : cells_)
    {
        cell.clear();
    }
}

void SpatialGrid::insertAgent(size_t agentIndex, float x, float y)
{
    auto [gridX, gridY] = worldToGrid(x, y);
    uint64_t hash = hashPosition(gridX, gridY);

    cells_[hash].addAgent(agentIndex);
}

void SpatialGrid::rebuild(const std::vector<Agent> &agents)
{
    clear();

    for (size_t i = 0; i < agents.size(); ++i)
    {
        insertAgent(i, agents[i].position.x, agents[i].position.y);
    }
}

std::vector<size_t> SpatialGrid::getNeighbors(float x, float y, float radius) const
{
    std::vector<size_t> neighbors;
    neighbors.reserve(64); // reasonable initial capacity

    // calculate grid range to check
    int radiusInCells = static_cast<int>(std::ceil(radius * invCellSize_));
    auto [centerX, centerY] = worldToGrid(x, y);

    // check all cells within radius
    for (int dx = -radiusInCells; dx <= radiusInCells; ++dx)
    {
        for (int dy = -radiusInCells; dy <= radiusInCells; ++dy)
        {
            int cellX = centerX + dx;
            int cellY = centerY + dy;

            uint64_t hash = hashPosition(cellX, cellY);
            auto it = cells_.find(hash);

            if (it != cells_.end())
            {
                const auto &cell = it->second;
                neighbors.insert(neighbors.end(),
                                 cell.agentIndices.begin(),
                                 cell.agentIndices.end());
            }
        }
    }

    return neighbors;
}

std::vector<size_t> SpatialGrid::getNeighborsInCell(float x, float y) const
{
    auto [gridX, gridY] = worldToGrid(x, y);
    uint64_t hash = hashPosition(gridX, gridY);

    auto it = cells_.find(hash);
    if (it != cells_.end())
    {
        return it->second.agentIndices;
    }

    return {};
}

std::vector<size_t> SpatialGrid::getSensingNeighbors(float x, float y, float sensorDistance) const
{
    // optimized for typical agent sensing operations
    return getNeighbors(x, y, sensorDistance * 1.1f); // small buffer for the edge cases
}

size_t SpatialGrid::getTotalAgentEntries() const
{
    size_t total = 0;
    for (const auto &[key, cell] : cells_)
    {
        total += cell.agentIndices.size();
    }
    return total;
}

void SpatialGrid::printStatistics() const
{
    size_t totalAgents = getTotalAgentEntries();
    size_t cellCount = getCellCount();

    std::cout << "=== SpatialGrid Statistics ===" << std::endl;
    std::cout << "Active cells: " << cellCount << std::endl;
    std::cout << "Total agent entries: " << totalAgents << std::endl;
    std::cout << "Average agents per cell: "
              << (cellCount > 0 ? static_cast<float>(totalAgents) / cellCount : 0.0f)
              << std::endl;
    std::cout << "Cell size: " << CELL_SIZE << std::endl;
    std::cout << "Grid dimensions: " << (width_ / CELL_SIZE) << "x" << (height_ / CELL_SIZE) << std::endl;
}

void SpatialGrid::reserve(size_t expectedAgents)
{
    size_t expectedCells = expectedAgents / ESTIMATED_AGENTS_PER_CELL + 1;
    cells_.reserve(expectedCells);
}

std::vector<size_t> SpatialGrid::getNearbyAgents(float x, float y, float radius) const
{
    return getNeighbors(x, y, radius);
}
