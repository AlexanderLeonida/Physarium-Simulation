#pragma once
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <queue>
#include "SimulationSettings.h"
#include "Pathfinder.h"
#include <SFML/Graphics.hpp>

// shared exploration state for explorer algorithms (BFS, Dijkstra, DFS, RandomWalk, whatever)
struct SharedExplorationState {
    std::deque<GridCell> frontier;  // shared frontier queue
    std::unordered_set<GridCell, GridCellHash> visited;  // shared visited set
    std::unordered_map<GridCell, GridCell, GridCellHash> parents;  // for path reconstruction
    bool foundGoal = false;
    GridCell goalCell;
    
    // leader agent for "follow the leader" DFS (only one agent runs actual DFS)
    int leaderAgentId = -1;  // ID of the leader agent (-1 = no leader assigned yet)
    bool hasLeader = false;
    
    // rate limiting for cell claims - prevents all agents grabbing cells at once
    int claimsThisFrame = 0;
    int maxClaimsPerFrame = 1;  // only N agents can claim cells per frame (slower avoids insta complete)
    
    void clear() {
        frontier.clear();
        visited.clear();
        parents.clear();
        foundGoal = false;
        claimsThisFrame = 0;
        leaderAgentId = -1;
        hasLeader = false;
    }
    
    void resetFrameClaims() {
        claimsThisFrame = 0;
    }
    
    bool canClaimCell() {
        if (claimsThisFrame < maxClaimsPerFrame) {
            claimsThisFrame++;
            return true;
        }
        return false;
    }
};

// bidirectional search state - two waves that meet in the middle
struct BidirectionalState {
    // forward wave (from start)
    std::unordered_set<GridCell, GridCellHash> forwardVisited;
    // backward wave (from goal)  
    std::unordered_set<GridCell, GridCellHash> backwardVisited;
    
    bool wavesHaveMet = false;  // true when forward and backward waves meet
    GridCell meetingPoint;      // where they met
    
    void clear() {
        forwardVisited.clear();
        backwardVisited.clear();
        wavesHaveMet = false;
    }
    
    // check for if a forward agent has reached backward territory (or vice versa)
    bool checkMeeting(const GridCell& cell, bool isForward) {
        if (wavesHaveMet) return true;
        
        if (isForward) {
            // forward agent checking if backward wave already visited this cell
            if (backwardVisited.count(cell)) {
                wavesHaveMet = true;
                meetingPoint = cell;
                return true;
            }
        } else {
            // backward agent checking if forward wave already visited this cell
            if (forwardVisited.count(cell)) {
                wavesHaveMet = true;
                meetingPoint = cell;
                return true;
            }
        }
        return false;
    }
    
    // and mark a cell as visited by forward or backward wave
    void markVisited(const GridCell& cell, bool isForward) {
        if (isForward) {
            forwardVisited.insert(cell);
        } else {
            backwardVisited.insert(cell);
        }
    }
};

// statistics for a single algorithm in the benchmark
struct AlgorithmStats {
    SimulationSettings::Algos algorithm;
    std::string name;
    sf::Color color;
    
    // benchmark progress
    int totalAgents = 0;
    int arrivedAgents = 0;
    bool finished = false;
    
    // timing
    double firstArrivalTimeMs = -1.0;   // time when first agent arrived
    double lastArrivalTimeMs = -1.0;    // time when last agent arrived
    double avgArrivalTimeMs = 0.0;      // average arrival time
    
    // pathfinding metrics (from initial path computation)
    double totalComputeTimeMs = 0.0;    // total time to compute all paths
    double avgComputeTimeMs = 0.0;      // average path compute time
    int totalNodesExpanded = 0;         // total nodes expanded across all agents
    int avgNodesExpanded = 0;           // average nodes expanded per agent
    float avgPathLength = 0.0f;         // average path length
    
    // ranking
    int rank = 0;                       // 1 = first to finish, etc.
    
    float getArrivalPercent() const {
        return totalAgents > 0 ? (100.0f * arrivedAgents / totalAgents) : 0.0f;
    }
};

// all the results of empirical doubling test per algorithm
struct DoublingResult {
    SimulationSettings::Algos algorithm;
    std::string algoName;
    int problemSize;           // N (maze cells)
    double timeMs;             // pathfinding compute time
    double ratio;              // T(2N) / T(N)
    std::string estimatedBigO; // estimated complexity
};

class BenchmarkManager {
public:
    BenchmarkManager();
    
    // benchmark setup
    void setupBenchmark(int width, int height, int agentsPerAlgorithm = 1000);
    void setGoalPosition(float x, float y);
    void reset();
    
    // maze type control
    void setMazeType(Pathfinder::MazeType type) { currentMazeType_ = type; }
    Pathfinder::MazeType getMazeType() const { return currentMazeType_; }
    void cycleMazeType();  // cycle to next maze type
    void setMazeDifficulty(float difficulty) { mazeDifficulty_ = std::clamp(difficulty, 0.0f, 1.0f); }
    float getMazeDifficulty() const { return mazeDifficulty_; }
    static const char* getMazeTypeName(Pathfinder::MazeType type);
    std::string getComplexityInfo() const;  // returns "Level X (N=Y cells)"
    void regenerateMaze();  // regenerate w/ current settings

    // benchmark controls
    void startBenchmark();
    void pauseBenchmark();
    void resumeBenchmark();
    bool isBenchmarkActive() const { return benchmarkActive_; }
    bool isBenchmarkComplete() const { return benchmarkComplete_; }
    bool isPaused() const { return benchmarkPaused_; }
    
    // (called each frame to update timing)
    void update(float deltaTime);
    
    // the agent arrival notification
    void recordArrival(int speciesIndex, int agentId);
    
    // statistics
    const std::vector<AlgorithmStats>& getStats() const { return stats_; }
    AlgorithmStats& getStatsMutable(int speciesIndex);
    double getBenchmarkElapsedMs() const;
    int getTotalArrivals() const;
    int getTotalAgents() const;
    
    // pathfinder access
    Pathfinder& getPathfinder() { return pathfinder_; }
    const Pathfinder& getPathfinder() const { return pathfinder_; }
    
    // goal position
    float getGoalX() const { return goalX_; }
    float getGoalY() const { return goalY_; }
    GridCell getGoalCell() const { return pathfinder_.worldToGrid(goalX_, goalY_); }
    float getSpawnMargin() const { return spawnMargin_; }
    int getArenaHeight() const { return height_; }
    
    // get spawn position for a species lane
    std::pair<float, float> getSpawnPosition(int speciesIndex, int agentIndex, int totalAgentsInSpecies) const;
    
    // algorithms in the benchmark (8 total)
    static const std::vector<SimulationSettings::Algos>& getBenchmarkAlgorithms();
    static sf::Color getAlgorithmColor(SimulationSettings::Algos algo);
    static const char* getAlgorithmName(SimulationSettings::Algos algo);
    
    // empirical doubling
    void runDoublingExperiment();
    const std::vector<DoublingResult>& getDoublingResults() const { return doublingResults_; }
    
    // rendering helpers
    void drawObstacles(sf::RenderTarget& target) const;
    void drawGoal(sf::RenderTarget& target) const;
    void drawHUD(sf::RenderTarget& target, const sf::Font& font) const;
    
    // updates agent counts for HUD display
    void updateAgentCounts(int newPerAlgo);
    
    // track which algorithms are enabled for HUD display
    void setAlgorithmEnabled(size_t index, bool enabled);
    bool isAlgorithmEnabled(size_t index) const;
    
private:
    Pathfinder pathfinder_;
    std::vector<AlgorithmStats> stats_;
    std::vector<DoublingResult> doublingResults_;
    std::vector<bool> algorithmEnabled_;  // which algorithms to show in HUD
    
    // benchmark state
    bool benchmarkActive_ = false;
    bool benchmarkComplete_ = false;
    bool benchmarkPaused_ = false;
    std::chrono::high_resolution_clock::time_point benchmarkStartTime_;
    std::chrono::high_resolution_clock::time_point pauseStartTime_;
    double totalPausedTimeMs_ = 0.0;
    
    // benchmark parameters
    int width_ = 800;
    int height_ = 600;
    int agentsPerAlgorithm_ = 1000;
    float goalX_ = 0.0f;
    float goalY_ = 0.0f;
    float spawnMargin_ = 50.0f;  // left margin for spawn area
    float goalMargin_ = 50.0f;   // right margin for goal
    
    // track which agents have arrived (to prevent double counting)
    std::unordered_map<int, std::unordered_set<int>> arrivedAgents_; // speciesIndex -> set of agentIds
    
    // maze settings
    Pathfinder::MazeType currentMazeType_ = Pathfinder::MazeType::MultiPath;
    float mazeDifficulty_ = 0.5f;
    
    // ranking
    int nextRank_ = 1;
    
    // then shared exploration state per algorithm (for proper BFS/Dijkstra visualization)
    std::unordered_map<SimulationSettings::Algos, SharedExplorationState> sharedExplorationStates_;
    
    // bidirectional search state (shared between forward and backward agents)
    BidirectionalState bidirectionalState_;
    
public:
    // access shared exploration state for an algorithm
    SharedExplorationState* getSharedExplorationState(SimulationSettings::Algos algo);
    
    // access bidirectional state
    BidirectionalState* getBidirectionalState() { return &bidirectionalState_; }
    
private:
    void initializeStats();
    void updateRankings();
    std::string estimateBigO(double ratio) const;
};
