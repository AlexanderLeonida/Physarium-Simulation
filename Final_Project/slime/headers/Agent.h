#pragma once
#include <SFML/Graphics.hpp>
#include "SimulationSettings.h"
#include "Pathfinder.h"

// forward declarations for optimization systems
// TODO: restructure to change this
class SpatialGrid;
class OptimizedTrailMap;
struct SharedExplorationState;
#include "FoodPellet.h"


// based on N body sim
// mem aligned agent structure for optimal cache performance
struct alignas(64) Agent // align to full cache line (64 bytes on modern cpus)
{
public:
    // hot data accessed every frame (first 32 bytes first half of cache line)
    sf::Vector2f position; // 8 bytes
    float angle;           // 4 bytes
    float previousAngle;   // 4 bytes - for momentum/inertia
    int speciesIndex;      // 4 bytes
    float energy;          // 4 bytes - for advanced behaviors
    float stateTimer;      // 4 bytes - for time based behaviors
    int behaviorState;     // 4 bytes - for complex state machines
    // total: 32 bytes exactly

    // warm data that is accessed during movement/sensing (second 32 bytes)
    sf::Vector2f velocity;        // 8 bytes - for physics based movement
    sf::Vector2f acceleration;    // 8 bytes - for realistic dynamics
    float sensorRange = 40.0f;            // 4 bytes - cached for performance (default value)
    float moveSpeed = 2.0f;               // 4 bytes - cached from species settings (default value)
    float turnSpeed = 16.0f;              // 4 bytes - cached from species settings (default value)
    float angularVelocity = 0.0f; // 4 bytes - per-agent heading angular velocity
    uint32_t gridHash;            // 4 bytes - spatial grid optimization

                                  // NOTE: warm block now 36 bytes; slight overflow past 64-byte guideline but is acceptable for correctness

    // cached constraint for turn clamping (radians per step)
    float maxTurnPerStep = 3.14159265f;

    // benchmark control slime helpers
    float benchmarkEnergy = 1.0f;
    float benchmarkSignalMemory = 0.0f;
    int benchmarkRespawnFrames = 0;
    int benchmarkLowSignalFrames = 0;
    bool benchmarkAlive = true;
    sf::Vector2f benchmarkSpawnPosition = sf::Vector2f(0.0f, 0.0f);
    sf::Vector2f benchmarkDefaultSpawnPosition = sf::Vector2f(0.0f, 0.0f);
    int benchmarkLaneIndex = 0;  // the lane (0-6) this agent spawns in
    int benchmarkWallSlideSign = 1;
    int benchmarkWallFollowFrames = 0;
    float benchmarkWallSlideHeading = 0.0f;
    float benchmarkPrevGoalDistance = -1.0f;
    int benchmarkRecentCollisionFrames = 0;


    // cold data... the accessed less frequently (separate cache line)
    sf::Vector3i speciesMask; // for the multi species support

    // genetics and lifecycle (cold data)
    struct Genome
    {
        float moveSpeedScale = 1.0f;
        float turnSpeedScale = 1.0f;
        float sensorAngleScale = 1.0f;
        float sensorDistScale = 1.0f;
        float alignWScale = 1.0f;
        float cohWScale = 1.0f;
        float sepWScale = 1.0f;
        float oscStrengthScale = 1.0f;
        float oscFreqScale = 1.0f;
    } genome;
    bool hasGenome = false;
    float ageSeconds = 0.0f;
    float mateCooldown = 0.0f;
    float splitCooldown = 0.0f;
    float lifespanSeconds = 180.0f;
    int parentSpeciesA = -1;
    int parentSpeciesB = -1;

    // path following for algorithm race mode
    std::vector<GridCell> currentPath;      // path from pathfinder
    size_t pathIndex = 0;                   // current position in path
    bool hasPath = false;                   // whether agent has a computed path
    bool reachedGoal = false;               // whether agent has reached the goal
    bool foundGoalFirst = false;            // whether this agent was first to find goal
    int agentId = -1;                       // unique ID for race tracking
    SimulationSettings::Algos assignedAlgo = SimulationSettings::Algos::AStar; // algorithm used

    // path memory for reward reinforcement 
    // ring buffer of recent positions - used to reinforce successful paths when goal is found
    static constexpr size_t PATH_MEMORY_SIZE = 64;  // last N positions to remember
    std::array<sf::Vector2i, PATH_MEMORY_SIZE> recentPositions;  // ring buffer
    size_t pathMemoryIndex = 0;             // current write index in ring buffer
    size_t pathMemoryCount = 0;             // how many positions stored (up to PATH_MEMORY_SIZE)
    
    // push a position into the path memory ring buffer
    void pushPathMemory(int x, int y) {
        recentPositions[pathMemoryIndex] = sf::Vector2i(x, y);
        pathMemoryIndex = (pathMemoryIndex + 1) % PATH_MEMORY_SIZE;
        if (pathMemoryCount < PATH_MEMORY_SIZE) pathMemoryCount++;
    }

    // for explorers (DFS, Dijkstra) that dont know goal location
    bool isExploring = false;                              // agent is in exploration mode
    bool isLeader = false;                                 // is this agent the "leader" for shared exploration (DFS)
    bool isBackwardWave = false;                           // for Bidirectional: true = searching from goal, false = from start
    GridCell currentCell;                                  // current grid position
    std::deque<GridCell> explorationFrontier;              // cells to explore next (deque for efficient front/back operations)
    std::unordered_set<GridCell, GridCellHash> visitedCells; // already visited cells
    std::unordered_map<GridCell, GridCell, GridCellHash> explorationParents; // for path reconstruction
    std::unordered_map<GridCell, float, GridCellHash> explorationCosts;      // for Dijkstra: cost to reach each cell
    int explorationStepsPerFrame = 1;                      // how many cells to explore per frame
    
    // smooth movement for exploration (same speed as pathfinders)
    float explorationTargetX = 0.0f;                       // target world X position
    float explorationTargetY = 0.0f;                       // target world Y position
    float explorationProgress = 1.0f;                      // 0-1 progress toward target cell

    // yellow (erratic)
    bool alienStateInit = false;
    float alienSpeedPhase = 0.0f;
    int alienSpeedMode = 0;
    // magenta (orderly)
    bool antiAlienStateInit = false;
    float antiAlienSpeedPhase = 0.0f;
    int antiAlienSpeedMode = 0;
    int antiAlienSpeedCounter = 0;
    // padding to maintain alignment (if needed)
    alignas(8) char padding_[4]; // next agent starts on cache boundary

    Agent(float x, float y, float a, int species = 0);
    ~Agent(); 

    // species mask management for correct color rendering after rerolls
    void setDefaultSpeciesMask(int localSpeciesIndex);
    void setSpeciesMaskFromOriginalIndex(int originalSpeciesIndex);

    // core behaviors
    void move(const SimulationSettings &settings);
    void moveWithPelletSeeking(const SimulationSettings &settings, const std::vector<FoodPellet> &foodPellets);
    void sense(const float *chemoattractant, int width, int height, const SimulationSettings &settings);
    void senseMultiSpeciesOptimized(class TrailMap &trailMap, const SimulationSettings &settings,
                                    const SpatialGrid &spatialGrid, const std::vector<Agent> &allAgents);
    void senseWithSpatialGrid(const SpatialGrid &spatialGrid, const std::vector<Agent> &allAgents,
                              class OptimizedTrailMap &trailMap, const SimulationSettings &settings);
    void depositOptimized(class OptimizedTrailMap &trailMap, const SimulationSettings &settings);
    void senseMultiSpecies(class TrailMap &trailMap, const SimulationSettings &settings);

    void deposit(float *chemoattractant, int width, int height, const SimulationSettings &settings);
    void depositMultiSpecies(class TrailMap &trailMap, const SimulationSettings &settings);
    void depositBenchmark(class TrailMap &trailMap, float strength);  // simple direct deposit for benchmark mode

    // boundary handling
    void wrapPosition(int width, int height);

    // path following methods for algorithm race mode
    void setPath(const std::vector<GridCell>& path, SimulationSettings::Algos algo);
    void clearPath();
    bool followPath(const Pathfinder& pathfinder, float moveSpeed, float goalRadius);
    bool hasValidPath() const { return hasPath && !currentPath.empty(); }
    float getPathProgress() const;
    
    // blind exploration methods 
    // initializes the exploration from current position (no goal knowledge)
    void initExploration(const Pathfinder& pathfinder, SimulationSettings::Algos algo);
    // initialize bidirectional exploration (specify if backward wave)
    void initBidirectional(const Pathfinder& pathfinder, bool backward, const GridCell& startCell);
    // takes one exploration step - returns true if found goal this step
    // uses shared state if provided, otherwise falls back to private state
    bool exploreStep(const Pathfinder& pathfinder, const GridCell& goalCell, float moveSpeed, SharedExplorationState* sharedState = nullptr);
    // Dijkstra exploration - priority queue by shortest distance
    bool exploreStepDijkstra(const Pathfinder& pathfinder, const GridCell& goalCell, float moveSpeed);
    // bidirectional exploration step - uses BidirectionalState to detect meeting
    bool exploreStepBidirectional(const Pathfinder& pathfinder, float moveSpeed, struct BidirectionalState* biState);
    
    // benchmark slime behavior 
    // authentic slime behavior for benchmark mode - trail based navigation with weak goal bias
    // goalX/goalY: used for weak directional bias when no trail detected (not cheating - just drift)
    // returns true if goal was found (by sensing high trail concentration at goal)
    bool benchmarkSlimeStep(class TrailMap& trailMap, const Pathfinder& pathfinder,
                            const SimulationSettings &settings,
                            float trailDepositStrength, float goalX, float goalY,
                            int goalFieldChannel);
    // reinforce the recent path with bonus trail deposits when goal is found
    // creates stigmergic feedback - other slimes will follow the proven path
    void reinforceRecentPath(class TrailMap& trailMap, float baseStrength);
    // follower behavior for hybrid DFS - trail behind leader, filling explored area
    bool exploreStepFollower(const Pathfinder& pathfinder, const GridCell& goalCell, float moveSpeed, SharedExplorationState* sharedState);
    // checks if agent has discovered the goal (physically reached it)
    bool hasDiscoveredGoal() const { return reachedGoal; }

    // goal seeking behavior for food pellets
    // sf::vector2f calculatepelletforce(const std::vector<foodpellet> &foodpellets, const simulationsettings &settings) const;
    // bool shouldseekpellets(const simulationsettings &settings) const;

    // advanced behaviors 
    enum class LifeEvent
    {
        None,
        Rebirth,
        Died
    };
    // updates energy/age; may rebirth in place (rebirth) or signal death (died)
    LifeEvent updateEnergyAndState(const SimulationSettings &settings);
    void applyInertia(const SimulationSettings &settings);

    // optimized agent interaction calculation
    float calculateAgentInteraction(const Agent &otherAgent, float distance, const SimulationSettings::SpeciesSettings &species) const;

    // genetics helpers
    static Agent createOffspring(const Agent &a, const Agent &b, const SimulationSettings &settings);
    void applyGenomeToCachedParams(const SimulationSettings &settings);

private:
    float sampleChemoattractant(const float *grid, int x, int y, int width, int height) const;

    // custom species specific sensing behaviors
    // each species has unique sensing patterns that create distinct emergent behaviors
    float senseRedTerritorialBully(class TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species);
    float senseBlueAltruisticHelper(class TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species);
    float senseGreenNomadicLoner(class TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species);
    float senseYellowQuantumAlien(class TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species);
    float senseMagentaOrderEnforcer(class TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species);
    float senseParasiticInvader(class TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species);
    float senseDemonicDestroyer(class TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species);
    float senseAbsoluteDevourer(class TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species);

    // turning behaviors
    // for processes sensory information differently for unique movement patterns
    void applyRedBullyTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species);
    void applyBlueCooperativeTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species);
    void applyGreenAvoidanceTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species);
    void applyAlienQuantumTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species);
    void applyOrderEnforcerTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species);
    void applyParasiticHuntingTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species);
    void applyDemonicDestructionTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species);
    void applyDevourerConsumptionTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species);

    // food pellet seeking behaviors
    sf::Vector2f calculatePelletForce(const std::vector<FoodPellet> &foodPellets, const SimulationSettings &settings) const;
    bool shouldSeekPellets(const SimulationSettings &settings) const;

    // deposition pattern helpers
    void depositThickTrail(class TrailMap &trailMap, int centerX, int centerY, float strength, int radius);
    void depositNetworkPattern(class TrailMap &trailMap, int centerX, int centerY, float strength);
    void depositSegmentedPattern(class TrailMap &trailMap, int centerX, int centerY, float strength);
    void depositRadialPattern(class TrailMap &trailMap, int centerX, int centerY, float strength);
    void depositAlienPattern(class TrailMap &trailMap, int centerX, int centerY, float strength, const SimulationSettings &settings);
    void depositParasiticPattern(class TrailMap &trailMap, int centerX, int centerY, float strength, const SimulationSettings &settings);
    void depositDestructivePattern(class TrailMap &trailMap, int centerX, int centerY, float strength);
    void depositProtectivePattern(class TrailMap &trailMap, int centerX, int centerY, float strength);
};

// agent factory for different spawn modes
class AgentFactory
{
public:
    static std::vector<Agent> createAgents(const SimulationSettings &settings);
    static std::vector<Agent> createAgents(const SimulationSettings &settings, const std::vector<int> &activeSpeciesIndices);

private:
    static sf::Vector2f getSpawnPosition(SimulationSettings::SpawnMode mode,
                                         const sf::Vector2f &center,
                                         int width, int height);
    static float getSpawnAngle(SimulationSettings::SpawnMode mode,
                               const sf::Vector2f &position,
                               const sf::Vector2f &center);
};
