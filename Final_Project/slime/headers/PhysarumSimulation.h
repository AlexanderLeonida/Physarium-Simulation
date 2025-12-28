#pragma once
#include <SFML/Graphics.hpp>
#include "SimulationSettings.h"
#include "Agent.h"
#include "TrailMap.h"
#include "SpatialGrid.h"
#include "ParallelProcessor.h"
#include "OptimizedTrailMap.h"
#include "FoodPellet.h"
#include "BenchmarkManager.h"
#include <vector>
#include <memory>
#include <optional>

class PhysarumSimulation
{
public:
    PhysarumSimulation(const SimulationSettings &settings);
    ~PhysarumSimulation() = default;

    // core simulation methods
    void update(float deltaTime);
    void reset();
    // void draw(sf::renderwindow &window);
    void draw(sf::RenderWindow &window, sf::Font &font);

    // settings management
    void updateSettings(const SimulationSettings &newSettings);
    const SimulationSettings &getSettings() const { return settings_; }

    // agent management
    void setAgentCount(int count);
    void adjustAgentCount(int delta);  // add/remove agents without full reset
    int getAgentCount() const { return agents_.size(); }

    // mouse interaction methods
    void depositFood(int x, int y, float amount, int radius = 15);
    void depositRepellent(int x, int y, float amount, int radius = 15);

    // food pellet system for goal-seeking behavior
    void addFoodPellet(int x, int y, float strength, float radius = 100.0f, int pelletType = 0);
    void clearFoodPellets();
    const std::vector<FoodPellet> &getFoodPellets() const { return foodPellets_; }

    // performance tracking
    float getLastUpdateTime() const { return lastUpdateTime_; }

    // tree visualization methods
    void setTreeVisualizationMode(bool showNodes, bool showConnections, bool showAgents);
    bool isTreeVisualizationEnabled() const;

    // cluster label control
    void toggleClusterLabels() { showClusterLabels_ = !showClusterLabels_; }
    bool areClusterLabelsEnabled() const { return showClusterLabels_; }
    void toggleAgentOverlay() { showAgentOverlay_ = !showAgentOverlay_; }
    
    // hides all of the UI elements 
    void setHideAllUI(bool hide) { hideAllUI_ = hide; }
    bool isAllUIHidden() const { return hideAllUI_; }
    bool isAgentOverlayEnabled() const { return showAgentOverlay_; }

    // Algorithm Benchmark Mode
    void enterBenchmarkMode();
    void exitBenchmarkMode();
    bool isInBenchmarkMode() const { return inBenchmarkMode_; }
    void updateBenchmark(float deltaTime);
    void startBenchmark();
    void pauseBenchmark();
    void resetBenchmark();
    void adjustBenchmarkAgentCount(int delta);  // dynamic agent count adjustment
    BenchmarkManager& getBenchmarkManager() { return benchmarkManager_; }
    const BenchmarkManager& getBenchmarkManager() const { return benchmarkManager_; }
    void toggleBenchmarkPackPositions();
    bool isBenchmarkPacked() const { return benchmarkAgentsPacked_; }
    void toggleBenchmarkAlgorithm(int algoIndex);  // toggle for algorithm group on/off (0-6)

private:
    SimulationSettings settings_;
    std::vector<Agent> agents_;
    std::unique_ptr<TrailMap> trailMap_;

    // food pellet system
    std::vector<FoodPellet> foodPellets_;

    std::unique_ptr<SpatialGrid> spatialGrid_;
    std::unique_ptr<ParallelProcessor> parallelProcessor_;
    std::unique_ptr<OptimizedTrailMap> optimizedTrailMap_;

    // performance mode flags

    // TODO: figure out what is wrong then fix this crap or just write a metal kernel .mm
    bool useOptimizedSystems_ = false; // temporarily disabled due to crashes
    bool useParallelUpdates_ = true;

    // rendering components
    sf::Image displayImage_;
    sf::Texture displayTexture_;
    std::optional<sf::Sprite> displaySprite_;

    // agent overlay components
    sf::Texture agentOverlayTexture_;
    std::optional<sf::Sprite> agentOverlaySprite_;
    std::vector<uint8_t> overlayPixelBuffer_;

    // performance tracking
    float lastUpdateTime_ = 0.0f;
    sf::Clock updateTimer_;

    // audit counters (debug): track events per frame
    std::uint64_t auditSplits_ = 0;
    std::uint64_t auditMatingsSame_ = 0;
    std::uint64_t auditMatingsCross_ = 0;
    std::uint64_t auditRebirths_ = 0;
    std::uint64_t auditSpores_ = 0;
    std::uint64_t auditDeaths_ = 0;

    // cumulative per species death tracking (persists across frames)
    std::vector<std::uint64_t> cumulativeDeathsPerSpecies_;
    std::uint64_t totalCumulativeDeaths_ = 0;

    // tree visualization system
    bool enableTreeVisualization_ = false;

    // cluster label visibility toggle
    bool showClusterLabels_ = true;
    bool showAgentOverlay_ = false;
    bool hideAllUI_ = false;  

    // Algorithm Benchmark Mode state
    bool inBenchmarkMode_ = false;
    BenchmarkManager benchmarkManager_;
    std::vector<size_t> benchmarkShuffledOrder_;  // randomized algorithm lane order (maybe put them in one spot)
    
    // slime communication state* (for benchmark mode)
    bool slimeGoalFound_ = false;
    float slimeGoalX_ = 0.0f;
    float slimeGoalY_ = 0.0f;
    bool benchmarkAgentsPacked_ = false;
    int benchmarkPackedLaneIndex_ = -1;
    sf::Vector2f benchmarkPackPoint_{0.0f, 0.0f};
    std::vector<bool> benchmarkAlgorithmEnabled_;  // track which algorithms are enabled (persists across resets)

    // helper methods
    void initializeDisplay();
    void updateAgents();
    void updateTrails();
    void updateAgentsOptimized();
    void updateTrailsOptimized();
    void updateDisplay();
    void setupDisplaySprite();
    void overlayBuffers();
    void updateAgentOverlayTexture();

    void validateSettings();
    void respawnBenchmarkSlime(Agent &agent);
};
