#include "PhysarumSimulation.h"
#include <cstdlib>
#include "Agent.h"
#include "SpatialGrid.h"
#include "ParallelProcessor.h"
#include "OptimizedTrailMap.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <atomic>
#include <cstdlib>
#include <unordered_map>
#include <random>
#include <map>
#include <numeric>

PhysarumSimulation::PhysarumSimulation(const SimulationSettings &settings)
    : settings_(settings)
{
    validateSettings();

    // initialize trail map with number of species
    int numSpecies = std::max(1, static_cast<int>(settings_.speciesSettings.size()));
    trailMap_ = std::make_unique<TrailMap>(settings_.width, settings_.height, numSpecies);

    // initialize high performance optimization systems
    if (useOptimizedSystems_)
    {
        // spatial grid for o(1) neighbor lookups (cell size = sensor distance for efficiency)
        float cellSize = 50.0f; // default cell size
        if (!settings_.speciesSettings.empty())
        {
            cellSize = std::max(settings_.speciesSettings[0].sensorOffsetDistance, 10.0f);
        }
        spatialGrid_ = std::make_unique<SpatialGrid>(settings_.width, settings_.height);

        // parallel processor for multi threaded updates
        if (useParallelUpdates_)
        {
            unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
            parallelProcessor_ = std::make_unique<ParallelProcessor>(numThreads);
        }

        // optimized trail map for simd operations
        optimizedTrailMap_ = std::make_unique<OptimizedTrailMap>(settings_.width, settings_.height, numSpecies);

        std::cout << "High-performance systems initialized:" << std::endl;
        std::cout << "  Spatial grid cell size: " << cellSize << std::endl;
        if (parallelProcessor_)
        {
            std::cout << "  Parallel threads: " << parallelProcessor_->getThreadCount() << std::endl;
        }
        std::cout << "  Optimized trail map: enabled" << std::endl;
    }

    // initialize agents
    agents_ = AgentFactory::createAgents(settings_);

    // initialize display components
    initializeDisplay();

    // initialize cumulative death tracking per species
    cumulativeDeathsPerSpecies_.resize(settings_.speciesSettings.size(), 0);

    std::cout << "PhysarumSimulation initialized:" << std::endl;
    std::cout << "  Resolution: " << settings_.width << "x" << settings_.height << std::endl;
    std::cout << "  Agents: " << agents_.size() << std::endl;
    std::cout << "  Species: " << settings_.speciesSettings.size() << std::endl;
}

void PhysarumSimulation::update(float deltaTime)
{
    // Handle benchmark mode separately
    if (inBenchmarkMode_) {
        updateTimer_.restart();
        updateBenchmark(deltaTime);
        
        // Update trails for visualization
        trailMap_->diffuse(settings_.diffuseRate);
        trailMap_->decay(settings_.decayRate);
        
        updateDisplay();
        lastUpdateTime_ = updateTimer_.getElapsedTime().asMilliseconds();
        return;
    }
    
    // reset audit counters each frame
    auditSplits_ = 0;
    auditMatingsSame_ = 0;
    auditMatingsCross_ = 0;
    auditRebirths_ = 0;
    auditSpores_ = 0;
    auditDeaths_ = 0;
    updateTimer_.restart();

    // update food pellets (decay over time and remove expired ones)
    for (auto &pellet : foodPellets_)
    {
        pellet.update();
    }
    // remove expired pellets - working???
    foodPellets_.erase(
        std::remove_if(foodPellets_.begin(), foodPellets_.end(),
                       [](const FoodPellet &p)
                       { return p.isExpired(); }),
        foodPellets_.end());

    for (int step = 0; step < settings_.stepsPerFrame; ++step)
    {
        if (useOptimizedSystems_ && spatialGrid_)
        {
            // high performance update path
            updateAgentsOptimized();
            updateTrailsOptimized();
        }
        else
        {
            // legacy update path
            updateAgents();
            updateTrails();
        }
    }

    updateDisplay();



    lastUpdateTime_ = updateTimer_.getElapsedTime().asMilliseconds();
}

void PhysarumSimulation::reset()
{
    // Don't reset during benchmark mode - use resetBenchmark() instead
    if (inBenchmarkMode_) {
        return;
    }
    
    trailMap_->clear();

    // clear optimized systems if enabled
    if (optimizedTrailMap_)
    {
        optimizedTrailMap_->clear();
    }
    if (spatialGrid_)
    {
        spatialGrid_->clear();
    }

    // reset cumulative death counters
    std::fill(cumulativeDeathsPerSpecies_.begin(), cumulativeDeathsPerSpecies_.end(), 0);
    totalCumulativeDeaths_ = 0;

    agents_ = AgentFactory::createAgents(settings_);
    std::cout << "Simulation reset with " << agents_.size() << " agents" << std::endl;
}

void PhysarumSimulation::draw(sf::RenderWindow &window, sf::Font &font)
{
    if (displaySprite_)
        window.draw(*displaySprite_);

    // the benchmark mode for specific drawing
    if (inBenchmarkMode_) {

        benchmarkManager_.drawObstacles(window);
        benchmarkManager_.drawGoal(window);
        
        // reusing shapes to avoid allocation overhead
        static sf::CircleShape head(3.0f);
        static sf::CircleShape trail(2.0f);
        static sf::CircleShape slimeHead(1.5f);  // smaller for slime - more like normal mode
        static sf::ConvexShape slimeBody(3);     // teardrop/directional shape for slime
        static bool initialized = false;
        if (!initialized) {
            head.setOrigin({3.0f, 3.0f});
            trail.setOrigin({2.0f, 2.0f});
            slimeHead.setOrigin({1.5f, 1.5f});
            // teardrop, triangle-ish shape pointing in direction of movement
            slimeBody.setPoint(0, {3.0f, 0.0f});   // front point
            slimeBody.setPoint(1, {-2.0f, 2.0f});  // back left
            slimeBody.setPoint(2, {-2.0f, -2.0f}); // back right
            initialized = true;
        }
        
        // algo colors - again MUST match BenchmarkManager::ALGO_COLORS for trail consistency
        static const sf::Color ALGO_COLORS[] = {
            sf::Color(255, 69, 0),    
            sf::Color(70, 130, 180),    
            sf::Color(150, 255, 150),  
            
            sf::Color(50, 205, 50),    
            sf::Color(255, 20, 147),    
            sf::Color(0, 255, 255),     
            sf::Color(255, 255, 0)     
        };
        
        // drawing agents with simple trails
        for (const Agent& agent : agents_) {
            // debug: for detection out of bounds
            if (agent.position.y >= settings_.height) {
                static int outOfBoundsCount = 0;
                outOfBoundsCount++;
                if (outOfBoundsCount % 1000 == 1) {
                    std::cout << "OUT OF BOUNDS Y: agent=" << agent.agentId 
                              << " algo=" << SimulationSettings::algoNames(agent.assignedAlgo)
                              << " y=" << agent.position.y 
                              << " height=" << settings_.height << std::endl;
                }
            }
            
            // skipping agents outside visible bounds
            if (agent.position.x < 0 || agent.position.x >= settings_.width ||
                agent.position.y < 0 || agent.position.y >= settings_.height) {
                continue;
            }
            
            // color from algorithm (7 algorithms for now)
            sf::Color color = (agent.speciesIndex >= 0 && agent.speciesIndex < 7) 
                ? ALGO_COLORS[agent.speciesIndex] : sf::Color::White;
            
            // dim if reached goal
            if (agent.reachedGoal) {
                color.a = 60;
            }
            
            // special rendering for slime (control) - looks like normal simulation mode
            if (agent.assignedAlgo == SimulationSettings::Algos::Slime) {
                float angleDeg = agent.angle * 180.0f / 3.14159265f;
                slimeBody.setRotation(sf::degrees(angleDeg));
                slimeBody.setPosition({agent.position.x, agent.position.y});
                slimeBody.setFillColor(color);
                window.draw(slimeBody);
            } else {
                // standard rendering for other algorithms (circles with trails)
                float dx = std::cos(agent.angle);
                float dy = std::sin(agent.angle);
                
                sf::Color trailColor = color;
                trailColor.a = 80;
                trail.setPosition({agent.position.x - dx * 8.0f, agent.position.y - dy * 8.0f});
                trail.setFillColor(trailColor);
                window.draw(trail);
                
                // draw agent head
                head.setPosition({agent.position.x, agent.position.y});
                head.setFillColor(color);
                window.draw(head);
            }
        }
        
        // draws the benchmark HUD (unless all ui is hidden)
        if (!hideAllUI_) {
            benchmarkManager_.drawHUD(window, font);
        }
        
        return;  // skip normal hud in benchmark mode
    }

    // skips all ui elements if hideAllUI_ is set (the shift+f toggle)
    if (hideAllUI_) {
        return;
    }

    if (showAgentOverlay_ && agentOverlayTexture_.getSize().x != 0 && agentOverlaySprite_)
    {
        window.draw(*agentOverlaySprite_);
    }

    // hud style label above pellet
    for (const auto &pellet : foodPellets_)
    {
        std::string labelText = pellet.strength > 0 ? "Food Pellet" : "Repel Pellet";

        sf::Text label(font, labelText, 12);
        label.setFillColor(sf::Color::White);

        // get text bounds for background sizing
        sf::FloatRect textBounds = label.getLocalBounds();

        // position label above pellet (centered)
        sf::Vector2f labelPos(pellet.position.x - textBounds.size.x / 2.0f,
                              pellet.position.y - 40.0f); // above the pellet
        label.setPosition(labelPos);

        // draw hud style background rectangle (same style as hud)
        sf::RectangleShape background(sf::Vector2f(textBounds.size.x + 10.0f, textBounds.size.y + 10.0f));
        background.setPosition({labelPos.x - 5.0f, labelPos.y - 5.0f});
        background.setFillColor(sf::Color(0, 0, 0, 120)); // semitransparent black like hud

        window.draw(background);
        window.draw(label);
    }

    // compact audit overlay (up in the top left)
    {
        std::ostringstream oss;
        oss << "splits:" << auditSplits_
            << " same:" << auditMatingsSame_
            << " cross:" << auditMatingsCross_
            << " rebirth:" << auditRebirths_
            << " spores:" << auditSpores_
            << " | TOTAL DEATHS: " << totalCumulativeDeaths_;
        
        // adds the per-species death breakdown - use actual species colors to determine names
        auto getSpeciesName = [](const sf::Color& c) -> std::string {
            //matching by color to get correct name
            if (c.r > 200 && c.g < 120 && c.b < 120) return "Red";
            if (c.r < 120 && c.g > 100 && c.b > 200) return "Blue";
            if (c.r < 120 && c.g > 200 && c.b < 120) return "Green";
            if (c.r > 200 && c.g > 200 && c.b < 120) return "Yellow";
            if (c.r > 200 && c.g < 120 && c.b > 200) return "Magenta";
            return "Species";
        };
        std::vector<std::string> speciesNames;
        for (const auto& sp : settings_.speciesSettings) {
            speciesNames.push_back(getSpeciesName(sp.color));
        }
        std::vector<sf::Color> speciesColors = {
            sf::Color(255, 80, 80), sf::Color(80, 150, 255), sf::Color(80, 255, 80),
            sf::Color(255, 255, 80), sf::Color(255, 80, 255), sf::Color(50, 255, 50),
            sf::Color(200, 0, 0), sf::Color(255, 255, 255)
        };
        
        sf::Text audit(font, oss.str(), 12);
        audit.setFillColor(sf::Color::Cyan);
        audit.setOutlineColor(sf::Color::Black);
        audit.setOutlineThickness(1.0f);
        audit.setPosition({8.0f, 8.0f});
        window.draw(audit);
        
        // draws per species death counts on second line
        std::ostringstream deathOss;
        deathOss << "Deaths: ";
        for (size_t i = 0; i < cumulativeDeathsPerSpecies_.size() && i < speciesNames.size(); ++i) {
            if (i > 0) deathOss << " | ";
            deathOss << speciesNames[i] << ":" << cumulativeDeathsPerSpecies_[i];
        }
        sf::Text deathText(font, deathOss.str(), 11);
        deathText.setFillColor(sf::Color::Yellow);
        deathText.setOutlineColor(sf::Color::Black);
        deathText.setOutlineThickness(1.0f);
        deathText.setPosition({8.0f, 24.0f});
        window.draw(deathText);
    }

    // draw cluster labels if enabled
    if (showClusterLabels_ && !agents_.empty() && !settings_.speciesSettings.empty())
    {
        const int numSpecies = static_cast<int>(settings_.speciesSettings.size());

        // 1) count totals per species to set thresholds
        std::vector<int> totalPerSpecies(numSpecies, 0);
        for (const auto &a : agents_)
        {
            if (a.speciesIndex >= 0 && a.speciesIndex < numSpecies)
            {
                totalPerSpecies[a.speciesIndex]++;
            }
        }

        // 2) grid clustering per species
        struct Cluster
        {
            sf::Vector2f sum;
            int count;
        };
        const float cellSize = 120.0f; // tune for label density
        std::vector<std::unordered_map<long long, Cluster>> clusters(numSpecies);
        auto makeKey = [](int cx, int cy) -> long long
        { return (static_cast<long long>(cx) << 32) ^ (static_cast<unsigned int>(cy)); };

        for (const auto &a : agents_)
        {
            const int s = a.speciesIndex;
            if (s < 0 || s >= numSpecies)
                continue;
            int cx = static_cast<int>(a.position.x / cellSize);
            int cy = static_cast<int>(a.position.y / cellSize);
            long long key = makeKey(cx, cy);
            auto &entry = clusters[s][key];
            entry.sum += a.position;
            entry.count += 1;
        }

        // 3) draw a label for each significant cluster
        for (int s = 0; s < numSpecies; ++s)
        {
            const int total = totalPerSpecies[s];
            if (total <= 0)
                continue;
            // and dynamic threshold to avoid too many labels
            const int minCount = std::max(8, static_cast<int>(std::round(total * 0.03f))); // 3%-ish of species population

            const sf::Color sc = settings_.speciesSettings[s].color;

            for (const auto &kv : clusters[s])
            {
                const Cluster &cl = kv.second;
                if (cl.count < minCount)
                    continue;

                sf::Vector2f centroid = sf::Vector2f(cl.sum.x / cl.count, cl.sum.y / cl.count);

                std::ostringstream l;
                l << "S" << s << ": " << cl.count << " agents";
                sf::Text label(font, l.str(), 13);
                label.setFillColor(sf::Color::White);
                label.setOutlineColor(sf::Color::Black);
                label.setOutlineThickness(1.0f);

                float offset = 12.0f + std::min(60.0f, std::sqrt(static_cast<float>(cl.count)) * 1.1f);
                sf::FloatRect tb = label.getLocalBounds();
                float x = std::clamp(centroid.x - tb.size.x * 0.5f, 4.0f, static_cast<float>(settings_.width) - tb.size.x - 4.0f);
                float y = std::clamp(centroid.y - offset - tb.size.y, 4.0f, static_cast<float>(settings_.height) - tb.size.y - 4.0f);
                label.setPosition({x, y});

                sf::RectangleShape bg;
                bg.setSize(sf::Vector2f(tb.size.x + 10.0f, tb.size.y + 10.0f));
                bg.setPosition({x - 5.0f, y - 5.0f});
                bg.setFillColor(sf::Color(0, 0, 0, 140));
                bg.setOutlineThickness(2.0f);
                bg.setOutlineColor(sf::Color(sc.r, sc.g, sc.b, 180));

                sf::Vertex line[] = {
                    sf::Vertex{{x + tb.size.x * 0.5f, y + tb.size.y + 5.0f}, sf::Color(sc.r, sc.g, sc.b, 180)},
                    sf::Vertex{centroid, sf::Color(sc.r, sc.g, sc.b, 100)}};

                window.draw(bg);
                window.draw(label);
                window.draw(line, 2, sf::PrimitiveType::Lines);
            }
        }
    }

}

void PhysarumSimulation::updateSettings(const SimulationSettings &newSettings)
{
    // In benchmark mode, only allow updating benchmark-specific settings
    if (inBenchmarkMode_) {
        settings_.benchmarkSettings = newSettings.benchmarkSettings;
        return;
    }
    
    bool needsResize = (newSettings.width != settings_.width ||
                        newSettings.height != settings_.height);
    bool needsSpeciesReset = (newSettings.speciesSettings.size() != settings_.speciesSettings.size());

    settings_ = newSettings;
    validateSettings();

    if (needsResize || needsSpeciesReset)
    {
        int numSpecies = std::max(1, static_cast<int>(settings_.speciesSettings.size()));
        trailMap_ = std::make_unique<TrailMap>(settings_.width, settings_.height, numSpecies);

        // resize cumulative death tracking for new species count
        cumulativeDeathsPerSpecies_.resize(numSpecies, 0);

        if (needsResize)
        {
            initializeDisplay();
        }
        
        // only recreate agents if species count changed or dimensions changed
        agents_ = AgentFactory::createAgents(settings_);

        std::cout << "TrailMap recreated for " << numSpecies << " species" << std::endl;
    }
    // do not auto recreate agents for numAgents changes - a user must press the space bar to reset
}

void PhysarumSimulation::setAgentCount(int count)
{
    settings_.numAgents = std::max(1, count);
    agents_ = AgentFactory::createAgents(settings_);
}

void PhysarumSimulation::adjustAgentCount(int delta)
{
    int currentCount = static_cast<int>(agents_.size());
    std::cout << "adjustAgentCount called: delta=" << delta << ", current=" << currentCount << std::endl;
    
    if (delta > 0 && !agents_.empty())
    {
        // adds new agents via cloning existing ones (spawn from existing agents)
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> agentDist(0, agents_.size() - 1);
        std::uniform_real_distribution<float> offsetDist(-5.0f, 5.0f);  // small offset from parent
        std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * M_PI);
        
        size_t originalSize = agents_.size();
        for (int i = 0; i < delta; ++i)
        {
            // random existing agent to spawn from (only from original agents)
            std::uniform_int_distribution<size_t> parentDist(0, originalSize - 1);
            size_t parentIdx = parentDist(gen);
            const Agent& parent = agents_[parentIdx];
            
            // spawns very close to the parent agent
            float x = std::clamp(parent.position.x + offsetDist(gen), 0.0f, static_cast<float>(settings_.width - 1));
            float y = std::clamp(parent.position.y + offsetDist(gen), 0.0f, static_cast<float>(settings_.height - 1));
            float angle = angleDist(gen);
            
            agents_.emplace_back(x, y, angle, parent.speciesIndex);
            agents_.back().setDefaultSpeciesMask(parent.speciesIndex);
            agents_.back().applyGenomeToCachedParams(settings_);  // initialize moveSpeed etc
        }
        settings_.numAgents = static_cast<int>(agents_.size());
        std::cout << "Added " << delta << " agents (total: " << agents_.size() << ")" << std::endl;
    }
    else if (delta < 0 && currentCount > 1)
    {
        // removes agents - but scale to percentage if removing more than 10%
        int toRemove = -delta;
        
        // dont remove more than half at once and keep at least 10
        toRemove = std::min(toRemove, currentCount / 2);
        toRemove = std::min(toRemove, currentCount - 10);
        
        if (toRemove > 0)
        {
            for (int i = 0; i < toRemove; ++i)
            {
                agents_.pop_back();
            }
            settings_.numAgents = static_cast<int>(agents_.size());
            std::cout << "Removed " << toRemove << " agents (total: " << agents_.size() << ")" << std::endl;
        }
        else
        {
            std::cout << "Cannot remove more agents (minimum reached)" << std::endl;
        }
    }
}

// mouse interaction methods implementation
void PhysarumSimulation::depositFood(int x, int y, float amount, int radius)
{
    if (!trailMap_)
        return;

    // convert screen coordinates to trail map coordinates if needed
    // for now just assuming direct mapping

    // deposit food to all species channels (food affects all species)
    for (int species = 0; species < trailMap_->getNumSpecies(); ++species)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            for (int dy = -radius; dy <= radius; ++dy)
            {
                int targetX = x + dx;
                int targetY = y + dy;

                // checking bounds
                if (targetX >= 0 && targetX < settings_.width &&
                    targetY >= 0 && targetY < settings_.height)
                {
                    // calculating distance based falloff
                    float distance = std::sqrt(dx * dx + dy * dy);
                    if (distance <= radius)
                    {
                        float falloff = 1.0f - (distance / radius);
                        float depositAmount = amount * falloff * falloff; // a quadratic falloff for smoother effect

                        trailMap_->deposit(targetX, targetY, depositAmount, species);
                    }
                }
            }
        }
    }
}

void PhysarumSimulation::depositRepellent(int x, int y, float amount, int radius)
{
    if (!trailMap_)
        return;

    // convert screen coordinates to trail map coordinates if needed
    // deposit repellent (negative amount) to all species channels
    for (int species = 0; species < trailMap_->getNumSpecies(); ++species)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            for (int dy = -radius; dy <= radius; ++dy)
            {
                int targetX = x + dx;
                int targetY = y + dy;

                if (targetX >= 0 && targetX < settings_.width &&
                    targetY >= 0 && targetY < settings_.height)
                {
                    float distance = std::sqrt(dx * dx + dy * dy);
                    if (distance <= radius)
                    {
                        float falloff = 1.0f - (distance / radius);
                        float depositAmount = amount * falloff * falloff; // (amount is already negative)

                        trailMap_->deposit(targetX, targetY, depositAmount, species);
                    }
                }
            }
        }
    }
}

// food pellet system implementation
void PhysarumSimulation::addFoodPellet(int x, int y, float strength, float radius, int pelletType)
{
    // clamp position to simulation bounds
    x = std::clamp(x, 0, settings_.width - 1);
    y = std::clamp(y, 0, settings_.height - 1);

    // create mega pellet with massive properties
    float pelletRadius = std::clamp(radius, 200.0f, 800.0f);          // huge range
    float pelletStrength = std::clamp(strength, -10000.0f, 10000.0f); // massive strength

    // much slower decay, pellets will last much longer
    float decayRate = 0.9995f; // almost no decay (was 0.995f)

    foodPellets_.emplace_back(x, y, pelletStrength, pelletRadius, decayRate, pelletType);

    std::cout << "Added MEGA food pellet at (" << x << "," << y << ") strength=" << pelletStrength
              << " radius=" << pelletRadius << " type=" << pelletType << std::endl;
}

void PhysarumSimulation::clearFoodPellets()
{
    foodPellets_.clear();
    std::cout << "Cleared all food pellets" << std::endl;
}

void PhysarumSimulation::initializeDisplay()
{
    // initializtion of image with new size and black fill
    displayImage_ = sf::Image(sf::Vector2u(static_cast<unsigned>(settings_.width),
                         static_cast<unsigned>(settings_.height)),
                         sf::Color::Black);

    displayTexture_ = sf::Texture(displayImage_);
    displayTexture_.setSmooth(true);

    setupDisplaySprite();

    agentOverlayTexture_.resize({static_cast<unsigned>(settings_.width),
                                static_cast<unsigned>(settings_.height)});
    agentOverlayTexture_.setSmooth(true);
    agentOverlaySprite_.emplace(agentOverlayTexture_);
    agentOverlaySprite_->setPosition(displaySprite_->getPosition());
    agentOverlaySprite_->setScale(displaySprite_->getScale());

    overlayBuffers();
}

void PhysarumSimulation::updateAgents()
{
    // check if we have multiple species
    bool isMultiSpecies = settings_.speciesSettings.size() > 1;
    
    // rebuilding spatial grid for O(1) neighbor lookups (mating, energy stealing, etc.)
    if (spatialGrid_)
    {
        spatialGrid_->rebuild(agents_);
    }

    if (isMultiSpecies)
    {
        // track starting population per species for conditional rebirth
        std::vector<int> currentPopPerSpecies(settings_.speciesSettings.size(), 0);
        for (const auto &a : agents_) {
            if (a.speciesIndex >= 0 && a.speciesIndex < static_cast<int>(currentPopPerSpecies.size()))
                ++currentPopPerSpecies[a.speciesIndex];
        }
        
        for (size_t i = 0; i < agents_.size(); ++i)
        {
            Agent &agent = agents_[i];
            if (agent.speciesIndex < 0 || agent.speciesIndex >= static_cast<int>(settings_.speciesSettings.size()))
                continue;
                
            const auto &sp = settings_.speciesSettings[agent.speciesIndex];
            
            agent.senseMultiSpecies(*trailMap_, settings_);

            // pellets completely override normal movement no distance check here!
            if (!foodPellets_.empty())
                agent.moveWithPelletSeeking(settings_, foodPellets_);
            else
                agent.move(settings_);

            agent.depositMultiSpecies(*trailMap_, settings_);
            
            // eat from trail to gain energy 
            if (sp.foodEconomyEnabled)
            {
                int ax = static_cast<int>(agent.position.x) % settings_.width;
                int ay = static_cast<int>(agent.position.y) % settings_.height;
                if (ax < 0) ax += settings_.width;
                if (ay < 0) ay += settings_.height;
                
                float foodEaten = 0.0f;
                
                if (sp.canEatOtherTrails)
                {
                    // the predator (red): "eat" other species trails first
                    foodEaten = trailMap_->eatAnySpecies(ax, ay, agent.speciesIndex, sp.eatRate);
                }
                
                // also eat own species trail (all species do this passively)
                foodEaten += trailMap_->eat(ax, ay, agent.speciesIndex, sp.eatRate);
                
                // convert food to energy
                agent.energy += foodEaten * sp.trailFoodValue;
                
                // movement costs energy
                agent.energy -= sp.movementEnergyCost * agent.moveSpeed;
            }
            
            //  energy mechanics 
            int sameSpeciesNeighbors = 0;
            
            // energy stealing (red territorial) - steal from OTHER species - use spatial grid
            if (sp.canStealEnergy && spatialGrid_)
            {
                float stealRadius2 = sp.energyStealRadius * sp.energyStealRadius;
                auto nearbyIndices = spatialGrid_->getNearbyAgents(agent.position.x, agent.position.y, sp.energyStealRadius);
                for (size_t j : nearbyIndices)
                {
                    if (j == i) continue;
                    Agent &victim = agents_[j];
                    if (victim.speciesIndex == agent.speciesIndex) continue; // don't steal from own kind
                    
                    float dx = victim.position.x - agent.position.x;
                    float dy = victim.position.y - agent.position.y;
                    if (dx*dx + dy*dy < stealRadius2)
                    {
                        // steal energy
                        float stolen = std::min(victim.energy, sp.energyStealRate);
                        victim.energy -= stolen;
                        agent.energy += stolen;
                    }
                }
            }
            
            // energy giving (blue altruistic) - give to other species (use spatial grid)
            if (sp.canGiveEnergy && agent.energy > sp.energyGiveThreshold && spatialGrid_)
            {
                float giveRadius2 = sp.energyGiveRadius * sp.energyGiveRadius;
                auto nearbyIndices = spatialGrid_->getNearbyAgents(agent.position.x, agent.position.y, sp.energyGiveRadius);
                for (size_t j : nearbyIndices)
                {
                    if (j == i) continue;
                    Agent &recipient = agents_[j];
                    if (recipient.speciesIndex == agent.speciesIndex) continue; // dont give to own kind thatd be too nice...
                    
                    float dx = recipient.position.x - agent.position.x;
                    float dy = recipient.position.y - agent.position.y;
                    if (dx*dx + dy*dy < giveRadius2)
                    {
                        // give energy
                        float given = std::min(agent.energy - sp.energyGiveThreshold, sp.energyGiveRate);
                        if (given > 0) {
                            agent.energy -= given;
                            recipient.energy += given;
                        }
                    }
                }
            }
            
            //  NOTE: LEGACY ENERGY SYSTEM (skipped when food economy enabled) 
            if (!sp.foodEconomyEnabled)
            {
                // count same species neighbors for energy gain (use spatial grid)
                if (spatialGrid_)
                {
                    float neighborRadius = 25.0f;
                    float neighborRadius2 = neighborRadius * neighborRadius;
                    auto nearbyIndices = spatialGrid_->getNearbyAgents(agent.position.x, agent.position.y, neighborRadius);
                    for (size_t j : nearbyIndices)
                    {
                        if (j == i || sameSpeciesNeighbors >= 15) break;
                        const Agent &b = agents_[j];
                        if (b.speciesIndex != agent.speciesIndex) continue;
                        float dx = b.position.x - agent.position.x;
                        float dy = b.position.y - agent.position.y;
                        if (dx*dx + dy*dy < neighborRadius2)
                            ++sameSpeciesNeighbors;
                    }
                }
                
                // energy gain from same species neighbors
                float energyGain = static_cast<float>(sameSpeciesNeighbors) * sp.energyGainPerNeighbor;
                agent.energy += energyGain;
                
                // passive energy regen (for loners who can't cluster)
                agent.energy += sp.passiveEnergyRegen;
            }
            
            agent.energy = std::min(agent.energy, 1.0f);  // cap energy at 1.0 (tighter budget)
            
            //  pre death budding (green loner) 
            // triggers when energy is low but not dead yet - last chance to reproduce
            if (sp.preDeathBuddingEnabled && agent.energy <= sp.preDeathBudThreshold && agent.energy > 0.05f)
            {
                // about to die - bud off a child with all remaining energy!
                float ang = agent.angle + ((std::rand() / (float)RAND_MAX) - 0.5f) * 1.0f;
                float offsetDist = 5.0f + (std::rand() / (float)RAND_MAX) * 10.0f;
                float childX = agent.position.x + std::cos(ang) * offsetDist;
                float childY = agent.position.y + std::sin(ang) * offsetDist;
                
                Agent child(childX, childY, ang, agent.speciesIndex);
                child.hasGenome = true;
                // mutate the child (metal)
                child.genome.moveSpeedScale = std::clamp(agent.genome.moveSpeedScale * (0.9f + 0.2f * (std::rand() / (float)RAND_MAX)), 0.5f, 1.5f);
                child.genome.turnSpeedScale = std::clamp(agent.genome.turnSpeedScale * (0.9f + 0.2f * (std::rand() / (float)RAND_MAX)), 0.5f, 1.5f);
                child.genome.sensorAngleScale = std::clamp(agent.genome.sensorAngleScale * (0.9f + 0.2f * (std::rand() / (float)RAND_MAX)), 0.5f, 1.5f);
                
                // child gets a fresh start with good energy
                child.energy = sp.offspringEnergy;  // use configured offspring energy
                child.ageSeconds = 0.0f;            // fresh start
                agent.energy = 0.01f;               // parent is now dying
                
                child.applyGenomeToCachedParams(settings_);
                agents_.push_back(child);
                ++auditSplits_;  // count as a split
            }
            
            {
                auto le = agent.updateEnergyAndState(settings_);
                if (le == Agent::LifeEvent::Rebirth)
                {
                    ++auditRebirths_;
                }
            }
        }
    }
    else
    {
        // use legacy single species methods with mega pellet override
        float *trailData = trailMap_->getData();
        for (auto &agent : agents_)
        {
            agent.sense(trailData, settings_.width, settings_.height, settings_);

            // pellets completely override normal movement no distance check
            if (!foodPellets_.empty())
                agent.moveWithPelletSeeking(settings_, foodPellets_);
            else
                agent.move(settings_);

            agent.deposit(trailData, settings_.width, settings_.height, settings_);
            {
                auto le = agent.updateEnergyAndState(settings_);
                if (le == Agent::LifeEvent::Rebirth)
                {
                    ++auditRebirths_;
                }
            }
        }
    }

    // handle deaths and spore bursts (legacy path)
    {
        //counts the current population per species for conditional rebirth
        std::vector<int> currentPopPerSpecies(settings_.speciesSettings.size(), 0);
        for (const auto &a : agents_) {
            if (a.speciesIndex >= 0 && a.speciesIndex < static_cast<int>(currentPopPerSpecies.size()))
                ++currentPopPerSpecies[a.speciesIndex];
        }
        
        // calculates starting population per species (estimate from numAgents / species count)
        int startingPopPerSpecies = settings_.numAgents / std::max(1, static_cast<int>(settings_.speciesSettings.size()));
        
        std::vector<size_t> toRemove;
        toRemove.reserve(64);
        for (size_t i = 0; i < agents_.size(); ++i)
        {
            Agent &a = agents_[i];
            if (a.speciesIndex < 0 || a.speciesIndex >= static_cast<int>(settings_.speciesSettings.size()))
                continue;
            const auto &sp = settings_.speciesSettings[a.speciesIndex];
            using DB = SimulationSettings::SpeciesSettings::DeathBehavior;
            
            // check if expired
            // death only from starvation not lifespan timer
            bool expired;
            if (sp.foodEconomyEnabled)
            {
                expired = (a.energy <= 0.0f);  // only die from starvation
            }
            else
            {
                expired = (a.ageSeconds > sp.lifespanSeconds || a.energy <= 0.05f);
            }
            if (!expired)
                continue;
            
            //  CONDITIONAL REBIRTH CHECK (red territorial emergency backup) 
            if (sp.conditionalRebirthEnabled)
            {
                float currentPop = static_cast<float>(currentPopPerSpecies[a.speciesIndex]);
                float threshold = startingPopPerSpecies * sp.rebirthPopulationThreshold;
                
                if (currentPop < threshold)
                {
                    // population struggling - rebirth
                    a.hasGenome = true;
                    a.genome.moveSpeedScale = std::clamp(a.genome.moveSpeedScale * (0.95f + 0.1f * (std::rand() / (float)RAND_MAX)), 0.5f, 1.5f);
                    a.genome.turnSpeedScale = std::clamp(a.genome.turnSpeedScale * (0.95f + 0.1f * (std::rand() / (float)RAND_MAX)), 0.5f, 1.5f);
                    a.energy = sp.rebirthEnergy;
                    a.ageSeconds = 0.0f;
                    a.applyGenomeToCachedParams(settings_);
                    ++auditRebirths_;
                    continue;  // phoenix
                }
            }
                
            if (sp.deathBehavior == DB::HardDeath)
            {
                toRemove.push_back(i);
                ++auditDeaths_;
                if (a.speciesIndex >= 0 && a.speciesIndex < static_cast<int>(cumulativeDeathsPerSpecies_.size())) {
                    ++cumulativeDeathsPerSpecies_[a.speciesIndex];
                    ++totalCumulativeDeaths_;
                }
            }
            else if (sp.deathBehavior == DB::Rebirth && sp.rebirthEnabled)
            {
                // normal rebirth (non conditional) - handled in updateEnergyAndState
                // however if we got here it means updateEnergyAndState didnt rebirth
                // this shouldnt happen good to handle it anyway
                a.energy = sp.rebirthEnergy;
                a.ageSeconds = 0.0f;
                ++auditRebirths_;
            }
            else if (sp.deathBehavior == DB::SporeBurst)
            {
                // spawn spores around current position with full genome mutation
                int created = 0;
                auto mutateSpore = [&](float parentVal) -> float {
                    float delta = ((std::rand() / (float)RAND_MAX) - 0.5f) * 2.0f * sp.sporeMutationRate;
                    return std::clamp(parentVal * (1.0f + delta), 0.5f, 1.5f);
                };
                
                for (int s = 0; s < std::max(0, sp.sporeCount); ++s)
                {
                    float ang = 2.0f * 3.14159265f * (std::rand() / (float)RAND_MAX);
                    float r = sp.sporeRadius * (std::rand() / (float)RAND_MAX);
                    sf::Vector2f pos(a.position.x + std::cos(ang) * r, a.position.y + std::sin(ang) * r);
                    Agent child(pos.x, pos.y, ang, a.speciesIndex);
                    child.hasGenome = true;
                    
                    // applying mutation to ALL genome traits (not just 2)
                    child.genome.moveSpeedScale = mutateSpore(a.genome.moveSpeedScale);
                    child.genome.turnSpeedScale = mutateSpore(a.genome.turnSpeedScale);
                    child.genome.sensorAngleScale = mutateSpore(a.genome.sensorAngleScale);
                    child.genome.sensorDistScale = mutateSpore(a.genome.sensorDistScale);
                    child.genome.alignWScale = mutateSpore(a.genome.alignWScale);
                    child.genome.cohWScale = mutateSpore(a.genome.cohWScale);
                    child.genome.sepWScale = mutateSpore(a.genome.sepWScale);
                    child.genome.oscStrengthScale = mutateSpore(a.genome.oscStrengthScale);
                    child.genome.oscFreqScale = mutateSpore(a.genome.oscFreqScale);
                    
                    child.energy = sp.sporeEnergy;
                    child.applyGenomeToCachedParams(settings_);
                    agents_.push_back(child);
                    ++created;
                }
                auditSpores_ += created;
                toRemove.push_back(i);
                ++auditDeaths_;
                // tracks cumulative per species deaths for spore burst too
                if (a.speciesIndex >= 0 && a.speciesIndex < static_cast<int>(cumulativeDeathsPerSpecies_.size())) {
                    ++cumulativeDeathsPerSpecies_[a.speciesIndex];
                    ++totalCumulativeDeaths_;
                }
            }
            // rebirth is handled in place by updateEnergyAndState already
        }
        // OPTIMIZED REMOVAL: uses swap and pop instead of erase (O(1) vs O(n) per removal)
        if (!toRemove.empty())
        {
            std::sort(toRemove.begin(), toRemove.end(), std::greater<size_t>()); // sort descending
            toRemove.erase(std::unique(toRemove.begin(), toRemove.end()), toRemove.end());
            for (size_t idx : toRemove)
            {
                if (idx < agents_.size())
                {
                    // wwap with last element then pop - O(1)
                    std::swap(agents_[idx], agents_.back());
                    agents_.pop_back();
                }
            }
        }
    }

    // lightweight reproduction pass (legacy path)
    // global pop cap - prevent runaway growth
    const size_t maxPopulation = static_cast<size_t>(settings_.numAgents * 2);  // cap at 2x starting population
    if (agents_.size() >= maxPopulation) {
        // population at cap - no reproduction allowed
        static int capWarnCounter = 0;
        if (++capWarnCounter % 300 == 0) {
            std::cout << "Population at cap (" << agents_.size() << "/" << maxPopulation << ") - reproduction paused" << std::endl;
        }
        return;  // skipping reproduction entirely
    }
    
    // strict birth limit - scale down as population grows toward cap
    float populationRatio = static_cast<float>(agents_.size()) / static_cast<float>(maxPopulation);
    size_t baseOffspring = 50;  // base births per frame
    size_t maxOffspring = static_cast<size_t>(baseOffspring * (1.0f - populationRatio * 0.9f));  // reduce births as pop grows
    maxOffspring = std::max<size_t>(5, std::min<size_t>(maxOffspring, 100));  // clamp to 5-100
    size_t born = 0;
    
    // debug: counts agents per species and their energy states
    static int debugCounter = 0;
    if (++debugCounter % 300 == 0) { // every 5 seconds at 60fps
        std::cerr << "\n [DEBUG] Species status " << std::endl;
        for (int speciesIdx = 0; speciesIdx < static_cast<int>(settings_.speciesSettings.size()); ++speciesIdx) {
            const auto& sp = settings_.speciesSettings[speciesIdx];
            int count = 0;
            float totalEnergy = 0;
            int canSplit = 0;
            int onCooldown = 0;
            for (const auto& a : agents_) {
                if (a.speciesIndex == speciesIdx) {
                    count++;
                    totalEnergy += a.energy;
                    if (sp.splittingEnabled && a.energy > sp.splitEnergyThreshold) {
                        if (a.splitCooldown <= 0.0f) canSplit++;
                        else onCooldown++;
                    }
                }
            }
            float avgE = count > 0 ? totalEnergy / count : 0;
            std::cerr << "  Species " << speciesIdx << ": " << count << " agents"
                      << ", avgEnergy=" << avgE 
                      << ", splitThreshold=" << sp.splitEnergyThreshold
                      << ", canSplit=" << canSplit 
                      << ", onCooldown=" << onCooldown
                      << ", splittingEnabled=" << sp.splittingEnabled
                      << std::endl;
        }
        std::cerr << "  BIRTHS this check: splits=" << auditSplits_ << " matingSame=" << auditMatingsSame_ << std::endl;
    }
    
    for (size_t i = 0; i < agents_.size() && born < maxOffspring; ++i)
    {
        Agent &a = agents_[i];
        if (a.speciesIndex < 0 || a.speciesIndex >= static_cast<int>(settings_.speciesSettings.size()))
            continue;
        const auto &sp = settings_.speciesSettings[a.speciesIndex];
        // asexual split / pre death budding (hydra style)
        // can trigger either from high energy OR from being near end of life
        if (sp.splittingEnabled && a.splitCooldown <= 0.0f)
        {
            bool energyTriggered = (a.energy > sp.splitEnergyThreshold);
            bool nearDeath = (a.ageSeconds > sp.lifespanSeconds * 0.6f);  // 60%+ through lifespan
            
            // pre death budding: even with lower energy can bud near end of life
            // much lower energy requirement - just need some energy...
            bool canBud = energyTriggered || (nearDeath && a.energy > 0.15f);
            
            if (canBud)
            {
                Agent child = Agent::createOffspring(a, a, settings_);
                
                // if near death budding parent loses more energy and ages faster
                if (nearDeath && !energyTriggered) {
                    child.energy = a.energy * 0.4f;  // child gets 40% of parents remaining energy :)
                    a.energy *= 0.3f;                // parent keeps only 30%
                    a.ageSeconds += sp.lifespanSeconds * 0.1f;  // parent ages 10% faster (accelerates death)
                } else {
                    child.energy = sp.offspringEnergy;
                    a.energy -= sp.matingEnergyCost;
                }
                
                a.splitCooldown = sp.splitCooldownTime;
                agents_.push_back(child);
                ++born;
                ++auditSplits_;
                continue;
            }
        }
        // mating (TODO use SPATIAL GRID for O(1) neighbor lookup instead of O(n) search)
        if (sp.matingEnabled && a.mateCooldown <= 0.0f && a.energy > sp.matingEnergyCost)
        {
            float r = sp.matingRadius;
            float r2 = r * r;
            
            // use spatial grid for nearby agents - O(1) instead of O(n)...
            std::vector<size_t> nearbyIndices;
            if (spatialGrid_)
            {
                nearbyIndices = spatialGrid_->getNearbyAgents(a.position.x, a.position.y, r);
            }
            
            for (size_t j : nearbyIndices)
            {
                if (j == i || born >= maxOffspring) continue;
                Agent &b = agents_[j];
                if (b.speciesIndex < 0 || b.speciesIndex >= static_cast<int>(settings_.speciesSettings.size()))
                    continue;
                    
                // quick distance check (spatial grid uses cells so still need precise check)
                float dx = b.position.x - a.position.x;
                float dy = b.position.y - a.position.y;
                float d2 = dx * dx + dy * dy;
                if (d2 > r2) continue;  // Too far
                
                const auto &spb = settings_.speciesSettings[b.speciesIndex];
                
                // blue behavior: can only mate with other species
                if (sp.onlyMateWithOtherSpecies && b.speciesIndex == a.speciesIndex)
                    continue;  // skip same species
                
                // cross species check 
                if (b.speciesIndex != a.speciesIndex && !(sp.crossSpeciesMating && spb.crossSpeciesMating))
                    continue;
                    
                // partner needs energy and no cooldown
                if (b.energy <= spb.matingEnergyCost || b.mateCooldown > 0.0f)
                    continue;
                
                // success thus create offspring
                Agent child = Agent::createOffspring(a, b, settings_);
                agents_.push_back(child);
                a.energy -= sp.matingEnergyCost;
                b.energy -= spb.matingEnergyCost;
                
                // mating energy bonus (blue gets energy from successful mating)
                a.energy += sp.matingEnergyBonus;
                b.energy += spb.matingEnergyBonus;
                
                a.mateCooldown = 2.0f;  // 2 second cooldown
                b.mateCooldown = 2.0f;
                ++born;
                if (a.speciesIndex == b.speciesIndex)
                    ++auditMatingsSame_;
                else
                    ++auditMatingsCross_;
                break;  // only one mate per agent per frame
            }
        }
    }
}

void PhysarumSimulation::updateTrails()
{
    trailMap_->diffuse(settings_.diffuseRate);
    trailMap_->decay(settings_.decayRate);

    // apply blur only every few frames to reduce performance impact
    static int blurCounter = 0;
    if (settings_.blurEnabled && (++blurCounter % 2 == 0))
    {
        trailMap_->applyBlur();
    }
}

void PhysarumSimulation::updateDisplay()
{
    static const sf::Color BENCHMARK_COLORS[] = {
        sf::Color(255, 69, 0),      
        sf::Color(70, 130, 180),    
        sf::Color(150, 255, 150),  
        
        sf::Color(50, 205, 50),  
        sf::Color(255, 20, 147),   
        sf::Color(0, 255, 255),    
        sf::Color(255, 255, 0)    
    };

    if (inBenchmarkMode_) {
        std::vector<sf::Color> benchmarkColors(BENCHMARK_COLORS, BENCHMARK_COLORS + 7);
        trailMap_->updateMultiSpeciesTexture(displayImage_, settings_.displayThreshold, benchmarkColors);
    }
    // check for if we have multiple species
    else if (settings_.speciesSettings.size() > 1)
    {
        // multi species display with color blending
        std::vector<sf::Color> speciesColors;
        for (const auto &speciesSettings : settings_.speciesSettings)
        {
            speciesColors.push_back(speciesSettings.color);
        }
        trailMap_->updateMultiSpeciesTexture(displayImage_, settings_.displayThreshold, speciesColors);
    }
    else
    {
        // use single species display
        sf::Color baseColor = settings_.speciesSettings.empty() ? sf::Color(255, 230, 0) : settings_.speciesSettings[0].color;
        trailMap_->updateTexture(displayImage_, settings_.displayThreshold, baseColor);
    }

    // draws food pellets as subtle blurry circles
    for (const auto &pellet : foodPellets_)
    {
        // small reasonable visual radius
        int radius = 30; // fixed small radius for visibility
        int centerX = static_cast<int>(pellet.position.x);
        int centerY = static_cast<int>(pellet.position.y);

        // opaque colors: dark red for attractive, darker red for repulsive
        sf::Color coreColor = pellet.strength > 0 ? sf::Color(120, 30, 30, 200) : // dark red for attractive
                                  sf::Color(80, 20, 20, 180);                     // darker red for repulsive

        // draw circle with soft blurry edges
        for (int y = -radius; y <= radius; ++y)
        {
            for (int x = -radius; x <= radius; ++x)
            {
                float distance = std::sqrt(x * x + y * y);
                if (distance <= radius)
                {
                    int px = centerX + x;
                    int py = centerY + y;
                    if (px >= 0 && px < static_cast<int>(displayImage_.getSize().x) &&
                        py >= 0 && py < static_cast<int>(displayImage_.getSize().y))
                    {
                        // create soft falloff for blurry edges
                        float falloff = 1.0f - (distance / radius);
                        falloff = falloff * falloff; 

                        // blurcolor based on falloff and corecolor
                        sf::Color blurColor(
                            static_cast<std::uint8_t>(coreColor.r * falloff),
                            static_cast<std::uint8_t>(coreColor.g * falloff),
                            static_cast<std::uint8_t>(coreColor.b * falloff),
                            static_cast<std::uint8_t>(coreColor.a * falloff));

                        // gentle blend with existing pixel
                        sf::Color existing = displayImage_.getPixel(sf::Vector2u(px, py));
                        sf::Color blended(
                            static_cast<std::uint8_t>(std::min(255, static_cast<int>(existing.r) + static_cast<int>(blurColor.r))),
                            static_cast<std::uint8_t>(std::min(255, static_cast<int>(existing.g) + static_cast<int>(blurColor.g))),
                            static_cast<std::uint8_t>(std::min(255, static_cast<int>(existing.b) + static_cast<int>(blurColor.b))),
                            static_cast<std::uint8_t>(255));
                        displayImage_.setPixel(sf::Vector2u(px, py), blended);
                    }
                }
            }
        }
    }

    // --- cpu slime shading post-process ---
    // convert displayImage_ to a shaded look by computing approximate normals
    // via central differences on the luminance and applying simple lighting.
    const unsigned w = displayImage_.getSize().x;
    const unsigned h = displayImage_.getSize().y;
    if (settings_.slimeShadingEnabled && w > 2 && h > 2)
    {
        sf::Image shaded(sf::Vector2u(w, h), sf::Color::Black);

        auto luminance = [&](const sf::Color &c) -> float
        {
            // perceived luminance
            return 0.2126f * (c.r / 255.0f) + 0.7152f * (c.g / 255.0f) + 0.0722f * (c.b / 255.0f);
        };

        // lighting params
        const sf::Vector3f lightDir = sf::Vector3f(0.35f, -0.55f, 0.75f); // tilted key light
        const float lightLen = std::sqrt(lightDir.x * lightDir.x + lightDir.y * lightDir.y + lightDir.z * lightDir.z);
        const sf::Vector3f L = sf::Vector3f(lightDir.x / lightLen, lightDir.y / lightLen, lightDir.z / lightLen);
        const float specPower = 24.0f;
        const float specStrength = 0.55f;
        const float rimStrength = 0.35f;
        const float normalScale = 1.5f; // exaggerate bumps

        // camera/view direction (screen space z)
        const sf::Vector3f V(0.0f, 0.0f, 1.0f);

        // iterate excluding 1 pixel border for central differences
        for (unsigned y = 1; y < h - 1; ++y)
        {
            for (unsigned x = 1; x < w - 1; ++x)
            {
                // gathers the neighborhood luminance (treat brighter as higher surface)
                // base luminance (unused currently, kept for future tuning)

                // const float clum = luminance(displayimage_.getpixel(sf::Vector2u(x, y)));
                const float l = luminance(displayImage_.getPixel(sf::Vector2u(x - 1, y)));
                const float r = luminance(displayImage_.getPixel(sf::Vector2u(x + 1, y)));
                const float u = luminance(displayImage_.getPixel(sf::Vector2u(x, y - 1)));
                const float d = luminance(displayImage_.getPixel(sf::Vector2u(x, y + 1)));

                // central differences (sobel lite)
                float dx = (r - l) * 0.5f;
                float dy = (d - u) * 0.5f;

                // construct normal; z scaled by normalscale to accentuate
                sf::Vector3f N(-dx * normalScale, -dy * normalScale, 1.0f);
                float nlen = std::max(1e-5f, std::sqrt(N.x * N.x + N.y * N.y + N.z * N.z));
                N.x /= nlen;
                N.y /= nlen;
                N.z /= nlen;

                // base albedo from original color with slight saturation boost
                sf::Color src = displayImage_.getPixel(sf::Vector2u(x, y));
                float rC = src.r / 255.0f, gC = src.g / 255.0f, bC = src.b / 255.0f;
                float mean = (rC + gC + bC) / 3.0f;
                rC = std::clamp(mean + (rC - mean) * 1.15f, 0.0f, 1.0f);
                gC = std::clamp(mean + (gC - mean) * 1.15f, 0.0f, 1.0f);
                bC = std::clamp(mean + (bC - mean) * 1.15f, 0.0f, 1.0f);

                // lambert + specular
                float NdotL = std::max(0.0f, N.x * L.x + N.y * L.y + N.z * L.z);
                // half vector specular
                sf::Vector3f H(L.x + V.x, L.y + V.y, L.z + V.z);
                float hlen = std::max(1e-5f, std::sqrt(H.x * H.x + H.y * H.y + H.z * H.z));
                H.x /= hlen;
                H.y /= hlen;
                H.z /= hlen;
                float NdotH = std::max(0.0f, N.x * H.x + N.y * H.y + N.z * H.z);
                float spec = std::pow(NdotH, specPower) * specStrength;

                // rim lighting (attempt to give a gooey edge)
                float rim = std::pow(1.0f - std::max(0.0f, N.x * V.x + N.y * V.y + N.z * V.z), 2.0f) * rimStrength;

                float shade = 0.15f + 0.85f * NdotL; // ambient + diffuse
                float outR = std::clamp(rC * shade + spec + rim * 0.2f, 0.0f, 1.0f);
                float outG = std::clamp(gC * shade + spec + rim * 0.2f, 0.0f, 1.0f);
                float outB = std::clamp(bC * shade + spec + rim * 0.2f, 0.0f, 1.0f);

                shaded.setPixel(sf::Vector2u(x, y), sf::Color(static_cast<std::uint8_t>(outR * 255.0f), static_cast<std::uint8_t>(outG * 255.0f), static_cast<std::uint8_t>(outB * 255.0f)));
            }
        }
        displayTexture_.update(shaded);
    }
    else
    {
        displayTexture_.update(displayImage_);
    }

    updateAgentOverlayTexture();
}

void PhysarumSimulation::setupDisplaySprite()
{
    displaySprite_.emplace(displayTexture_);
    displaySprite_->setPosition(sf::Vector2f(0.0f, 0.0f));

    // scale sprite to fill the target display area if needed
    // for now just assumes 1:1 mapping
    displaySprite_->setScale(sf::Vector2f(1.0f, 1.0f));

    agentOverlaySprite_->setPosition(displaySprite_->getPosition());
    agentOverlaySprite_->setScale(displaySprite_->getScale());
}

void PhysarumSimulation::overlayBuffers()
{
    const size_t pixelCount = static_cast<size_t>(settings_.width) * static_cast<size_t>(settings_.height);
    if (pixelCount == 0)
        return;

    overlayPixelBuffer_.assign(pixelCount * 4, 0);

    if (agentOverlayTexture_.getSize().x != static_cast<unsigned>(settings_.width) ||
        agentOverlayTexture_.getSize().y != static_cast<unsigned>(settings_.height))
    {
        agentOverlayTexture_.resize({static_cast<unsigned>(settings_.width),
                                    static_cast<unsigned>(settings_.height)});
        agentOverlayTexture_.setSmooth(true);
    }
    agentOverlaySprite_.emplace(agentOverlayTexture_);
    agentOverlaySprite_->setPosition(displaySprite_->getPosition());
    agentOverlaySprite_->setScale(displaySprite_->getScale());
}

void PhysarumSimulation::updateAgentOverlayTexture()
{
    if (!showAgentOverlay_)
        return;

    const int width = settings_.width;
    const int height = settings_.height;
    const size_t bufferSize = static_cast<size_t>(width) * height * 4;

    if (overlayPixelBuffer_.size() != bufferSize)
        overlayPixelBuffer_.assign(bufferSize, 0);

    // clear to transparent
    std::fill(overlayPixelBuffer_.begin(), overlayPixelBuffer_.end(), 0);

    // render ALL agents - no skipping, no stride
    for (const Agent &agent : agents_)
    {
        int x = static_cast<int>(agent.position.x);
        int y = static_cast<int>(agent.position.y);
        
        if (x < 0 || x >= width || y < 0 || y >= height)
            continue;

        // get species color
        sf::Color color(200, 200, 200); // default gray
        if (!settings_.speciesSettings.empty() && agent.speciesIndex >= 0 &&
            agent.speciesIndex < static_cast<int>(settings_.speciesSettings.size()))
        {
            color = settings_.speciesSettings[agent.speciesIndex].color;
        }

        // draw single pixel per agent
        size_t idx = (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4;
        overlayPixelBuffer_[idx + 0] = color.r;
        overlayPixelBuffer_[idx + 1] = color.g;
        overlayPixelBuffer_[idx + 2] = color.b;
        overlayPixelBuffer_[idx + 3] = 255;
    }

    agentOverlayTexture_.update(overlayPixelBuffer_.data());
    agentOverlaySprite_->setTexture(agentOverlayTexture_);
    agentOverlaySprite_->setPosition(displaySprite_->getPosition());
    agentOverlaySprite_->setScale(displaySprite_->getScale());
}

void PhysarumSimulation::validateSettings()
{
    settings_.validateAndClamp();
}

void PhysarumSimulation::updateAgentsOptimized()
{
    // update spatial grid with current agent positions
    spatialGrid_->clear();
    for (size_t i = 0; i < agents_.size(); ++i)
    {
        spatialGrid_->insertAgent(i, agents_[i].position.x, agents_[i].position.y);
    }

    // a check if we have multiple species
    bool isMultiSpecies = settings_.speciesSettings.size() > 1;

    if (parallelProcessor_ && useParallelUpdates_)
    {
        // track rebirths in parallel safely
        std::atomic<uint64_t> localRebirths{0};
        // parallel agent updates using the optimized systems
        if (isMultiSpecies)
        {
            parallelProcessor_->processAgentsParallel(agents_, [this, &localRebirths](Agent &agent)
                                                      {
                // use spatial grid for an ultra fast neighbor sensing
                agent.senseWithSpatialGrid(*spatialGrid_, agents_, *optimizedTrailMap_, settings_);
                agent.move(settings_);
                agent.depositOptimized(*optimizedTrailMap_, settings_);
                auto le = agent.updateEnergyAndState(settings_);
                if (le == Agent::LifeEvent::Rebirth) {
                    localRebirths.fetch_add(1, std::memory_order_relaxed);
                } });
        }
        else
        {
            parallelProcessor_->processAgentsParallel(agents_, [this, &localRebirths](Agent &agent)
                                                      {
                agent.senseWithSpatialGrid(*spatialGrid_, agents_, *optimizedTrailMap_, settings_);
                agent.move(settings_);
                agent.depositOptimized(*optimizedTrailMap_, settings_);
                auto le = agent.updateEnergyAndState(settings_);
                if (le == Agent::LifeEvent::Rebirth) {
                    localRebirths.fetch_add(1, std::memory_order_relaxed);
                } });
        }
        auditRebirths_ += localRebirths.load(std::memory_order_relaxed);
    }
    else
    {
        // serial optimized updates
        if (isMultiSpecies)
        {
            for (auto &agent : agents_)
            {
                agent.senseWithSpatialGrid(*spatialGrid_, agents_, *optimizedTrailMap_, settings_);
                agent.move(settings_);
                agent.depositOptimized(*optimizedTrailMap_, settings_);
                {
                    auto le = agent.updateEnergyAndState(settings_);
                    if (le == Agent::LifeEvent::Rebirth)
                        ++auditRebirths_;
                }
            }
        }
        else
        {
            for (auto &agent : agents_)
            {
                agent.senseWithSpatialGrid(*spatialGrid_, agents_, *optimizedTrailMap_, settings_);
                agent.move(settings_);
                agent.depositOptimized(*optimizedTrailMap_, settings_);
                {
                    auto le = agent.updateEnergyAndState(settings_);
                    if (le == Agent::LifeEvent::Rebirth)
                        ++auditRebirths_;
                }
            }
        }
    }

    // handle deaths and spore bursts (optimized path)
    {
        std::vector<size_t> toRemove;
        toRemove.reserve(64);
        for (size_t i = 0; i < agents_.size(); ++i)
        {
            Agent &a = agents_[i];
            if (a.speciesIndex < 0 || a.speciesIndex >= static_cast<int>(settings_.speciesSettings.size()))
                continue;
            const auto &sp = settings_.speciesSettings[a.speciesIndex];
            using DB = SimulationSettings::SpeciesSettings::DeathBehavior;
            bool expired = (a.ageSeconds > sp.lifespanSeconds || a.energy <= 0.05f);
            if (!expired)
                continue;
            if (sp.deathBehavior == DB::HardDeath)
            {
                toRemove.push_back(i);
                ++auditDeaths_;
            }
            else if (sp.deathBehavior == DB::SporeBurst)
            {
                // spawn spores around current position with full genome mutation
                int created = 0;
                auto mutateSpore = [&](float parentVal) -> float {
                    float delta = ((std::rand() / (float)RAND_MAX) - 0.5f) * 2.0f * sp.sporeMutationRate;
                    return std::clamp(parentVal * (1.0f + delta), 0.5f, 1.5f);
                };
                
                for (int s = 0; s < std::max(0, sp.sporeCount); ++s)
                {
                    float ang = 2.0f * 3.14159265f * (std::rand() / (float)RAND_MAX);
                    float r = sp.sporeRadius * (std::rand() / (float)RAND_MAX);
                    sf::Vector2f pos(a.position.x + std::cos(ang) * r, a.position.y + std::sin(ang) * r);
                    Agent child(pos.x, pos.y, ang, a.speciesIndex);
                    child.hasGenome = true;
                    
                    // Apply mutation to ALL genome traits
                    child.genome.moveSpeedScale = mutateSpore(a.genome.moveSpeedScale);
                    child.genome.turnSpeedScale = mutateSpore(a.genome.turnSpeedScale);
                    child.genome.sensorAngleScale = mutateSpore(a.genome.sensorAngleScale);
                    child.genome.sensorDistScale = mutateSpore(a.genome.sensorDistScale);
                    child.genome.alignWScale = mutateSpore(a.genome.alignWScale);
                    child.genome.cohWScale = mutateSpore(a.genome.cohWScale);
                    child.genome.sepWScale = mutateSpore(a.genome.sepWScale);
                    child.genome.oscStrengthScale = mutateSpore(a.genome.oscStrengthScale);
                    child.genome.oscFreqScale = mutateSpore(a.genome.oscFreqScale);
                    
                    child.energy = sp.sporeEnergy;
                    child.applyGenomeToCachedParams(settings_);
                    agents_.push_back(child);
                    ++created;
                }
                auditSpores_ += created;
                toRemove.push_back(i);
                ++auditDeaths_;
            }
            // rebirth handled in place by updateEnergyAndState
        }
        if (!toRemove.empty())
        {
            std::sort(toRemove.begin(), toRemove.end(), std::greater<size_t>()); // Sort descending
            toRemove.erase(std::unique(toRemove.begin(), toRemove.end()), toRemove.end());
            for (size_t idx : toRemove)
            {
                if (idx < agents_.size())
                {
                    // swap with last element then pop - O(1)
                    std::swap(agents_[idx], agents_.back());
                    agents_.pop_back();
                }
            }
        }
    }

    // reproduction pass (optimized path): spatial, capped
    const size_t maxOffspring = std::min<size_t>(agents_.size() / 50 + 1, 2000);
    size_t born = 0;
    // use grid for partner search
    for (size_t i = 0; i < agents_.size() && born < maxOffspring; ++i)
    {
        Agent &a = agents_[i];
        if (a.speciesIndex < 0 || a.speciesIndex >= static_cast<int>(settings_.speciesSettings.size()))
            continue;
        const auto &sp = settings_.speciesSettings[a.speciesIndex];
        // asexual split / pre-death budding (Hydra-style)
        // should trigger from: high energy or near end of life
        bool nearEndOfLife = (a.ageSeconds > sp.lifespanSeconds * 0.8f);
        bool canBud = sp.splittingEnabled && a.splitCooldown <= 0.0f && 
                      (a.energy > sp.splitEnergyThreshold || (nearEndOfLife && (std::rand() % 100) < 15));
        if (canBud)
        {
            Agent child = Agent::createOffspring(a, a, settings_);
            child.energy = sp.offspringEnergy;
            a.energy -= sp.matingEnergyCost;
            a.splitCooldown = sp.splitCooldownTime;
            agents_.push_back(child);
            ++born;
            ++auditSplits_;
            continue;
        }
        if (!sp.matingEnabled || a.mateCooldown > 0.0f || a.energy <= sp.matingEnergyCost)
            continue;
        auto neighbors = spatialGrid_->getNearbyAgents(a.position.x, a.position.y, sp.matingRadius);
        for (size_t idx : neighbors)
        {
            if (idx == i)
                continue;
            Agent &b = agents_[idx];
            const auto &spb = settings_.speciesSettings[std::min(b.speciesIndex, (int)settings_.speciesSettings.size() - 1)];
            // require both species to allow cross species mating when species differ
            if (b.speciesIndex != a.speciesIndex && !(sp.crossSpeciesMating && spb.crossSpeciesMating))
                continue;
            if (b.energy <= spb.matingEnergyCost || b.mateCooldown > 0.0f)
                continue;
            Agent child = Agent::createOffspring(a, b, settings_);
            agents_.push_back(child);
            a.energy -= sp.matingEnergyCost;
            b.energy -= spb.matingEnergyCost;
            a.mateCooldown = 1.5f;
            b.mateCooldown = 1.5f;
            ++born;
            if (a.speciesIndex == b.speciesIndex)
                ++auditMatingsSame_;
            else
                ++auditMatingsCross_;
            break;
        }
    }
}

void PhysarumSimulation::updateTrailsOptimized()
{
    if (parallelProcessor_ && useParallelUpdates_)
    {
        // parallel trail processing using simd optimized operations
        parallelProcessor_->processTrailsParallel(*optimizedTrailMap_, settings_.diffuseRate, settings_.decayRate);
    }
    else
    {
        // serial optimized trail updates
        optimizedTrailMap_->diffuseOptimized(settings_.diffuseRate);
        optimizedTrailMap_->decayOptimized(settings_.decayRate);
    }

    // apply blur only every few frames to reduce performance impact
    static int blurCounter = 0;
    if (settings_.blurEnabled && (++blurCounter % 2 == 0))
    {
        optimizedTrailMap_->applyBlurOptimized();
    }

    // syncs optimized trail map back to legacy trail map for display
    optimizedTrailMap_->syncToLegacyTrailMap(*trailMap_);
}

// Algorithm benchmark mode

void PhysarumSimulation::enterBenchmarkMode() {
    if (inBenchmarkMode_) return;
    
    inBenchmarkMode_ = true;
    benchmarkAgentsPacked_ = false;
    benchmarkPackedLaneIndex_ = -1;
    
    // starting all algorithms as enabled
    const auto& algorithms = BenchmarkManager::getBenchmarkAlgorithms();
    benchmarkAlgorithmEnabled_.assign(algorithms.size(), true);
    
    // reset slime communication states
    slimeGoalFound_ = false;
    slimeGoalX_ = 0.0f;
    slimeGoalY_ = 0.0f;
    
    std::cout << "Setting up benchmark mode [V7]..." << std::endl;
    std::cout << "  Window: " << settings_.width << "x" << settings_.height << std::endl;
    
    // configure trail settings for benchmark mode - visible but translucent trails
    settings_.trailWeight = 50.0f;      // strong deposits
    settings_.decayRate = 0.004f;       // moderate decay - trails fade visibly over time
    settings_.diffuseRate = 0.06f;      // some diffusion for softer look
    settings_.displayThreshold = 0.001f; // low threshold to show faint trails
    
    // uses fixed precision for small float values
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Trail settings: weight=" << settings_.trailWeight 
              << " decay=" << settings_.decayRate 
              << " diffuse=" << settings_.diffuseRate << std::endl;
    std::cout << std::defaultfloat;  // reset to default formatting
    
    // IMPORTANT here: recreate TrailMap with 10 species (one per algorithm)
    // 5 explorers + 5 pathfinders = 10 total algorithms
    int numAlgos = static_cast<int>(algorithms.size());
    // 7 algo channels (0-6) + 1 hidden channel (7) for goal food sensing
    trailMap_ = std::make_unique<TrailMap>(settings_.width, settings_.height, numAlgos + 1);
    std::cout << "  TrailMap recreated with " << (numAlgos + 1) << " channels (7 visible + 1 hidden for goal)" << std::endl;
    std::cout << "  Trail settings: weight=" << settings_.trailWeight 
              << " decay=" << settings_.decayRate 
              << " diffuse=" << settings_.diffuseRate << std::endl;
              
    // update species settings to match benchmark algorithms
    settings_.speciesSettings.clear();
    for (const auto& algo : algorithms) {
        SimulationSettings::SpeciesSettings species;
        species.color = BenchmarkManager::getAlgorithmColor(algo);
        species.moveSpeed = 2.0f; // standard speed for benchmark
        species.turnSpeed = 45.0f;
        species.sensorAngleSpacing = 45.0f;
        species.sensorOffsetDistance = 20.0f;
        settings_.speciesSettings.push_back(species);
    }
    
    // setup for benchmark manager with current dimensions and larger cell size for faster pathfinding
    benchmarkManager_.getPathfinder().setCellSize(settings_.benchmarkSettings.pathCellSize);
    benchmarkManager_.setupBenchmark(settings_.width, settings_.height, 
                           settings_.benchmarkSettings.agentsPerAlgorithm);
    
    std::cout << "  Goal at world: (" << benchmarkManager_.getGoalX() << ", " << benchmarkManager_.getGoalY() << ")" << std::endl;
    std::cout << "  Goal cell: (" << benchmarkManager_.getGoalCell().x << ", " << benchmarkManager_.getGoalCell().y << ")" << std::endl;
    std::cout << "  Grid size: " << benchmarkManager_.getPathfinder().getGridWidth() << "x" << benchmarkManager_.getPathfinder().getGridHeight() << std::endl;
    std::cout << "  Cell size: " << benchmarkManager_.getPathfinder().getCellSize() << std::endl;
    
    // clear existing agents
    agents_.clear();
    
    // get the algorithms and shuffle them for random lane order each time
    int agentId = 0;
    GridCell goal = benchmarkManager_.getGoalCell();
    
    // shuffled indices for random algorithm order to reduce bias from lane position
    benchmarkShuffledOrder_.resize(algorithms.size());
    for (size_t i = 0; i < algorithms.size(); i++) {
        benchmarkShuffledOrder_[i] = i;
    }
    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(benchmarkShuffledOrder_.begin(), benchmarkShuffledOrder_.end(), rng);
    
    std::cout << "  Algorithm order (randomized): ";
    for (size_t i = 0; i < benchmarkShuffledOrder_.size(); i++) {
        std::cout << SimulationSettings::algoNames(algorithms[benchmarkShuffledOrder_[i]]);
        if (i < benchmarkShuffledOrder_.size() - 1) std::cout << ", ";
    }
    std::cout << std::endl;
    
    // cache paths by (algorithm, start cell) to avoid redundant computation
    std::map<std::pair<int, GridCell>, PathResult> pathCache;
    int totalPathsFound = 0;
    int totalPathsFailed = 0;
    
    for (size_t laneIdx = 0; laneIdx < algorithms.size(); laneIdx++) {
        size_t algoIdx = benchmarkShuffledOrder_[laneIdx];  // get the shuffled algorithm for this lane
        SimulationSettings::Algos algo = algorithms[algoIdx];
        
        // but skipping disabled algorithms
        if (!benchmarkAlgorithmEnabled_[algoIdx]) {
            std::cout << "  Skipping " << SimulationSettings::algoNames(algo) << " (disabled)" << std::endl;
            continue;
        }
        
        bool isExplorer = SimulationSettings::isExplorer(algo);
        bool isSlime = (algo == SimulationSettings::Algos::Slime);
        
        std::cout << "  Setting up " << SimulationSettings::algoNames(algo) 
                  << " (" << (isExplorer ? "Explorer" : "Pathfinder") << ") in lane " << laneIdx << "..." << std::flush;
        
        int pathsComputed = 0;
        int pathsReused = 0;
        int explorersSetup = 0;
        
        // agents for this algorithm
        for (int i = 0; i < settings_.benchmarkSettings.agentsPerAlgorithm; i++) {
            // uses laneIdx for spawn position (visual lane) and algoIdx for species color
            auto [spawnX, spawnY] = benchmarkManager_.getSpawnPosition(
                static_cast<int>(laneIdx), i, settings_.benchmarkSettings.agentsPerAlgorithm);
            
            Agent agent(spawnX, spawnY, 0.0f, static_cast<int>(algoIdx));  
            agent.agentId = agentId++;
            agent.assignedAlgo = algo;
            
            GridCell start = benchmarkManager_.getPathfinder().worldToGrid(spawnX, spawnY);

            sf::Vector2f laneSpawn(spawnX, spawnY);
            agent.benchmarkDefaultSpawnPosition = laneSpawn;
            agent.benchmarkSpawnPosition = laneSpawn;
            agent.benchmarkLaneIndex = static_cast<int>(laneIdx);  // track which lane
            agent.benchmarkEnergy = 1.0f;
            agent.benchmarkSignalMemory = 0.0f;
            agent.benchmarkLowSignalFrames = 0;
            agent.benchmarkRespawnFrames = 0;
            agent.benchmarkAlive = true;
            agent.benchmarkPrevGoalDistance = -1.0f;
            agent.benchmarkRecentCollisionFrames = 0;
            
            //  uses native slime behavior (no path) 
            if (isSlime) {
                // slime uses its own behavior in updateBenchmark() - no path needed
                if (i == 0) {
                    std::cout << "\n    First agent spawn: (" << spawnX << ", " << spawnY << ")" 
                              << " -> grid (" << start.x << ", " << start.y << ")"
                              << " SLIME BEHAVIOR (native sensing)";
                }
            }
            //  explorers (BFS, DFS, dijkstra, slime*): true blind exploration 
            else if (isExplorer) {
                agent.initExploration(benchmarkManager_.getPathfinder(), algo);
                explorersSetup++;
                
                if (i == 0) {
                    std::cout << "\n    First agent spawn: (" << spawnX << ", " << spawnY << ")" 
                              << " -> grid (" << start.x << ", " << start.y << ")"
                              << " EXPLORER (blind search)";
                }
            }
            //  pathfinders (A*, greedy, etc.): compute path using their specific algorithm 
            else {
                auto cacheKey = std::make_pair(static_cast<int>(algoIdx), start);
                
                PathResult pathResult;
                auto it = pathCache.find(cacheKey);
                if (it != pathCache.end()) {
                    pathResult = it->second;
                    pathsReused++;
                } else {
                    // findPath() dispatches to the correct algorithm (A*, Greedy, etc.)
                    pathResult = benchmarkManager_.getPathfinder().findPath(algo, start, goal);
                    pathCache[cacheKey] = pathResult;
                    pathsComputed++;
                    
                    // debug first agent of each algorithm
                    if (i == 0) {
                        std::cout << "\n    First agent spawn: (" << spawnX << ", " << spawnY << ")" 
                                  << " -> grid (" << start.x << ", " << start.y << ")"
                                  << " path " << (pathResult.found ? "FOUND" : "NOT FOUND")
                                  << " len=" << pathResult.path.size()
                                  << " (" << SimulationSettings::algoNames(algo) << ")";
                    }
                }
                
                if (pathResult.found) {
                    agent.setPath(pathResult.path, algo);
                    totalPathsFound++;
                    
                    // recording pathfinding stats (only for computed paths and not cached)
                    if (it == pathCache.end()) {
                        auto& stats = benchmarkManager_.getStatsMutable(static_cast<int>(algoIdx));
                        stats.totalComputeTimeMs += pathResult.computeTimeMs;
                        stats.totalNodesExpanded += pathResult.nodesExpanded;
                        stats.avgPathLength = (stats.avgPathLength * (pathsComputed-1) + pathResult.pathLength) / pathsComputed;
                    }
                } else if (!pathResult.path.empty()) {
                    // a partial path but give it to agent anyway
                    agent.setPath(pathResult.path, algo);
                    totalPathsFound++;
                } else {
                    totalPathsFailed++;
                }
            }
            
            agents_.push_back(agent);
        }
        
        if (isSlime) {
            std::cout << " done (slime uses native behavior)" << std::endl;
        } else if (isExplorer) {
            std::cout << " done (" << explorersSetup << " explorers initialized)" << std::endl;
        } else {
            std::cout << " done (" << pathsComputed << " computed, " << pathsReused << " reused)" << std::endl;
            
            //the finalize compute time stats
            auto& stats = benchmarkManager_.getStatsMutable(static_cast<int>(algoIdx));
            if (pathsComputed > 0) {
                stats.avgComputeTimeMs = stats.totalComputeTimeMs / pathsComputed;
                stats.avgNodesExpanded = stats.totalNodesExpanded / pathsComputed;
            }
        }
    }
    
    std::cout << "  Total paths: " << totalPathsFound << " found, " << totalPathsFailed << " failed" << std::endl;
    
    std::cout << "  verifying first agent of each species:" << std::endl;
    for (size_t i = 0; i < algorithms.size() && i * settings_.benchmarkSettings.agentsPerAlgorithm < agents_.size(); i++) {
        size_t idx = i * settings_.benchmarkSettings.agentsPerAlgorithm;
        const Agent& a = agents_[idx];
        std::cout << "    Species " << i << " (" << SimulationSettings::algoNames(algorithms[i]) << "): "
                  << "pos=(" << a.position.x << ", " << a.position.y << ") "
                  << "hasPath=" << a.hasPath << " pathLen=" << a.currentPath.size() << std::endl;
    }
    
    // update species settings for display colors
    settings_.speciesSettings.clear();
    for (size_t i = 0; i < algorithms.size(); i++) {
        SimulationSettings::SpeciesSettings sp;
        sp.color = BenchmarkManager::getAlgorithmColor(algorithms[i]);
        sp.moveSpeed = settings_.benchmarkSettings.agentMoveSpeed;
        settings_.speciesSettings.push_back(sp);
    }
    
    std::cout << "entered algorithm benchmark mode with " << agents_.size() << " agents" << std::endl;
    std::cout << "goal at (" << benchmarkManager_.getGoalX() << ", " << benchmarkManager_.getGoalY() << ")" << std::endl;
}

void PhysarumSimulation::exitBenchmarkMode() {
    if (!inBenchmarkMode_) return;
    
    inBenchmarkMode_ = false;
    benchmarkAgentsPacked_ = false;
    benchmarkPackedLaneIndex_ = -1;
    benchmarkManager_.reset();
    
    // for restore normal simulation
    reset();
    
    std::cout << "Exited Algorithm Benchmark Mode" << std::endl;
}

void PhysarumSimulation::startBenchmark() {
    if (!inBenchmarkMode_) return;
    benchmarkManager_.startBenchmark();
    std::cout << "Benchmark started!" << std::endl;
}

void PhysarumSimulation::pauseBenchmark() {
    if (!inBenchmarkMode_) return;
    
    if (benchmarkManager_.isBenchmarkActive()) {
        benchmarkManager_.pauseBenchmark();
        std::cout << "Benchmark paused" << std::endl;
    } else {
        benchmarkManager_.resumeBenchmark();
        std::cout << "Benchmark resumed" << std::endl;
    }
}

void PhysarumSimulation::resetBenchmark() {
    if (!inBenchmarkMode_) return;
    
    // saving of enabled state before reset
    std::vector<bool> savedEnabled = benchmarkAlgorithmEnabled_;
    
    // reenter benchmark mode to reset everything
    inBenchmarkMode_ = false;
    benchmarkAgentsPacked_ = false;
    benchmarkPackedLaneIndex_ = -1;
    enterBenchmarkMode();
    
    // restoring enabled state
    benchmarkAlgorithmEnabled_ = savedEnabled;
    
    //rRemove agents for disabled algorithms. erase_if or remove_if might be better? but they arent very readable
    size_t write = 0;
    for (size_t read = 0; read < agents_.size(); read++) {
        int idx = agents_[read].speciesIndex;
        if (idx >= 0 && idx < static_cast<int>(benchmarkAlgorithmEnabled_.size()) && 
            benchmarkAlgorithmEnabled_[idx]) {
            if (write != read) {
                agents_[write] = std::move(agents_[read]);
            }
            write++;
        }
    }
    agents_.erase(agents_.begin() + write, agents_.end());
}

void PhysarumSimulation::toggleBenchmarkPackPositions() {
    if (!inBenchmarkMode_) {
        std::cout << "[BENCH PACK] Only available in benchmark mode." << std::endl;
        return;
    }
    if (benchmarkManager_.isBenchmarkActive()) {
        std::cout << "[BENCH PACK] Toggle before starting the benchmark (press Space to reset if needed)." << std::endl;
        return;
    }

    benchmarkAgentsPacked_ = !benchmarkAgentsPacked_;
    auto& pathfinder = benchmarkManager_.getPathfinder();
    GridCell goalCell = benchmarkManager_.getGoalCell();
    static thread_local std::mt19937 rng(std::random_device{}());
    
    int numAlgos = static_cast<int>(BenchmarkManager::getBenchmarkAlgorithms().size());
    float laneHeight = static_cast<float>(settings_.height) / numAlgos;
    
    if (benchmarkAgentsPacked_) {
        if (numAlgos == 0) return;
        std::uniform_int_distribution<int> lanePick(0, numAlgos - 1);
        benchmarkPackedLaneIndex_ = lanePick(rng);
        float targetLaneCenterY = (benchmarkPackedLaneIndex_ + 0.5f) * laneHeight;
        benchmarkPackPoint_ = sf::Vector2f(0.0f, targetLaneCenterY);
    } else {
        benchmarkPackedLaneIndex_ = -1;
    }

    auto resetAgentStart = [&](Agent& agent) {
        agent.position = agent.benchmarkSpawnPosition;
        agent.velocity = sf::Vector2f(0.0f, 0.0f);
        agent.acceleration = sf::Vector2f(0.0f, 0.0f);
        agent.reachedGoal = false;
        agent.foundGoalFirst = false;
        agent.hasPath = false;
        agent.currentPath.clear();
        agent.pathIndex = 0;
        agent.isExploring = false;
        agent.explorationFrontier.clear();
        agent.visitedCells.clear();
        agent.explorationParents.clear();
        agent.explorationCosts.clear();
        agent.explorationProgress = 1.0f;
        agent.benchmarkAlive = true;
        agent.benchmarkRespawnFrames = 0;
        agent.benchmarkLowSignalFrames = 0;
        agent.benchmarkSignalMemory = 0.0f;
        agent.benchmarkWallFollowFrames = 0;
        agent.benchmarkPrevGoalDistance = -1.0f;
        agent.benchmarkRecentCollisionFrames = 0;

        if (agent.assignedAlgo == SimulationSettings::Algos::Slime) {
            agent.benchmarkEnergy = 1.0f;
            return;
        }

        if (SimulationSettings::isExplorer(agent.assignedAlgo)) {
            agent.initExploration(pathfinder, agent.assignedAlgo);
        } else {
            GridCell startCell = pathfinder.worldToGrid(
                static_cast<int>(std::clamp(agent.benchmarkSpawnPosition.x, 0.0f, static_cast<float>(settings_.width - 1))),
                static_cast<int>(std::clamp(agent.benchmarkSpawnPosition.y, 0.0f, static_cast<float>(settings_.height - 1))));
            PathResult pathResult = pathfinder.findPath(agent.assignedAlgo, startCell, goalCell);
            if (pathResult.found) {
                agent.setPath(pathResult.path, agent.assignedAlgo);
            } else {
                agent.clearPath();
            }
        }
    };

    if (benchmarkAgentsPacked_) {
        //each agent keeps its X and its offset from its original lane center,
        // but moves to the target lane's Y center
        float targetLaneCenterY = benchmarkPackPoint_.y;
        
        for (auto& agent : agents_) {
            // agent's original lane center Y using their actual lane index
            int origLaneIdx = agent.benchmarkLaneIndex;
            float origLaneCenterY = (origLaneIdx + 0.5f) * laneHeight;
            
            // agent's offset from its lane center
            float offsetY = agent.benchmarkDefaultSpawnPosition.y - origLaneCenterY;
            
            // and new position: same X, but Y relative to target lane
            sf::Vector2f packedPos(
                agent.benchmarkDefaultSpawnPosition.x,
                targetLaneCenterY + offsetY);
            
            packedPos.y = std::clamp(packedPos.y, 10.0f, static_cast<float>(settings_.height - 10));
            
            agent.benchmarkSpawnPosition = packedPos;
            resetAgentStart(agent);
        }
        std::cout << "[BENCH PACK] All groups stacked at lane " << benchmarkPackedLaneIndex_ << std::endl;
    } else {
        for (auto& agent : agents_) {
            agent.benchmarkSpawnPosition = agent.benchmarkDefaultSpawnPosition;
            resetAgentStart(agent);
        }
        std::cout << "[BENCH PACK] Agents restored to original lane layout." << std::endl;
    }
}

void PhysarumSimulation::adjustBenchmarkAgentCount(int delta) {
    if (!inBenchmarkMode_) return;
    
    const auto& algorithms = BenchmarkManager::getBenchmarkAlgorithms();
    int numAlgos = static_cast<int>(algorithms.size());
    
    // use the stored setting as source of truth, not agent counts
    // (disabled algos would throw off the count)
    int currentPerAlgo = settings_.benchmarkSettings.agentsPerAlgorithm;
    int minPerAlgo = 5;
    int maxPerAlgo = 500;
    int newPerAlgo = std::clamp(currentPerAlgo + delta, minPerAlgo, maxPerAlgo);
    
    if (newPerAlgo == currentPerAlgo) {
        std::cout << "Agent count at limit (" << currentPerAlgo << " per algo)" << std::endl;
        return;
    }
    
    settings_.benchmarkSettings.agentsPerAlgorithm = newPerAlgo;
    
    if (delta > 0) {
        // spawn them fresh at the start positions
        GridCell goal = benchmarkManager_.getGoalCell();
        int agentId = static_cast<int>(agents_.size());
        int added = 0;
        int actualDelta = newPerAlgo - currentPerAlgo;
        
        // uses the stored shuffled order for consistent lane assignment
        for (int laneIdx = 0; laneIdx < numAlgos; laneIdx++) {
            size_t algoIdx = benchmarkShuffledOrder_[laneIdx];
            
            // Skip disabled algorithms
            if (!benchmarkAlgorithmEnabled_[algoIdx]) {
                continue;
            }
            
            SimulationSettings::Algos algo = algorithms[algoIdx];
            bool isExplorer = SimulationSettings::isExplorer(algo);
            bool isSlime = (algo == SimulationSettings::Algos::Slime);
            
            for (int i = 0; i < actualDelta; i++) {
                auto [spawnX, spawnY] = benchmarkManager_.getSpawnPosition(
                    laneIdx, currentPerAlgo + i, newPerAlgo);  
                sf::Vector2f defaultSpawn(spawnX, spawnY);
                sf::Vector2f activeSpawn = defaultSpawn;
                
                if (benchmarkAgentsPacked_ && benchmarkPackedLaneIndex_ >= 0) {
                    // Calculate offset from this agent's original lane center
                    float laneHeight = static_cast<float>(settings_.height) / numAlgos;
                    float origLaneCenterY = (laneIdx + 0.5f) * laneHeight;
                    float offsetY = spawnY - origLaneCenterY;
                    float targetLaneCenterY = (benchmarkPackedLaneIndex_ + 0.5f) * laneHeight;
                    activeSpawn.y = std::clamp(targetLaneCenterY + offsetY, 10.0f, static_cast<float>(settings_.height - 10));
                    // X stays the same (keeps diagonal pattern)
                }

                Agent agent(activeSpawn.x, activeSpawn.y, 0.0f, static_cast<int>(algoIdx));
                agent.agentId = agentId++;
                agent.assignedAlgo = algo;
                
                int startX = static_cast<int>(std::clamp(activeSpawn.x, 0.0f, static_cast<float>(settings_.width - 1)));
                int startY = static_cast<int>(std::clamp(activeSpawn.y, 0.0f, static_cast<float>(settings_.height - 1)));
                GridCell start = benchmarkManager_.getPathfinder().worldToGrid(startX, startY);

                agent.benchmarkDefaultSpawnPosition = defaultSpawn;
                agent.benchmarkSpawnPosition = activeSpawn;
                agent.benchmarkLaneIndex = laneIdx;  // track which lane
                agent.benchmarkEnergy = 1.0f;
                agent.benchmarkSignalMemory = 0.0f;
                agent.benchmarkLowSignalFrames = 0;
                agent.benchmarkRespawnFrames = 0;
                agent.benchmarkAlive = true;
                agent.benchmarkPrevGoalDistance = -1.0f;
                agent.benchmarkRecentCollisionFrames = 0;
                
                if (isExplorer && !isSlime) {
                    agent.initExploration(benchmarkManager_.getPathfinder(), algo);
                }
                else if (!isSlime) {
                    PathResult pathResult = benchmarkManager_.getPathfinder().findPath(algo, start, goal);
                    if (pathResult.found) {
                        agent.setPath(pathResult.path, algo);
                    }
                }
                
                agents_.push_back(agent);
                added++;
            }
        }
        std::cout << "Added " << added << " agents (" << newPerAlgo << " per algo, " 
                  << agents_.size() << " total)" << std::endl;
    }
    else {
        std::vector<Agent> newAgents;
        std::vector<int> keptPerAlgo(numAlgos, 0);
        
        for (const Agent& agent : agents_) {
            int algoIdx = agent.speciesIndex;
            if (algoIdx >= 0 && algoIdx < numAlgos && keptPerAlgo[algoIdx] < newPerAlgo) {
                newAgents.push_back(agent);
                keptPerAlgo[algoIdx]++;
            }
        }
        
        int removed = static_cast<int>(agents_.size()) - static_cast<int>(newAgents.size());
        agents_ = std::move(newAgents);
        std::cout << "Removed " << removed << " agents (" << newPerAlgo << " per algo, "
                  << agents_.size() << " total)" << std::endl;
    }
    
    // HUD update counts
    benchmarkManager_.updateAgentCounts(newPerAlgo);
}

void PhysarumSimulation::toggleBenchmarkAlgorithm(int algoIndex) {
    if (!inBenchmarkMode_) return;
    
    const auto& algorithms = BenchmarkManager::getBenchmarkAlgorithms();
    int numAlgos = static_cast<int>(algorithms.size());
    
    if (algoIndex < 0 || algoIndex >= numAlgos) {
        std::cout << "Invalid algorithm index: " << algoIndex << " (valid: 0-" << (numAlgos - 1) << ")" << std::endl;
        return;
    }
    
    // for finding which lane this algorithm is in (due to shuffling)
    int targetLane = -1;
    for (size_t lane = 0; lane < benchmarkShuffledOrder_.size(); lane++) {
        if (benchmarkShuffledOrder_[lane] == static_cast<size_t>(algoIndex)) {
            targetLane = static_cast<int>(lane);
            break;
        }
    }
    
    // count for how many agents of this algorithm exist
    int currentCount = 0;
    for (const Agent& agent : agents_) {
        if (agent.speciesIndex == algoIndex) {
            currentCount++;
        }
    }
    
    SimulationSettings::Algos algo = algorithms[algoIndex];
    std::string algoName = SimulationSettings::algoNames(algo);
    
    // toggle enabled state
    benchmarkAlgorithmEnabled_[algoIndex] = !benchmarkAlgorithmEnabled_[algoIndex];
    
    if (!benchmarkAlgorithmEnabled_[algoIndex]) {
        // removes all agents of this algorithm
        agents_.erase(
            std::remove_if(agents_.begin(), agents_.end(),
                [algoIndex](const Agent& a) { return a.speciesIndex == algoIndex; }),
            agents_.end());
        std::cout << "[BENCH] Disabled " << algoName << std::endl;
    } else {
        // add agents for this algorithm
        if (targetLane < 0) {
            std::cout << "[BENCH] Could not find lane for " << algoName << std::endl;
            return;
        }
        
        int perAlgo = settings_.benchmarkSettings.agentsPerAlgorithm;
        GridCell goal = benchmarkManager_.getGoalCell();
        int agentId = static_cast<int>(agents_.size());
        bool isExplorer = SimulationSettings::isExplorer(algo);
        bool isSlime = (algo == SimulationSettings::Algos::Slime);
        
        int numAlgosForLane = static_cast<int>(algorithms.size());
        float laneHeight = static_cast<float>(settings_.height) / numAlgosForLane;
        float targetLaneCenterY = (benchmarkPackedLaneIndex_ + 0.5f) * laneHeight;
        
        for (int i = 0; i < perAlgo; i++) {
            auto [spawnX, spawnY] = benchmarkManager_.getSpawnPosition(targetLane, i, perAlgo);
            sf::Vector2f defaultSpawn(spawnX, spawnY);
            sf::Vector2f activeSpawn = defaultSpawn;
            
            if (benchmarkAgentsPacked_ && benchmarkPackedLaneIndex_ >= 0) {
                float origLaneCenterY = (targetLane + 0.5f) * laneHeight;
                float offsetY = spawnY - origLaneCenterY;
                activeSpawn.y = std::clamp(targetLaneCenterY + offsetY, 10.0f, static_cast<float>(settings_.height - 10));
            }
            
            Agent agent(activeSpawn.x, activeSpawn.y, 0.0f, algoIndex);
            agent.agentId = agentId++;
            agent.assignedAlgo = algo;
            agent.benchmarkDefaultSpawnPosition = defaultSpawn;
            agent.benchmarkSpawnPosition = activeSpawn;
            agent.benchmarkLaneIndex = targetLane;
            agent.benchmarkEnergy = 1.0f;
            agent.benchmarkSignalMemory = 0.0f;
            agent.benchmarkLowSignalFrames = 0;
            agent.benchmarkRespawnFrames = 0;
            agent.benchmarkAlive = true;
            agent.benchmarkPrevGoalDistance = -1.0f;
            agent.benchmarkRecentCollisionFrames = 0;
            
            if (isExplorer && !isSlime) {
                agent.initExploration(benchmarkManager_.getPathfinder(), algo);
            } else if (!isSlime) {
                int startX = static_cast<int>(std::clamp(activeSpawn.x, 0.0f, static_cast<float>(settings_.width - 1)));
                int startY = static_cast<int>(std::clamp(activeSpawn.y, 0.0f, static_cast<float>(settings_.height - 1)));
                GridCell start = benchmarkManager_.getPathfinder().worldToGrid(startX, startY);
                PathResult pathResult = benchmarkManager_.getPathfinder().findPath(algo, start, goal);
                if (pathResult.found) {
                    agent.setPath(pathResult.path, algo);
                }
            }
            
            agents_.push_back(agent);
        }
        std::cout << "[BENCH] Enabled " << algoName << " (" << perAlgo << " agents)" << std::endl;
    }
    
    // sync with benchmark manager so HUD updates
    benchmarkManager_.setAlgorithmEnabled(algoIndex, benchmarkAlgorithmEnabled_[algoIndex]);
}

void PhysarumSimulation::updateBenchmark(float deltaTime) {
    if (!inBenchmarkMode_) return;
    
    // if benchmark not started yet then just wait (agents stay at spawn)
    if (!benchmarkManager_.isBenchmarkActive()) return;
    
    // if benchmark is paused then doesnt update
    if (benchmarkManager_.isPaused()) return;
    
    benchmarkManager_.update(deltaTime);
    
    // agents following its path
    Pathfinder& pathfinder = benchmarkManager_.getPathfinder();
    float goalRadius = settings_.benchmarkSettings.goalArrivalRadius;
    float moveSpeed = settings_.benchmarkSettings.agentMoveSpeed;
    constexpr int SLIME_RESPAWN_DELAY_FRAMES = 90;
    
    //  depositing food at goal location
    // this allows slime agents to sense the goal via trail concentration
    // instead of knowing goal coordinates (which is authentic Physarum behavior)
    // MUST be MUCH stronger than slime self trails to create a gradient
    // that pulls slimes out of their local "scent bubble"
    int goalX = static_cast<int>(benchmarkManager_.getGoalX());
    int goalY = static_cast<int>(benchmarkManager_.getGoalY());
    const float GOAL_FOOD_STRENGTH = 500.0f;  // strong beacon for slime sensing
    const int GOAL_FOOD_RADIUS = 200;         // and extra wide radius for long range detection
    
    // deposit food to a hidden channel (7) - not rendered but sensed by slimes
    // this way goal food doesnt create a visible glow that overwhelms trail visibility
    const int goalFoodChannel = std::max(0, trailMap_->getNumSpecies() - 1);
    
    for (int dx = -GOAL_FOOD_RADIUS; dx <= GOAL_FOOD_RADIUS; ++dx) {
        for (int dy = -GOAL_FOOD_RADIUS; dy <= GOAL_FOOD_RADIUS; ++dy) {
            float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            if (dist <= GOAL_FOOD_RADIUS) {
                int px = goalX + dx;
                int py = goalY + dy;
                if (px >= 0 && px < settings_.width && py >= 0 && py < settings_.height) {
                    float falloff = 1.0f - (dist / GOAL_FOOD_RADIUS);
                    trailMap_->deposit(px, py, GOAL_FOOD_STRENGTH * falloff, goalFoodChannel);
                }
            }
        }
    }
    
    static int debugCounter = 0;
    static int frameCounter = 0;
    frameCounter++;

    //goal field instrumentation accumulators (experiment 1) 
    constexpr float SPAWN_GOAL_SAMPLE_MAX_X = 160.0f;
    float frameSpawnGoalSum = 0.0f;
    int frameSpawnGoalSamples = 0;
    float frameFrontGoalValue = 0.0f;
    float frameFrontMostX = -1.0f;
    static float spawnGoalRunningSum = 0.0f;
    static int spawnGoalRunningFrames = 0;
    static float frontGoalRunningSum = 0.0f;
    static int frontGoalRunningFrames = 0;
    static int goalFieldPrintCounter = 0;
    
    int movedCount = 0;
    int arrivedCount = 0;
    int noPathCount = 0;
    int slimeCount = 0;
    int explorerCount = 0;
    
    // debugging: count slimes in agents
    static int debugFrame = 0;
    if (++debugFrame % 120 == 0) {
        int totalSlimes = 0;
        int slimesExploring = 0;
        int slimesReached = 0;
        for (const auto& a : agents_) {
            if (a.assignedAlgo == SimulationSettings::Algos::Slime) {
                totalSlimes++;
                if (a.isExploring) slimesExploring++;
                if (a.reachedGoal) slimesReached++;
            }
        }
        std::cout << "[SLIME CENSUS] Total=" << totalSlimes 
                  << " isExploring=" << slimesExploring 
                  << " reachedGoal=" << slimesReached << std::endl;
    }
    
    int agentIndex = 0;
    for (Agent& agent : agents_) {
        int i = agentIndex++;  // track agent index for rate limiting
        if (agent.reachedGoal) {
            arrivedCount++;
            continue;
        }
        
        if (agent.isExploring) {
            explorerCount++;
            
            GridCell goalCell = benchmarkManager_.getGoalCell();
            
            bool found = false;
            if (agent.assignedAlgo == SimulationSettings::Algos::Dijkstra) {
                found = agent.exploreStepDijkstra(pathfinder, goalCell, moveSpeed);
            } else {
                found = agent.exploreStep(pathfinder, goalCell, moveSpeed, nullptr);
            }
            
            if (found) {
                agent.reachedGoal = true;
                benchmarkManager_.recordArrival(agent.speciesIndex, agent.agentId);
            }
            
            agent.depositBenchmark(*trailMap_, settings_.trailWeight);
            continue;
        }
        
        if (agent.assignedAlgo == SimulationSettings::Algos::Slime) {
            slimeCount++;

            static int slimeCodePathCounter = 0;
            if (++slimeCodePathCounter % 300 == 0) {
                std::cout << "[SLIME CODE PATH] " << slimeCodePathCounter 
                          << " slime updates executed" << std::endl;
            }

            if (agent.isExploring) {
                static bool warned = false;
                if (!warned) {
                    std::cout << "[WARNING] Slime agent " << agent.agentId << " has isExploring=true!" << std::endl;
                    warned = true;
                }
            }

            if (!agent.benchmarkAlive) {
                if (agent.benchmarkRespawnFrames > 0) {
                    agent.benchmarkRespawnFrames--;
                }
                if (agent.benchmarkRespawnFrames <= 0) {
                    respawnBenchmarkSlime(agent);
                }
                continue;
            }

            double elapsedMs = benchmarkManager_.getBenchmarkElapsedMs();
            float depositMultiplier = (elapsedMs < 5000.0) ? 0.25f : 1.0f;
            if (agent.benchmarkSignalMemory < 0.015f) {
                depositMultiplier *= 0.5f;
            }
            float slimeTrailStrength = 2.0f * depositMultiplier;
            float goalWorldX = benchmarkManager_.getGoalX();
            float goalWorldY = benchmarkManager_.getGoalY();

            bool alive = agent.benchmarkSlimeStep(
                *trailMap_, pathfinder, settings_,
                slimeTrailStrength,
                goalWorldX, goalWorldY,
                goalFoodChannel
            );

            if (!alive) {
                agent.benchmarkAlive = false;
                agent.benchmarkRespawnFrames = SLIME_RESPAWN_DELAY_FRAMES;
                continue;
            }

            int sampleX = std::clamp(static_cast<int>(agent.position.x), 0, settings_.width - 1);
            int sampleY = std::clamp(static_cast<int>(agent.position.y), 0, settings_.height - 1);
            float goalSense = trailMap_->sample(sampleX, sampleY, goalFoodChannel);
            if (agent.position.x < SPAWN_GOAL_SAMPLE_MAX_X) {
                frameSpawnGoalSum += goalSense;
                frameSpawnGoalSamples++;
            }
            if (agent.position.x > frameFrontMostX) {
                frameFrontMostX = agent.position.x;
                frameFrontGoalValue = goalSense;
            }

            float dx = goalWorldX - agent.position.x;
            float dy = goalWorldY - agent.position.y;
            float distToGoal = std::sqrt(dx * dx + dy * dy);

            if (distToGoal < goalRadius) {
                std::cout << "[SLIME ARRIVED] Agent " << agent.agentId 
                          << " reached goal! Reinforcing path with 500 strength to ch2" << std::endl;
                agent.reinforceRecentPath(*trailMap_, 500.0f);

                agent.reachedGoal = true;
                benchmarkManager_.recordArrival(agent.speciesIndex, agent.agentId);
            }

            continue;
        }
        
        if (!agent.hasPath || agent.currentPath.empty()) {
            noPathCount++;
            continue;
        }
        
        bool arrived = agent.followPath(pathfinder, moveSpeed, goalRadius);
        movedCount++;
        
        if (arrived) {
            agent.reachedGoal = true;
            benchmarkManager_.recordArrival(agent.speciesIndex, agent.agentId);
        }
        
        agent.depositBenchmark(*trailMap_, settings_.trailWeight);
    }
    
    if (frameSpawnGoalSamples > 0) {
        spawnGoalRunningSum += frameSpawnGoalSum / static_cast<float>(frameSpawnGoalSamples);
        spawnGoalRunningFrames++;
    }
    if (frameFrontMostX >= 0.0f) {
        frontGoalRunningSum += frameFrontGoalValue;
        frontGoalRunningFrames++;
    }
    goalFieldPrintCounter++;
    if (goalFieldPrintCounter >= 120) {
        float avgSpawn = (spawnGoalRunningFrames > 0) ?
            (spawnGoalRunningSum / static_cast<float>(spawnGoalRunningFrames)) : 0.0f;
        float avgFront = (frontGoalRunningFrames > 0) ?
            (frontGoalRunningSum / static_cast<float>(frontGoalRunningFrames)) : 0.0f;
        std::cout << "[GoalField] spawnAvg=" << avgSpawn
                  << " frontAvg=" << avgFront << std::endl;
        spawnGoalRunningSum = 0.0f;
        spawnGoalRunningFrames = 0;
        frontGoalRunningSum = 0.0f;
        frontGoalRunningFrames = 0;
        goalFieldPrintCounter = 0;
    }

    debugCounter++;
    if (debugCounter % 60 == 0) {
        std::cout << "Benchmark: explorers=" << explorerCount
                  << " pathfinders=" << movedCount 
                  << " slime=" << slimeCount
                  << " arrived=" << arrivedCount 
                  << " noPath=" << noPathCount << std::endl;
    }
}

void PhysarumSimulation::respawnBenchmarkSlime(Agent &agent) {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> jitter(-6.0f, 6.0f);

    sf::Vector2f spawn = agent.benchmarkSpawnPosition;
    spawn.x = std::clamp(spawn.x + jitter(rng), 5.0f, static_cast<float>(settings_.width - 5));
    spawn.y = std::clamp(spawn.y + jitter(rng), 5.0f, static_cast<float>(settings_.height - 5));

    agent.position = spawn;
    agent.angle = jitter(rng) * 0.2f;
    agent.velocity = sf::Vector2f(0.0f, 0.0f);
    agent.acceleration = sf::Vector2f(0.0f, 0.0f);
    agent.benchmarkEnergy = 0.6f;
    agent.benchmarkSignalMemory = 0.0f;
    agent.benchmarkLowSignalFrames = 0;
    agent.benchmarkAlive = true;
    agent.benchmarkRespawnFrames = 0;
    agent.benchmarkWallFollowFrames = 0;
    agent.benchmarkPrevGoalDistance = -1.0f;
    agent.benchmarkRecentCollisionFrames = 0;
    agent.pathMemoryCount = 0;
    agent.pathMemoryIndex = 0;
    agent.reachedGoal = false;
}