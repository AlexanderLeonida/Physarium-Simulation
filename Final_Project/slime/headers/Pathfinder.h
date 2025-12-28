#pragma once
#include <vector>
#include <queue>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <functional>
#include "SimulationSettings.h"

// grid cell coordinates
struct GridCell {
    int x, y;
    
    bool operator==(const GridCell& other) const {
        return x == other.x && y == other.y;
    }
    
    bool operator!=(const GridCell& other) const {
        return !(*this == other);
    }
    
    // required for priority queue with std::greater<std::pair<float, GridCell>>
    bool operator<(const GridCell& other) const {
        if (x != other.x) return x < other.x;
        return y < other.y;
    }
    
    bool operator>(const GridCell& other) const {
        return other < *this;
    }
};

// hash function for GridCell (for use in unordered_map/set)
struct GridCellHash {
    size_t operator()(const GridCell& cell) const {
        return std::hash<int>()(cell.x) ^ (std::hash<int>()(cell.y) << 16);
    }
};

// result of a pathfinding operation
struct PathResult {
    std::vector<GridCell> path;          // the path from start to goal
    int nodesExpanded = 0;               // number of nodes expanded during search
    double computeTimeMs = 0.0;          // time taken to compute path in milliseconds
    bool found = false;                  // whether a path was found
    float pathLength = 0.0f;             // total path length in grid cells
};

// the obstacle representation data structure
struct Obstacle {
    int x, y;           // top left corner in grid coordinates
    int width, height;  // size in grid cells
};

class Pathfinder {
public:
    Pathfinder(int width, int height, int cellSize = 4);
    
    // grid management
    void resize(int width, int height);
    void setCellSize(int size);
    int getCellSize() const { return cellSize_; }
    int getGridWidth() const { return gridWidth_; }
    int getGridHeight() const { return gridHeight_; }
    int getWorldWidth() const { return worldWidth_; }
    int getWorldHeight() const { return worldHeight_; }
    
    // obstacle management
    void clearObstacles();
    void addObstacle(int x, int y, int width, int height);
    void addObstacle(const Obstacle& obs);
    void generateRandomObstacles(int count, int minSize, int maxSize, int marginLeft, int marginRight, bool clearExisting = true);
    void generateMaze(float density = 0.3f);  // legacy simple maze
    
    // advanced maze types for challenging benchmarks
    enum class MazeType {
        Simple,           // original horizontal barriers (easy)
        Labyrinth,        // true recursive backtracking maze with dead ends
        MultiPath,        // multiple viable routes of different lengths
        Bottleneck,       // convergence points that force decisions
        Spiral,           // spiral pattern with shortcuts
        Chambers,         // connected chambers with multiple entrances
        TrueMaze          // perfect maze for empirical doubling (controlled complexity)
    };
    void generateAdvancedMaze(MazeType type, float difficulty = 0.5f);
    
    // true maze for empirical doubling experiments
    // complexityLevel: 1=simple (8 cells), 2=medium (16 cells), 3=hard (32 cells), etc.
    // Each level doubles the number of maze cells
    void generateTrueMaze(int complexityLevel);
    int getMazeCellCount() const { return mazeCellCount_; }  // for reporting
    std::pair<int, int> getMazeExit() const { return {mazeExitX_, mazeExitY_}; }  // exit grid position
    
    bool isBlocked(int gridX, int gridY) const;
    bool isBlocked(const GridCell& cell) const;
    const std::vector<Obstacle>& getObstacles() const { return obstacles_; }
    
    // coordinate conversion
    GridCell worldToGrid(float worldX, float worldY) const;
    std::pair<float, float> gridToWorld(const GridCell& cell) const;
    std::pair<float, float> gridToWorld(int gridX, int gridY) const;
    
    // pathfinding algos - all return PathResult with timing info
    // explorers (blind search - don't need goal to explore)
    // pathfinders (goal aware)
    PathResult findPathAStar(const GridCell& start, const GridCell& goal);
    PathResult findPathGreedy(const GridCell& start, const GridCell& goal);
    PathResult findPathBidirectional(const GridCell& start, const GridCell& goal);
    PathResult findPathDFS(const GridCell& start, const GridCell& goal);
    PathResult findPathDijkstra(const GridCell& start, const GridCell& goal);
    PathResult findPathJPS(const GridCell& start, const GridCell& goal);
    PathResult findPathTheta(const GridCell& start, const GridCell& goal);
    
    // generic pathfinding dispatcher
    PathResult findPath(SimulationSettings::Algos algo, const GridCell& start, const GridCell& goal);
    PathResult findPath(SimulationSettings::Algos algo, float startX, float startY, float goalX, float goalY);
    
    // path utilities
    std::vector<GridCell> simplifyPath(const std::vector<GridCell>& path) const;
    
    // utility
    float heuristic(const GridCell& a, const GridCell& b) const;
    float euclideanDistance(const GridCell& a, const GridCell& b) const;
    float manhattanDistance(const GridCell& a, const GridCell& b) const;
    bool lineOfSight(const GridCell& a, const GridCell& b) const;
    std::vector<GridCell> getNeighbors(const GridCell& cell, bool allowDiagonal = true) const;
    
private:
    int worldWidth_, worldHeight_;      // world dimensions in pixels
    int cellSize_;                      // size of each grid cell in pixels
    int gridWidth_, gridHeight_;        // grid dimensions in cells
    
    std::vector<bool> blocked_;         // blocked cells (true = obstacle)
    std::vector<Obstacle> obstacles_;   // list of obstacles for rendering
    
    // helper to get the flat index from grid coordinates
    int getIndex(int x, int y) const { return y * gridWidth_ + x; }
    int getIndex(const GridCell& cell) const { return cell.y * gridWidth_ + cell.x; }
    
    // check if cell is valid (in bounds and not blocked)
    bool isValid(int x, int y) const;
    bool isValid(const GridCell& cell) const;
    
    // reconstruct path from came_from map
    std::vector<GridCell> reconstructPath(
        const std::unordered_map<GridCell, GridCell, GridCellHash>& cameFrom,
        const GridCell& start, const GridCell& goal);
    
    // calculate path length
    float calculatePathLength(const std::vector<GridCell>& path) const;
    
    // JPS helpers
    GridCell jump(const GridCell& current, int dx, int dy, const GridCell& goal);
    std::vector<GridCell> getJPSNeighbors(const GridCell& current, const GridCell& parent);
    bool hasForced(const GridCell& cell, int dx, int dy) const;
    
    // advanced maze generation helpers
    void generateLabyrinthMaze(int spawnMargin, int goalMargin, float difficulty);
    void generateMultiPathMaze(int spawnMargin, int goalMargin, float difficulty);
    void generateBottleneckMaze(int spawnMargin, int goalMargin, float difficulty);
    void generateSpiralMaze(int spawnMargin, int goalMargin, float difficulty);
    void generateChambersMaze(int spawnMargin, int goalMargin, float difficulty);
    void carvePassage(int x1, int y1, int x2, int y2, int width = 2);
    void floodFillCheck(int startX, int startY, int goalX, int goalY);  // verify path exists
    
    int mazeCellCount_ = 0;  // track maze complexity for reporting
    int mazeExitX_ = 0;      // exit grid X position (for goal placement)
    int mazeExitY_ = 0;      // exit grid Y position
};
