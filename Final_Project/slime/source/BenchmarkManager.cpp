#include "BenchmarkManager.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

// static lists of algorithms for the benchmark
// grouped by "explorers (uninformed) first" then the pathfinders (informed/heuristic)
static const std::vector<SimulationSettings::Algos> BENCHMARK_ALGORITHMS = {
    // uninformed search no goal heuristic obviously
    SimulationSettings::Algos::DFS,      // depth first search (burning wool like effect attempt anyways)
    SimulationSettings::Algos::Dijkstra, // dijkstra octagonal wavefront
    SimulationSettings::Algos::Slime,    // slime mold ("biological intelligence")

    // informed search w/ goal heuristics
    SimulationSettings::Algos::AStar,
    SimulationSettings::Algos::Greedy,
    SimulationSettings::Algos::JPS,
    SimulationSettings::Algos::Theta};

// the colors for each algorithm (MUST MATCH ORDER ABOVE... will screw up the look otherwise)
static const sf::Color ALGO_COLORS[] = {
    sf::Color(255, 69, 0),    // orange-ish red
    sf::Color(70, 130, 180),  // blue steal
    sf::Color(150, 255, 150), // bright green - teal-like

    sf::Color(50, 205, 50),  // lime green
    sf::Color(255, 20, 147), // pink
    sf::Color(0, 255, 255),  // cyan
    sf::Color(255, 255, 0)   // yellow
};

BenchmarkManager::BenchmarkManager()
    : pathfinder_(800, 600, 4)
{
}

const std::vector<SimulationSettings::Algos> &BenchmarkManager::getBenchmarkAlgorithms()
{
    // central accessor for whoever needs the benchmark algorithm list.
    return BENCHMARK_ALGORITHMS;
}

sf::Color BenchmarkManager::getAlgorithmColor(SimulationSettings::Algos algo)
{
    // use the explicit lookup table for enum ordering
    for (size_t i = 0; i < BENCHMARK_ALGORITHMS.size(); i++)
    {
        if (BENCHMARK_ALGORITHMS[i] == algo)
        {
            return ALGO_COLORS[i];
        }
    }
    return sf::Color::White;
}

const char *BenchmarkManager::getAlgorithmName(SimulationSettings::Algos algo)
{
    return SimulationSettings::algoNames(algo);
}

void BenchmarkManager::setupBenchmark(int width, int height, int agentsPerAlgorithm)
{
    // stored the arena bounds and per lane agent budget before configuring subsystems
    width_ = width;
    height_ = height;
    agentsPerAlgorithm_ = agentsPerAlgorithm;

    // resizing pathfinder grid
    pathfinder_.resize(width, height);

    // for the goal on the right side
    goalX_ = width - goalMargin_;
    goalY_ = height / 2.0f;

    // generation of advanced maze based on current type
    regenerateMaze();
    runDoublingExperiment();
    initializeStats();

    reset();
}

void BenchmarkManager::regenerateMaze()
{
    // for empirical doubling. ONLY using the "true maze" (not random obstacles) with controlled complexity
    // which gives us proper maze structure with measurable N (cell count)
    currentMazeType_ = Pathfinder::MazeType::TrueMaze;

    // complexity levels 1-6 based on difficulty (0.0 to 1.0)
    int complexityLevel = 1 + static_cast<int>(mazeDifficulty_ * 5.0f);
    complexityLevel = std::clamp(complexityLevel, 1, 6);

    pathfinder_.generateTrueMaze(complexityLevel);

    // goal placement at the maze exit
    auto [exitX, exitY] = pathfinder_.getMazeExit();
    int cellSize = pathfinder_.getCellSize();
    goalX_ = static_cast<float>(exitX * cellSize + cellSize / 2);
    goalY_ = static_cast<float>(exitY * cellSize + cellSize / 2);
}

void BenchmarkManager::cycleMazeType()
{
    // cycling through complexity levels
    // for the doubling for empirical analysis
    mazeDifficulty_ += 0.2f; // each press will increase by ~1 complexity level
    if (mazeDifficulty_ > 1.0f)
    {
        mazeDifficulty_ = 0.0f; // then wrap back to easiest
    }
}

const char *BenchmarkManager::getMazeTypeName(Pathfinder::MazeType type)
{
    switch (type)
    {
    case Pathfinder::MazeType::TrueMaze:
        return "True Maze";
    default:
        return "True Maze";
    }
}

std::string BenchmarkManager::getComplexityInfo() const
{
    int level = 1 + static_cast<int>(mazeDifficulty_ * 5.0f);
    level = std::clamp(level, 1, 6);
    int cells = pathfinder_.getMazeCellCount();
    return "Level " + std::to_string(level) + " (N=" + std::to_string(cells) + " cells)";
}

void BenchmarkManager::setGoalPosition(float x, float y)
{
    // just an override for the finish line helpful for debugging
    goalX_ = x;
    goalY_ = y;
}

void BenchmarkManager::initializeStats()
{
    // clears old stats so each algorithm starts with fresh timing and arrival counters
    stats_.clear();
    stats_.reserve(BENCHMARK_ALGORITHMS.size());
    
    // initialize all algorithms as enabled
    algorithmEnabled_.assign(BENCHMARK_ALGORITHMS.size(), true);

    for (size_t i = 0; i < BENCHMARK_ALGORITHMS.size(); i++)
    {
        AlgorithmStats stat;
        stat.algorithm = BENCHMARK_ALGORITHMS[i];
        stat.name = getAlgorithmName(BENCHMARK_ALGORITHMS[i]);
        stat.color = ALGO_COLORS[i];
        stat.totalAgents = agentsPerAlgorithm_;
        stats_.push_back(stat);
    }
}

void BenchmarkManager::setAlgorithmEnabled(size_t index, bool enabled) {
    if (index < algorithmEnabled_.size()) {
        algorithmEnabled_[index] = enabled;
    }
}

bool BenchmarkManager::isAlgorithmEnabled(size_t index) const {
    if (index < algorithmEnabled_.size()) {
        return algorithmEnabled_[index];
    }
    return true;  // default to enabled if out of range
}

void BenchmarkManager::updateAgentCounts(int newPerAlgo)
{
    // the little sliders in the HUB that are called mid run
    agentsPerAlgorithm_ = newPerAlgo;
    for (auto &stat : stats_)
    {
        stat.totalAgents = newPerAlgo;
    }
}

void BenchmarkManager::reset()
{
    // clears the timers, rankings, and any other lingering shared search state
    benchmarkActive_ = false;
    benchmarkComplete_ = false;
    benchmarkPaused_ = false;
    totalPausedTimeMs_ = 0.0;
    nextRank_ = 1;
    arrivedAgents_.clear();

    sharedExplorationStates_.clear();

    // inits shared states for the naive explorer algorithms
    GridCell goalCell = pathfinder_.worldToGrid(goalX_, goalY_);
    for (auto algo : BENCHMARK_ALGORITHMS)
    {
        (void)algo; // ...suppresses unused warning
    }

    for (auto &stat : stats_)
    {
        stat.arrivedAgents = 0;
        stat.finished = false;
        stat.firstArrivalTimeMs = -1.0;
        stat.lastArrivalTimeMs = -1.0;
        stat.avgArrivalTimeMs = 0.0;
        stat.rank = 0;
    }
}

SharedExplorationState *BenchmarkManager::getSharedExplorationState(SimulationSettings::Algos algo)
{
    // explorer algos can reuse a shared frontier/visited cache and return it when present.
    auto it = sharedExplorationStates_.find(algo);
    if (it != sharedExplorationStates_.end())
    {
        return &it->second;
    }
    return nullptr;
}

void BenchmarkManager::startBenchmark()
{
    // initializes a fresh run and starts the benchmark timers
    if (!benchmarkActive_)
    {
        benchmarkActive_ = true;
        benchmarkComplete_ = false;
        benchmarkPaused_ = false;
        benchmarkStartTime_ = std::chrono::high_resolution_clock::now();
        totalPausedTimeMs_ = 0.0;
    }
}

void BenchmarkManager::pauseBenchmark()
{
    // for recording when the pause began so elapsed time stays accurate
    if (benchmarkActive_ && !benchmarkPaused_)
    {
        benchmarkPaused_ = true;
        pauseStartTime_ = std::chrono::high_resolution_clock::now();
    }
}

void BenchmarkManager::resumeBenchmark()
{
    // subtracting the paused slice before resuming so the clock remains consistent
    if (benchmarkActive_ && benchmarkPaused_)
    {
        auto now = std::chrono::high_resolution_clock::now();
        totalPausedTimeMs_ += std::chrono::duration<double, std::milli>(now - pauseStartTime_).count();
        benchmarkPaused_ = false;
    }
}

void BenchmarkManager::update(float deltaTime)
{
    (void)deltaTime; // unused for now

    if (!benchmarkActive_ || benchmarkPaused_)
        return;

    // check if the benchmark is complete (ie all algorithms finished)
    bool allFinished = true;
    for (const auto &stat : stats_)
    {
        if (!stat.finished)
        {
            allFinished = false;
            break;
        }
    }

    if (allFinished)
    {
        benchmarkComplete_ = true;
    }
}

void BenchmarkManager::recordArrival(int speciesIndex, int agentId)
{
    if (!benchmarkActive_ || benchmarkPaused_)
        return;
    if (speciesIndex < 0 || speciesIndex >= static_cast<int>(stats_.size()))
        return;

    // check for if already arrived
    auto &arrivedSet = arrivedAgents_[speciesIndex];
    if (arrivedSet.find(agentId) != arrivedSet.end())
    {
        return; // already recorded then
    }
    arrivedSet.insert(agentId);

    auto &stat = stats_[speciesIndex];
    double elapsedMs = getBenchmarkElapsedMs();

    stat.arrivedAgents++;

    if (stat.firstArrivalTimeMs < 0)
    {
        stat.firstArrivalTimeMs = elapsedMs;
    }
    stat.lastArrivalTimeMs = elapsedMs;

    // updating average
    stat.avgArrivalTimeMs = (stat.avgArrivalTimeMs * (stat.arrivedAgents - 1) + elapsedMs) / stat.arrivedAgents;

    // check if it finished
    if (stat.arrivedAgents >= stat.totalAgents && !stat.finished)
    {
        stat.finished = true;
        stat.rank = nextRank_++;
    }
}

AlgorithmStats &BenchmarkManager::getStatsMutable(int speciesIndex)
{
    // HUB needs write access for the live updating secondary info (like avg compute time whatever)
    return stats_[speciesIndex];
}

double BenchmarkManager::getBenchmarkElapsedMs() const
{
    if (!benchmarkActive_)
        return 0.0;

    auto now = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(now - benchmarkStartTime_).count();

    if (benchmarkPaused_)
    {
        elapsed -= std::chrono::duration<double, std::milli>(now - pauseStartTime_).count();
    }

    return elapsed - totalPausedTimeMs_;
}

int BenchmarkManager::getTotalArrivals() const
{
    // aggregate view for the HUD footer - sums every lane
    int total = 0;
    for (const auto &stat : stats_)
    {
        total += stat.arrivedAgents;
    }
    return total;
}

int BenchmarkManager::getTotalAgents() const
{
    // mostly for sanity checks I guess when tweaking per algorithm populations
    int total = 0;
    for (const auto &stat : stats_)
    {
        total += stat.totalAgents;
    }
    return total;
}

std::pair<float, float> BenchmarkManager::getSpawnPosition(int speciesIndex, int agentIndex, int totalAgentsInSpecies) const
{
    // spawn in lanes on the left side
    int numAlgos = static_cast<int>(BENCHMARK_ALGORITHMS.size());
    float laneHeight = static_cast<float>(height_) / numAlgos;

    // center of this species lane
    float laneY = (speciesIndex + 0.5f) * laneHeight;

    // spread agents within the lane
    float spreadY = laneHeight * 0.8f; // and use 80% of lane height
    float agentSpacing = spreadY / std::max(1, totalAgentsInSpecies - 1);

    float y = laneY - spreadY / 2.0f + agentIndex * agentSpacing;
    if (totalAgentsInSpecies == 1)
    {
        y = laneY;
    }

    // clamp y to stay within screen bounds
    y = std::clamp(y, 10.0f, static_cast<float>(height_ - 10));

    // and x position: slightly randomized within spawn margin of course
    float x = spawnMargin_ * 0.5f + (agentIndex % 5) * 5.0f;

    return {x, y};
}

void BenchmarkManager::updateRankings()
{
    // display ordering without mutating the authoritative stats array
    // and sorting by arrival count (descending in this case) then by first arrival time
    std::vector<size_t> indices(stats_.size());
    for (size_t i = 0; i < indices.size(); i++)
        indices[i] = i;

    std::sort(indices.begin(), indices.end(), [this](size_t a, size_t b)
              {
        if (stats_[a].finished != stats_[b].finished) {
            return stats_[a].finished; // finished comes first
        }
        if (stats_[a].finished && stats_[b].finished) {
            return stats_[a].lastArrivalTimeMs < stats_[b].lastArrivalTimeMs;
        }
        if (stats_[a].arrivedAgents != stats_[b].arrivedAgents) {
            return stats_[a].arrivedAgents > stats_[b].arrivedAgents;
        }
        return stats_[a].firstArrivalTimeMs < stats_[b].firstArrivalTimeMs; });
}

std::string BenchmarkManager::estimateBigO(double ratio) const
{
    // empirical doubling method: if T(2N)/T(N) = r then:
    //   r ≈ 1   -> O(1)       (constant: time doesnt change)
    //   r ≈ 1   -> O(log n)   (log(2n)/log(n) -> 1 for large n)
    //   r ≈ 2   -> O(n)       (linear: double input = double time)
    //   r ≈ 2-3 -> O(n log n) (slightly superlinear)
    //   r ≈ 4   -> O(n^2)      (quadratic: (2n)^2 = 4n^2)
    // thresholds are widened to account for measurement noise and cache effects
    if (ratio < 1.2)
        return "O(1)";
    if (ratio < 1.5)
        return "O(log n)";
    if (ratio < 2.5)
        return "O(n)";
    if (ratio < 3.5)
        return "O(n log n)";
    if (ratio < 5.0)
        return "O(n^2)";
    return "O(n^2+)";
}

void BenchmarkManager::runDoublingExperiment()
{
    // rebuilds several mazes to measure how each algorithm scales
    doublingResults_.clear();
    const int trials = 3;

    // and preserves current maze so the live benchmark isnt messed with
    Pathfinder originalPathfinder = pathfinder_;
    float savedDifficulty = mazeDifficulty_;
    float savedGoalX = goalX_;
    float savedGoalY = goalY_;

    std::vector<SimulationSettings::Algos> pathfinders = {
        SimulationSettings::Algos::AStar,
        SimulationSettings::Algos::Greedy,
        SimulationSettings::Algos::JPS,
        SimulationSettings::Algos::Theta};

    for (auto algo : pathfinders)
    {
        // each algorithm will gets six mazes that double in cell count back to back.
        double prevTime = 0.0;
        for (int level = 1; level <= 6; ++level)
        {
            pathfinder_.generateTrueMaze(level);
            auto [exitX, exitY] = pathfinder_.getMazeExit();
            GridCell goal{exitX, exitY};
            GridCell start = pathfinder_.worldToGrid(spawnMargin_ * 0.5f, height_ * 0.5f);
            double totalMs = 0.0;
            for (int t = 0; t < trials; ++t)
            {
                PathResult res = pathfinder_.findPath(algo, start, goal);
                totalMs += res.computeTimeMs;
            }
            double avgMs = totalMs / trials;
            int problemSize = pathfinder_.getMazeCellCount();
            double ratio = (level == 1 || prevTime <= 0.0) ? 0.0 : avgMs / prevTime;
            std::string est = (level == 1) ? "--" : estimateBigO(ratio);
            doublingResults_.push_back({algo, getAlgorithmName(algo), problemSize, avgMs, ratio, est});
            prevTime = avgMs;
        }
    }

    // restoration of maze state
    pathfinder_ = originalPathfinder;
    mazeDifficulty_ = savedDifficulty;
    goalX_ = savedGoalX;
    goalY_ = savedGoalY;
}

void BenchmarkManager::drawObstacles(sf::RenderTarget &target) const
{
    // the actual visualization of every solid cell so when you run it
    // you understand what the solvers navigate behind the scenes more of a novelty than anything
    int cellSize = pathfinder_.getCellSize();

    for (const auto &obs : pathfinder_.getObstacles())
    {
        sf::RectangleShape rect;
        rect.setPosition(sf::Vector2f(
            static_cast<float>(obs.x * cellSize),
            static_cast<float>(obs.y * cellSize)));
        rect.setSize(sf::Vector2f(
            static_cast<float>(obs.width * cellSize),
            static_cast<float>(obs.height * cellSize)));
        rect.setFillColor(sf::Color(80, 80, 80, 200));
        rect.setOutlineColor(sf::Color(120, 120, 120));
        rect.setOutlineThickness(1.0f);
        target.draw(rect);
    }
}

void BenchmarkManager::drawGoal(sf::RenderTarget &target) const
{
    // Ugly bright goal marker so it stays readable through all the bright trail noise
    sf::CircleShape goal(20.0f);
    goal.setPosition(sf::Vector2f(goalX_ - 20.0f, goalY_ - 20.0f));
    goal.setFillColor(sf::Color(255, 215, 0, 200));
    goal.setOutlineColor(sf::Color::White);
    goal.setOutlineThickness(3.0f);
    target.draw(goal);

    sf::CircleShape inner(10.0f);
    inner.setPosition(sf::Vector2f(goalX_ - 10.0f, goalY_ - 10.0f));
    inner.setFillColor(sf::Color(255, 255, 0));
    target.draw(inner);
}

void BenchmarkManager::drawHUD(sf::RenderTarget &target, const sf::Font &font) const
{
    // HUD shows all the timing, standings, doubling results, and control hints
    float hudX = 10.0f;
    float hudY = 10.0f;
    float lineHeight = 22.0f;

    // count enabled algorithms for background sizing
    size_t enabledCount = 0;
    for (size_t i = 0; i < algorithmEnabled_.size(); i++) {
        if (algorithmEnabled_[i]) enabledCount++;
    }
    if (enabledCount == 0) enabledCount = stats_.size();  // fallback if not initialized

    // the background
    sf::RectangleShape bg;
    bg.setPosition(sf::Vector2f(hudX - 5.0f, hudY - 5.0f));
    bg.setSize(sf::Vector2f(320.0f, 30.0f + enabledCount * lineHeight + 60.0f));
    bg.setFillColor(sf::Color(0, 0, 0, 180));
    target.draw(bg);

    sf::Text title(font);
    title.setString("Benchmark");
    title.setCharacterSize(18);
    title.setFillColor(sf::Color::White);
    title.setStyle(sf::Text::Bold);
    title.setPosition(sf::Vector2f(hudX, hudY));
    target.draw(title);

    hudY += 28.0f;

    // elapsed times
    std::ostringstream timeStr;
    timeStr << std::fixed << std::setprecision(1) << "Time: " << getBenchmarkElapsedMs() / 1000.0 << "s";
    if (benchmarkComplete_)
        timeStr << " [COMPLETE]";
    else if (benchmarkPaused_)
        timeStr << " [PAUSED]";
    else if (!benchmarkActive_)
        timeStr << " [READY]";

    sf::Text timeText(font);
    timeText.setString(timeStr.str());
    timeText.setCharacterSize(14);
    timeText.setFillColor(sf::Color(200, 200, 200));
    timeText.setPosition(sf::Vector2f(hudX, hudY));
    target.draw(timeText);

    hudY += 24.0f;

    // create sorted copy for display, filtering out disabled algorithms
    std::vector<std::pair<size_t, const AlgorithmStats*>> sortedWithIndex;
    for (size_t i = 0; i < stats_.size(); i++) {
        if (isAlgorithmEnabled(i)) {
            sortedWithIndex.push_back({i, &stats_[i]});
        }
    }
    std::sort(sortedWithIndex.begin(), sortedWithIndex.end(), 
              [](const auto& a, const auto& b) {
        if (a.second->finished != b.second->finished) return a.second->finished;
        if (a.second->arrivedAgents != b.second->arrivedAgents) 
            return a.second->arrivedAgents > b.second->arrivedAgents;
        return a.second->firstArrivalTimeMs < b.second->firstArrivalTimeMs; 
    });

    // stats for each algorithm
    int displayRank = 1;
    for (const auto& [idx, stat] : sortedWithIndex)
    {
        std::ostringstream ss;

        // ranking indicator
        if (stat->finished)
        {
            ss << "#" << stat->rank << " ";
        }
        else
        {
            ss << "   ";
        }

        // progress bar
        int barWidth = 10;
        int filled = static_cast<int>(stat->getArrivalPercent() / 100.0f * barWidth);
        ss << "[";
        for (int i = 0; i < barWidth; i++)
        {
            ss << (i < filled ? "=" : " ");
        }
        ss << "] ";

        // the name and stats
        ss << std::setw(14) << std::left << stat->name << " ";
        ss << std::setw(4) << std::right << stat->arrivedAgents << "/" << stat->totalAgents;

        if (stat->firstArrivalTimeMs > 0)
        {
            ss << " (" << std::fixed << std::setprecision(1) << stat->firstArrivalTimeMs / 1000.0 << "s)";
        }

        sf::Text line(font);
        line.setString(ss.str());
        line.setCharacterSize(13);
        line.setFillColor(stat->color);
        line.setPosition(sf::Vector2f(hudX, hudY));
        target.draw(line);

        hudY += lineHeight;
        displayRank++;
    }

    // all the complexity info for empirical doubling
    hudY += 8.0f;
    std::ostringstream mazeStr;
    int level = 1 + static_cast<int>(mazeDifficulty_ * 5.0f);
    level = std::clamp(level, 1, 6);
    int cells = pathfinder_.getMazeCellCount();
    mazeStr << "True Maze - Level " << level << " (N = " << cells << " cells)";
    sf::Text mazeText(font);
    mazeText.setString(mazeStr.str());
    mazeText.setCharacterSize(12);
    mazeText.setFillColor(sf::Color(180, 180, 255));
    mazeText.setPosition(sf::Vector2f(hudX, hudY));
    target.draw(mazeText);

    // doubling results summary - draw on RIGHT SIDE of screen
    if (!doublingResults_.empty())
    {
        float rightHudX = 880.0f; // right side of 1200px window
        float rightHudY = 10.0f;

        // background for right panel
        sf::RectangleShape rightBg;
        rightBg.setPosition(sf::Vector2f(rightHudX - 5.0f, rightHudY - 5.0f));
        rightBg.setSize(sf::Vector2f(330.0f, 260.0f));
        rightBg.setFillColor(sf::Color(0, 0, 0, 180));
        target.draw(rightBg);

        sf::Text dblTitle(font);
        dblTitle.setString("Empirical Doubling");
        dblTitle.setCharacterSize(18);
        title.setFillColor(sf::Color::White);
        title.setStyle(sf::Text::Bold);
        dblTitle.setFillColor(sf::Color(200, 200, 200));
        dblTitle.setPosition(sf::Vector2f(rightHudX, rightHudY));
        target.draw(dblTitle);
        rightHudY += 22.0f;

        sf::Text dblsubTitle(font);
        dblsubTitle.setString("(pathfind compute time)");
        dblsubTitle.setCharacterSize(12);
        dblsubTitle.setFillColor(sf::Color(200, 200, 200));
        dblsubTitle.setPosition(sf::Vector2f(rightHudX, rightHudY));
        target.draw(dblsubTitle);
        rightHudY += 16.0f;

        // compute average ratio across all 5 transitions (levels 2-6) for each algorithm
        // results are stored as: [algo1_lvl1, algo1_lvl2, ..., algo1_lvl6, algo2_lvl1, ...]
        std::string lastAlgo;
        for (size_t algoStart = 0; algoStart < doublingResults_.size(); algoStart += 6)
        {
            const auto &dr = doublingResults_[algoStart]; // use first result for name/color
            if (dr.algoName == lastAlgo)
                continue;
            lastAlgo = dr.algoName;
            
            // average the ratios from levels 2-6 (indices 1-5 within this algo's results)
            double sumRatio = 0.0;
            int ratioCount = 0;
            for (size_t i = algoStart + 1; i < algoStart + 6 && i < doublingResults_.size(); i++) {
                if (doublingResults_[i].ratio > 0.0) {
                    sumRatio += doublingResults_[i].ratio;
                    ratioCount++;
                }
            }
            double avgRatio = (ratioCount > 0) ? sumRatio / ratioCount : 0.0;
            std::string avgEstimate = estimateBigO(avgRatio);
            
            std::ostringstream ds;
            ds << std::setw(8) << std::left << dr.algoName
               << " r=" << std::fixed << std::setprecision(2) << avgRatio
               << " " << avgEstimate;
            sf::Text dblLine(font);
            dblLine.setString(ds.str());
            dblLine.setCharacterSize(13);
            dblLine.setFillColor(getAlgorithmColor(dr.algorithm));
            dblLine.setPosition(sf::Vector2f(rightHudX, rightHudY));
            target.draw(dblLine);
            rightHudY += 16.0f;
        }

        // note explaining the approximation
        rightHudY += 6.0f;
        sf::Text noteText(font);
        noteText.setString("N = maze cells (48 -> 108 -> 192 -> 432 -> 768 -> 1728)");
        noteText.setCharacterSize(10);
        noteText.setFillColor(sf::Color(130, 130, 130));
        noteText.setPosition(sf::Vector2f(rightHudX, rightHudY));
        target.draw(noteText);
        rightHudY += 12.0f;
        
        sf::Text noteText2(font);
        noteText2.setString("r = avg T(2N)/T(N) ratio across all 5 doublings");
        noteText2.setCharacterSize(10);
        noteText2.setFillColor(sf::Color(130, 130, 130));
        noteText2.setPosition(sf::Vector2f(rightHudX, rightHudY));
        target.draw(noteText2);
        rightHudY += 16.0f;

        sf::Text note2(font);
        note2.setString("Scaling ~2x avg (1.78x-2.25x) due to pixel grid limits.");
        note2.setCharacterSize(10);
        note2.setFillColor(sf::Color(130, 130, 130));
        note2.setPosition(sf::Vector2f(rightHudX, rightHudY));
        target.draw(note2);
        rightHudY += 12.0f;

        sf::Text note3(font);
        note3.setString("Mazes stress heuristics better than random graphs.");
        note3.setCharacterSize(10);
        note3.setFillColor(sf::Color(130, 130, 130));
        note3.setPosition(sf::Vector2f(rightHudX, rightHudY));
        target.draw(note3);
        rightHudY += 14.0f;

        sf::Text note4(font);
        note4.setString("Computed instantly (not from the race) by calling");
        note4.setCharacterSize(10);
        note4.setFillColor(sf::Color(130, 130, 130));
        note4.setPosition(sf::Vector2f(rightHudX, rightHudY));
        target.draw(note4);
        rightHudY += 12.0f;

        sf::Text note5(font);
        note5.setString("each algorithm directly on maze gen/reload.");
        note5.setCharacterSize(10);
        note5.setFillColor(sf::Color(130, 130, 130));
        note5.setPosition(sf::Vector2f(rightHudX, rightHudY));
        target.draw(note5);
        rightHudY += 14.0f;

        sf::Text note6(font);
        note6.setString("Explorers (DFS/Dijkstra/Slime) not in doubling - no");
        note6.setCharacterSize(10);
        note6.setFillColor(sf::Color(130, 130, 130));
        note6.setPosition(sf::Vector2f(rightHudX, rightHudY));
        target.draw(note6);
        rightHudY += 12.0f;

        sf::Text note7(font);
        note7.setString("path to time; they roam until they find the goal.");
        note7.setCharacterSize(10);
        note7.setFillColor(sf::Color(130, 130, 130));
        note7.setPosition(sf::Vector2f(rightHudX, rightHudY));
        target.draw(note7);
    }

    hudY += 18.0f;
    sf::Text hint(font);
    hint.setString("SPACE: Start | R: Regen | D: Doubling | +/-: Agents");
    hint.setCharacterSize(11);
    hint.setFillColor(sf::Color(150, 150, 150));
    hint.setPosition(sf::Vector2f(hudX, hudY));
    target.draw(hint);
    
    hudY += 14.0f;
    sf::Text hint2(font);
    hint2.setString("P: Pack agents | Shift+1-7: Toggle algorithm");
    hint2.setCharacterSize(11);
    hint2.setFillColor(sf::Color(150, 150, 150));
    hint2.setPosition(sf::Vector2f(hudX, hudY));
    target.draw(hint2);
}
