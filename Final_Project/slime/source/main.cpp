/********************************************************
 *  author:         Nicholas Tibbetts
 *  date:           06/04/2025 t00:57:29
 *  description:    _
 *  :               _
 *  build/run:      shift+cmd+b ./main
 ***********************************************************/

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <cstdint>
#include <SFML/System.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <random>

#include "SimulationSettings.h"
#include "Agent.h"
#include "TrailMap.h"
#include "PhysarumSimulation.h"

// species generation modes
enum class SpeciesMode
{
    Single = 1,        // original single species
    Classic5 = 2,      // the 5 distinct species
    Random = 3,        // randomly generated species
    AlgorithmBenchmark = 4  // pathfinding algorithm benchmark mode
};

// hud display modes
enum class HudMode
{
    Full = 0,    // full detailed hud
    Compact = 1, // minimal info only
    Hidden = 2   // no hud at all
};

// hud position
enum class HudPosition
{
    TopLeft = 0,
    TopRight = 1,
    BottomLeft = 2,
    BottomRight = 3
};

// global variables for species management
static SpeciesMode currentSpeciesMode = SpeciesMode::Single;
static int randomSpeciesCount = 3;                              // default number of random species
static std::vector<int> activeSpeciesIndices = {0}; // START WITH ONLY RED for profiling

// global variables for hud management
static HudMode currentHudMode = HudMode::Full;
static HudPosition currentHudPosition = HudPosition::TopLeft;
static bool hudTransparency = true; // whether hud background is transparent
static bool allUIHidden = false;    // Shift+F toggle - completely hides ALL UI elements
static HudMode savedHudMode = HudMode::Full; // saved mode before hiding all UI

// TODO: restructure to change this forward declaration*
std::string getSpeciesModeString(SpeciesMode mode, int randomCount);
std::string getHudModeString(HudMode mode);
std::string getHudPositionString(HudPosition position);
void setupSpecies(SimulationSettings &settings);
float scaleBehaviorValue(float baseValue, int intensity, float minMult, float maxMult);

// window constants
const int WINDOW_WIDTH = 1200;
const int WINDOW_HEIGHT = 900;

// mouse interaction settings
struct MouseSettings
{
    float foodStrength = 50.0f;       // base amount of food/attractant to deposit
    float repellentStrength = -25.0f; // negative amount for repellent
    int brushRadius = 60;             // radius of effect around mouse cursor
    bool leftButtonPressed = false;
    bool rightButtonPressed = false;
};

// randomize simulation settings
void randomizeSettings(SimulationSettings &settings)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());

    // randomize trail settings
    std::uniform_real_distribution<float> diffuseDist(0.005f, 0.08f);
    std::uniform_real_distribution<float> decayDist(0.001f, 0.03f);
    std::uniform_real_distribution<float> trailWeightDist(1.0f, 20.0f);
    std::uniform_real_distribution<float> thresholdDist(0.0f, 0.2f);

    settings.diffuseRate = diffuseDist(gen);
    settings.decayRate = decayDist(gen);
    settings.trailWeight = trailWeightDist(gen);
    settings.displayThreshold = thresholdDist(gen);

    // randomly toggle blur (visual test)
    // std::uniform_int_distribution<int> boolDist(0, 1);
    // settings.blurEnabled = boolDist(gen) == 1;

    // randomize species settings if available
    if (!settings.speciesSettings.empty())
    {
        auto &species = settings.speciesSettings[0];

        std::uniform_real_distribution<float> moveSpeedDist(0.5f, 4.0f);
        std::uniform_real_distribution<float> turnSpeedDist(8.0f, 45.0f);
        std::uniform_real_distribution<float> sensorAngleDist(8.0f, 35.0f);
        std::uniform_real_distribution<float> sensorDistDist(6.0f, 20.0f);

        species.moveSpeed = moveSpeedDist(gen);
        species.turnSpeed = turnSpeedDist(gen);
        species.sensorAngleSpacing = sensorAngleDist(gen);
        species.sensorOffsetDistance = sensorDistDist(gen);
    }

    std::cout << "Settings randomized! New values:" << std::endl;
    std::cout << "  Diffuse: " << settings.diffuseRate << ", Decay: " << settings.decayRate << std::endl;
    std::cout << "  Trail Weight: " << settings.trailWeight << ", Threshold: " << settings.displayThreshold << std::endl;
    if (!settings.speciesSettings.empty())
    {
        const auto &species = settings.speciesSettings[0];
        std::cout << "  Move Speed: " << species.moveSpeed << ", Turn Speed: " << species.turnSpeed << std::endl;
        std::cout << "  Sensor Angle: " << species.sensorAngleSpacing << ", Sensor Distance: " << species.sensorOffsetDistance << std::endl;
    }
}

// declarations for species management
void addRandomClassic5Species(PhysarumSimulation &simulation, SimulationSettings &settings);
void removeRandomClassic5Species(PhysarumSimulation &simulation, SimulationSettings &settings);
void toggleSpecies(int speciesIndex, PhysarumSimulation &simulation, SimulationSettings &settings);

void drawCompactHUD(sf::RenderWindow &window, sf::Font &font, const SimulationSettings &settings,
                    const MouseSettings &mouseSettings, int agentCount,
                    SpeciesMode speciesMode, int randomCount, HudPosition position)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    // minimal info only
    oss << "Agents: " << agentCount << " | ";
    oss << getSpeciesModeString(speciesMode, randomCount) << "\n";

    // shading status and motion inertia (compact)
    oss << (settings.slimeShadingEnabled ? "Shading: ON" : "Shading: OFF") << " | ";
    oss << "Inertia: " << std::fixed << std::setprecision(2) << settings.motionInertia << " | ";
    oss << (settings.anisotropicSplatsEnabled ? "Aniso: ON" : "Aniso: OFF") << " | ";
    oss << "k: " << std::fixed << std::setprecision(2) << settings.complianceStrength << " | ";
    oss << "d: " << std::fixed << std::setprecision(2) << settings.complianceDamping << "\n";

    if (speciesMode == SpeciesMode::Random)
    {
        oss << "[N/V] Count | [G] Regen | ";
    }
    oss << "[F] Mode | [H] Toggle | [Shift+S] Shade | [Shift+1-4] Position";

    sf::Text text(font, oss.str(), 12);
    text.setFillColor(sf::Color::White);
    text.setOutlineColor(sf::Color::Black);
    text.setOutlineThickness(1.0f);

    // position the hud based on the current position setting
    sf::FloatRect textBounds = text.getLocalBounds();
    sf::Vector2f windowSize(static_cast<float>(window.getSize().x), static_cast<float>(window.getSize().y));
    sf::Vector2f hudPos;

    switch (position)
    {
    case HudPosition::TopLeft:
        hudPos = sf::Vector2f(10.0f, 10.0f);
        break;
    case HudPosition::TopRight:
        hudPos = sf::Vector2f(windowSize.x - textBounds.size.x - 15.0f, 10.0f);
        break;
    case HudPosition::BottomLeft:
        hudPos = sf::Vector2f(10.0f, windowSize.y - textBounds.size.y - 15.0f);
        break;
    case HudPosition::BottomRight:
        hudPos = sf::Vector2f(windowSize.x - textBounds.size.x - 15.0f, windowSize.y - textBounds.size.y - 15.0f);
        break;
    }

    text.setPosition(hudPos);

    // draws the background if transparency is enabled
    if (hudTransparency)
    {
        sf::RectangleShape background(sf::Vector2f(textBounds.size.x + 10, textBounds.size.y + 10));
        background.setPosition({hudPos.x - 5, hudPos.y - 5});
        background.setFillColor(sf::Color(0, 0, 0, 100));
        window.draw(background);
    }

    window.draw(text);
}

void drawSimpleHUD(sf::RenderWindow &window, sf::Font &font, const SimulationSettings &settings,
                   const MouseSettings &mouseSettings, int agentCount,
                   SpeciesMode speciesMode, int randomCount)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);

    oss << " Physarum Simulation (Modern) " << "\n";
    oss << "Agents: " << agentCount << "\n";
    oss << "Resolution: " << settings.width << "x" << settings.height << "\n\n";

    oss << " Trail Settings " << "\n";
    oss << "Trail Weight [5/6]: " << settings.trailWeight << "\n";
    oss << "Decay Rate [3/4]: " << settings.decayRate << "\n";
    oss << "Diffuse Rate [1/2]: " << settings.diffuseRate << "\n";
    oss << "Display Threshold [T/Y]: " << settings.displayThreshold << "\n";
    oss << "Blur [B]: " << (settings.blurEnabled ? "ON" : "OFF") << "\n\n";

    oss << " Rendering " << "\n";
    oss << "Slime Shading [Shift+S]: " << (settings.slimeShadingEnabled ? "ON" : "OFF") << "\n";
    oss << "Anisotropic Splats [Shift+A]: " << (settings.anisotropicSplatsEnabled ? "ON" : "OFF") << "\n";
    oss << "Compliance k [U/P]: " << std::fixed << std::setprecision(2) << settings.complianceStrength << "\n";
    oss << "Damping d [;/' ]: " << std::fixed << std::setprecision(2) << settings.complianceDamping << "\n";
    oss << "Motion Inertia [Shift+I/Shift+O]: " << std::fixed << std::setprecision(2) << settings.motionInertia << "\n\n";

    if (!settings.speciesSettings.empty())
    {
        const auto &species = settings.speciesSettings[0];
        oss << " Movement Settings " << "\n";
        oss << "Move Speed [E/R]: " << species.moveSpeed << "\n";
        oss << "Turn Speed [Q/W]: " << species.turnSpeed << "\n";
        oss << "Sensor Angle [7/8]: " << species.sensorAngleSpacing << "\n";
        oss << "Sensor Distance [9/0]: " << species.sensorOffsetDistance << "\n\n";

        // emergence & reproduction*
        oss << " Emergence & Reproduction " << "\n";
        oss << "Mating [Shift+X]: " << (species.matingEnabled ? "ON" : "OFF")
            << "  |  Splitting [Shift+Z]: " << (species.splittingEnabled ? "ON" : "OFF")
            << "  |  Cross-species [Shift+C]: " << (species.crossSpeciesMating ? "ON" : "OFF") << "\n";
        oss << "Radius [[/]]: " << species.matingRadius
            << "  |  Mutation [-/=]: " << species.hybridMutationRate << "\n";
        oss << "Rebirth: " << (species.rebirthEnabled ? "ON" : "OFF")
            << "  |  Lifespan(s): " << species.lifespanSeconds << "\n\n";
    }

    oss << " Mouse Interaction " << "\n";
    oss << "Food Strength [Shift+↑/↓]: " << mouseSettings.foodStrength << "\n";
    oss << "Brush Radius: " << mouseSettings.brushRadius << " px" << "\n\n";

    oss << " Species Mode " << "\n";
    oss << "Current: " << getSpeciesModeString(speciesMode, randomCount) << "\n";

    // mode specific information here
    if (speciesMode == SpeciesMode::Classic5)
    {
        oss << "Species: Red(Aggressive), Blue(Cooperative), Green(Avoiding),\n";
        oss << "         Yellow(Alien), Magenta(Anti-Alien)\n";
        oss << "Intensity: [I/O] All +/- | [H/J/K/,/.] Individual Cycle\n";
        oss << "Reroll: [G] Random combination of species\n";
    }
    else if (speciesMode == SpeciesMode::Random)
    {
        oss << "Random Count: " << randomCount << " species\n";
        oss << "Controls: [N/V] Count +/- | [G] Regenerate\n";
        oss << "Intensity: [I/O] All +/-\n";
    }
    else if (speciesMode == SpeciesMode::Single)
    {
        oss << "Single species mode\n";
        oss << "Intensity: [H] Cycle | [I/O] +/-\n";
    }

    if (!settings.speciesSettings.empty())
    {
        oss << "Current Intensity: " << settings.speciesSettings[0].behaviorIntensity << " (1=mild, 4=extreme)\n";
    }
    oss << "\n";

    oss << " Controls " << "\n";
    oss << "[Space] Reset | [↑/↓] Agent Count" << "\n";
    oss << "[S] Save | [L] Load | [Z] Randomize (plain) | [Shift+Z] Toggle Splitting" << "\n";
    oss << "[C] Change Color | [M] Cycle Species Mode" << "\n";

    if (speciesMode == SpeciesMode::Random)
    {
        oss << "[N/V] Random Count +/- | [G] Regenerate Random" << "\n";
    }

    oss << "[B] Toggle Blur | [Shift+S] Slime Shading | [Shift+A] Aniso Splats | [U/P] k -/+ | [;/' ] d -/+ | [Shift+I/Shift+O] Inertia +/- | [F] HUD Mode | [H] Hide HUD" << "\n";
    oss << "[Shift+1-4] HUD Position | [X] HUD Transparency" << "\n";
    oss << "[Shift+X] Toggle Mating | [Shift+C] Toggle Cross-species" << "\n";
    oss << "[[/]] Mating Radius | [-/=] Hybrid Mutation" << "\n";
    oss << "[Left Mouse] Food | [Right Mouse] Repellent" << "\n";

    sf::Text text(font, oss.str(), 14);
    text.setFillColor(sf::Color::White);
    text.setOutlineColor(sf::Color::Black);
    text.setOutlineThickness(1.5f);

    // positions the hud based on the current position setting
    sf::FloatRect textBounds = text.getLocalBounds();
    sf::Vector2f windowSize(static_cast<float>(window.getSize().x), static_cast<float>(window.getSize().y));
    sf::Vector2f hudPos;

    switch (currentHudPosition)
    {
    case HudPosition::TopLeft:
        hudPos = sf::Vector2f(10.0f, 10.0f);
        break;
    case HudPosition::TopRight:
        hudPos = sf::Vector2f(windowSize.x - textBounds.size.x - 15.0f, 10.0f);
        break;
    case HudPosition::BottomLeft:
        hudPos = sf::Vector2f(10.0f, windowSize.y - textBounds.size.y - 15.0f);
        break;
    case HudPosition::BottomRight:
        hudPos = sf::Vector2f(windowSize.x - textBounds.size.x - 15.0f, windowSize.y - textBounds.size.y - 15.0f);
        break;
    }

    text.setPosition(hudPos);

    // draws background with configurable transparency
    sf::RectangleShape background(sf::Vector2f(textBounds.size.x + 20, textBounds.size.y + 20));
    background.setPosition({hudPos.x - 10, hudPos.y - 10});
    if (hudTransparency)
    {
        background.setFillColor(sf::Color(0, 0, 0, 120)); // more transparent
    }
    else
    {
        background.setFillColor(sf::Color(0, 0, 0, 200)); // more opaque
    }

    window.draw(background);
    window.draw(text);
}

// helper func to scale behavior values based on intensity (1-4)
float scaleBehaviorValue(float baseValue, int intensity, float minScale = 0.3f, float maxScale = 2.0f)
{
    // intensity levels: 1 = mild, 2 = default, 3 = strong, 4 = extreme
    float scale = 1.0f;

    switch (intensity)
    {
    case 1:
        scale = minScale;
        break; // mild: 30% of base
    case 2:
        scale = 1.0f;
        break; // default: 100% of base
    case 3:
        scale = 1.5f;
        break; // strong: 150% of base
    case 4:
        scale = maxScale;
        break; // extreme: 200% of base
    default:
        scale = 1.0f;
        break;
    }

    return baseValue * scale;
}

// generate a random species with completely random behaviors
// curated species archetypes inspired by some really cool c#/go program configs for emergent behaviors
struct SpeciesArchetype
{
    std::string name;
    sf::Color color;
    float moveSpeed;
    float turnSpeed;
    float sensorAngleSpacing;
    float sensorOffsetDistance;
    float attractionToSelf;
    float attractionToOthers;
    float repulsionFromOthers;
    int behaviorIntensity;
    std::string description;
};

static std::vector<SpeciesArchetype> getSpeciesArchetypes()
{
    return {
        // fast explorers - wide sensing, rapid movement
        {"Velocity Scout", sf::Color(255, 100, 100), 3.5f, 45.0f, 60.0f, 20.0f, 0.8f, -0.2f, 0.1f, 3, "High-speed explorer with wide sensor range"},
        {"Sprint Runner", sf::Color(100, 255, 100), 4.0f, 30.0f, 45.0f, 25.0f, 1.2f, 0.0f, 0.0f, 4, "Ultra-fast linear movement specialist"},

        // meandering architects - precise builders with tight turns
        {"Spiral Architect", sf::Color(100, 100, 255), 1.2f, 85.0f, 25.0f, 35.0f, 2.0f, 0.5f, 0.0f, 2, "Creates intricate spiral patterns with tight turns"},
        {"Maze Builder", sf::Color(255, 255, 100), 1.8f, 60.0f, 30.0f, 40.0f, 1.5f, 0.3f, 0.2f, 2, "Constructs complex maze-like structures"},

        // adaptive foragers - dynamic sensing and moderate speeds
        {"Smart Forager", sf::Color(255, 100, 255), 2.3f, 25.0f, 35.0f, 30.0f, 1.0f, 0.8f, 0.0f, 3, "Adaptive behavior with balanced exploration"},
        {"Resource Hunter", sf::Color(100, 255, 255), 2.8f, 40.0f, 50.0f, 22.0f, 0.9f, 1.2f, 0.1f, 3, "Aggressive resource acquisition specialist"},

        // social coordinators - attraction/repulsion specialists
        {"Colony Former", sf::Color(200, 150, 255), 2.0f, 15.0f, 20.0f, 35.0f, 1.8f, 1.5f, 0.0f, 2, "Forms tight colonies through strong mutual attraction"},
        {"Territory Guardian", sf::Color(255, 150, 100), 2.5f, 50.0f, 40.0f, 28.0f, 1.2f, -0.5f, 0.8f, 3, "Maintains territorial boundaries through repulsion"},

        // chaos agents - unpredictable and disruptive
        {"Chaos Walker", sf::Color(150, 255, 150), 3.2f, 120.0f, 70.0f, 15.0f, 0.2f, -0.8f, 0.3f, 4, "Highly unpredictable with extreme turn rates"},
        {"Quantum Dancer", sf::Color(255, 200, 200), 2.7f, 95.0f, 55.0f, 12.0f, 0.5f, 0.2f, -0.2f, 4, "Erratic movement with quantum-like behavior"},

        // precision specialists - fine tuned for specific patterns
        {"Vein Tracer", sf::Color(200, 100, 150), 1.5f, 12.0f, 18.0f, 45.0f, 2.2f, 0.1f, 0.0f, 1, "Creates delicate vein like networks"},
        {"Wave Generator", sf::Color(150, 200, 100), 2.1f, 8.0f, 22.0f, 38.0f, 1.6f, 0.4f, 0.1f, 2, "Generates flowing wave-like patterns"},

        // symbiotic pairs - designed to interact
        {"Symbiont Alpha", sf::Color(180, 100, 200), 2.4f, 35.0f, 28.0f, 32.0f, 0.8f, 1.8f, 0.0f, 3, "Forms symbiotic relationships with other species"},
        {"Symbiont Beta", sf::Color(100, 180, 200), 2.2f, 28.0f, 32.0f, 28.0f, 0.6f, 2.0f, 0.1f, 2, "Complementary partner for symbiotic interactions"},

        // network builders - connection specialists
        {"Bridge Builder", sf::Color(220, 220, 100), 1.9f, 22.0f, 38.0f, 42.0f, 1.4f, 0.9f, 0.0f, 2, "Specializes in connecting disparate regions"},
        {"Node Connector", sf::Color(100, 220, 220), 2.6f, 18.0f, 42.0f, 26.0f, 1.1f, 1.1f, 0.2f, 3, "Creates hub-and-spoke network patterns"},

        // environmental adapters
        {"Climate Shifter", sf::Color(255, 180, 120), 2.0f, 65.0f, 48.0f, 20.0f, 1.0f, 0.0f, 0.4f, 3, "Adapts movement based on environmental density"},
        {"Pressure Sensor", sf::Color(120, 255, 180), 2.8f, 42.0f, 33.0f, 36.0f, 0.7f, 0.6f, 0.6f, 3, "Responds dynamically to population pressure"},

        // exotic behaviors - inspired by real slime mold research
        {"Memory Keeper", sf::Color(200, 200, 255), 1.6f, 20.0f, 25.0f, 50.0f, 2.5f, 0.2f, 0.0f, 1, "Maintains long-term trail memory"},
        {"Efficiency Expert", sf::Color(255, 200, 200), 3.0f, 35.0f, 40.0f, 24.0f, 1.3f, 0.4f, 0.3f, 4, "Optimizes pathfinding with minimal waste"},

        // edge cases - extreme parameter combinations
        {"Micro Manager", sf::Color(180, 180, 255), 0.8f, 5.0f, 12.0f, 55.0f, 3.0f, 0.1f, 0.0f, 1, "Ultra-slow, precise movements with maximum trail affinity"},
        {"Hyperactive", sf::Color(255, 180, 180), 4.5f, 150.0f, 80.0f, 10.0f, 0.1f, -1.0f, 0.5f, 4, "Extreme speed with chaotic turning"},
    };
}

SimulationSettings::SpeciesSettings generateRandomSpecies(int speciesIndex)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static auto archetypes = getSpeciesArchetypes();

    // strategy: mix of pure archetypes (70%) and mutations (30%)
    std::uniform_real_distribution<float> strategyDist(0.0f, 1.0f);
    std::uniform_int_distribution<int> archetypeChoice(0, archetypes.size() - 1);

    SimulationSettings::SpeciesSettings species;

    if (strategyDist(gen) < 0.7f)
    {
        // pure archetype
        auto archetype = archetypes[archetypeChoice(gen)];
        species.color = archetype.color;
        species.moveSpeed = archetype.moveSpeed;
        species.turnSpeed = archetype.turnSpeed;
        species.sensorAngleSpacing = archetype.sensorAngleSpacing;
        species.sensorOffsetDistance = archetype.sensorOffsetDistance;
        species.attractionToSelf = archetype.attractionToSelf;
        species.attractionToOthers = archetype.attractionToOthers;
        species.repulsionFromOthers = archetype.repulsionFromOthers;
        species.behaviorIntensity = archetype.behaviorIntensity;

        std::cout << "Generated archetype: " << archetype.name << " - " << archetype.description << std::endl;
    }
    else
    {
        // mutations
        auto archetype1 = archetypes[archetypeChoice(gen)];
        auto archetype2 = archetypes[archetypeChoice(gen)];

        std::uniform_real_distribution<float> blendFactor(0.3f, 0.7f);
        std::uniform_real_distribution<float> mutationFactor(0.8f, 1.2f);

        float blend = blendFactor(gen);

        // blend colors
        species.color.r = static_cast<uint8_t>((archetype1.color.r * blend + archetype2.color.r * (1 - blend)) * mutationFactor(gen));
        species.color.g = static_cast<uint8_t>((archetype1.color.g * blend + archetype2.color.g * (1 - blend)) * mutationFactor(gen));
        species.color.b = static_cast<uint8_t>((archetype1.color.b * blend + archetype2.color.b * (1 - blend)) * mutationFactor(gen));

        // clamp colors to valid range
        species.color.r = std::min(255, std::max(50, (int)species.color.r));
        species.color.g = std::min(255, std::max(50, (int)species.color.g));
        species.color.b = std::min(255, std::max(50, (int)species.color.b));

        // blend and mutate parameters
        species.moveSpeed = std::max(0.3f, (archetype1.moveSpeed * blend + archetype2.moveSpeed * (1 - blend)) * mutationFactor(gen));
        species.turnSpeed = std::max(3.0f, (archetype1.turnSpeed * blend + archetype2.turnSpeed * (1 - blend)) * mutationFactor(gen));
        species.sensorAngleSpacing = std::max(8.0f, std::min(90.0f, (archetype1.sensorAngleSpacing * blend + archetype2.sensorAngleSpacing * (1 - blend)) * mutationFactor(gen)));
        species.sensorOffsetDistance = std::max(5.0f, std::min(60.0f, (archetype1.sensorOffsetDistance * blend + archetype2.sensorOffsetDistance * (1 - blend)) * mutationFactor(gen)));
        species.attractionToSelf = std::max(-1.0f, std::min(3.0f, (archetype1.attractionToSelf * blend + archetype2.attractionToSelf * (1 - blend)) * mutationFactor(gen)));
        species.attractionToOthers = std::max(-2.0f, std::min(2.5f, (archetype1.attractionToOthers * blend + archetype2.attractionToOthers * (1 - blend)) * mutationFactor(gen)));
        species.repulsionFromOthers = std::max(-0.5f, std::min(1.0f, (archetype1.repulsionFromOthers * blend + archetype2.repulsionFromOthers * (1 - blend)) * mutationFactor(gen)));
        species.behaviorIntensity = std::max(1, std::min(4, static_cast<int>((archetype1.behaviorIntensity * blend + archetype2.behaviorIntensity * (1 - blend)) * mutationFactor(gen))));

        std::cout << "Generated hybrid mutant: " << archetype1.name << " x " << archetype2.name << std::endl;
    }

    // distinctive colors per species index to avoid confusion
    switch (speciesIndex % 8)
    {
    case 0: // boost red channel
        species.color.r = std::min(255, static_cast<int>(species.color.r * 1.2f));
        break;
    case 1: // boost green channel
        species.color.g = std::min(255, static_cast<int>(species.color.g * 1.2f));
        break;
    case 2: // boost blue channel
        species.color.b = std::min(255, static_cast<int>(species.color.b * 1.2f));
        break;
    case 3: // cyan emphasis
        species.color.g = std::min(255, static_cast<int>(species.color.g * 1.1f));
        species.color.b = std::min(255, static_cast<int>(species.color.b * 1.1f));
        break;
    case 4: // magenta emphasis
        species.color.r = std::min(255, static_cast<int>(species.color.r * 1.1f));
        species.color.b = std::min(255, static_cast<int>(species.color.b * 1.1f));
        break;
    case 5: // yellow emphasis
        species.color.r = std::min(255, static_cast<int>(species.color.r * 1.1f));
        species.color.g = std::min(255, static_cast<int>(species.color.g * 1.1f));
        break;
    default:
        // keep original colors for additional species
        break;
    }

    return species;
}

void setupSingleSpecies(SimulationSettings &settings)
{
    settings.speciesSettings.clear();

    // single default species
    SimulationSettings::SpeciesSettings species;
    species.color = sf::Color(255, 230, 0); // yellow default
    species.moveSpeed = 2.4f;
    species.turnSpeed = 30.0f;
    species.sensorAngleSpacing = 22.5f;
    species.sensorOffsetDistance = 39.0f;
    species.attractionToSelf = 1.0f;
    species.attractionToOthers = 0.0f;
    species.repulsionFromOthers = 0.0f;
    species.behaviorIntensity = 2;

    settings.speciesSettings.push_back(species);

    std::cout << "Single species mode activated!" << std::endl;
}

void setupClassic5Species(SimulationSettings &settings)
{
    // if we have an active species combination then use that (works for any number of species)
    if (!activeSpeciesIndices.empty())
    {
        // use the reroll combination so it preserves existing intensities
        std::vector<int> existingIntensities;
        for (const auto &species : settings.speciesSettings)
        {
            existingIntensities.push_back(species.behaviorIntensity);
        }

        settings.speciesSettings.clear();

        for (size_t i = 0; i < activeSpeciesIndices.size(); ++i)
        {
            int selectedIndex = activeSpeciesIndices[i];
            SimulationSettings::SpeciesSettings newSpecies;

            // uses existing intensity if available, otherwise default to 2
            int intensity = (i < existingIntensities.size()) ? existingIntensities[i] : 2;

            if (selectedIndex == 0) // RED - bullies
            {
                // RED: Territorial energy thieves / PREDATORS 
                // food economy: eats own trail and other species trails (predator)
                // - only mate with their own kind
                // - hunts (eats) other species food
                // - conditional rebirth if population drops below 30%
                newSpecies.color = sf::Color(255, 80, 80);
                newSpecies.behaviorIntensity = intensity;
                newSpecies.moveSpeed = scaleBehaviorValue(2.2f, intensity, 1.5f, 3.5f);
                newSpecies.turnSpeed = scaleBehaviorValue(50.0f, intensity, 30.0f, 80.0f);
                newSpecies.sensorAngleSpacing = scaleBehaviorValue(35.0f, intensity, 25.0f, 60.0f);
                newSpecies.sensorOffsetDistance = scaleBehaviorValue(18.0f, intensity, 12.0f, 30.0f);
                newSpecies.attractionToSelf = scaleBehaviorValue(2.0f, intensity, 1.5f, 3.0f);     // like own kind
                newSpecies.attractionToOthers = scaleBehaviorValue(-1.5f, intensity, -0.8f, -2.5f); // hate others
                
                // food economy settings - RED is a predator expensive but powerful
                newSpecies.foodEconomyEnabled = true;
                newSpecies.eatRate = 5.0f;              // eats trail - creates real scarcity!
                newSpecies.movementEnergyCost = 0.025f;  // very high cost - aggression is expensive!
                newSpecies.canEatOtherTrails = true;    // predator - steals food from other trails!
                newSpecies.trailFoodValue = 0.01f;      // trail gives little energy (must eat lots)
                
                // reproduction: mate only with own kind
                newSpecies.splittingEnabled = false;
                newSpecies.matingEnabled = true;
                newSpecies.crossSpeciesMating = false;     // only mate with reds!
                newSpecies.matingRadius = 20.0f;
                newSpecies.matingEnergyCost = 0.15f;
                newSpecies.offspringEnergy = 0.6f;
                newSpecies.matingEnergyBonus = 0.0f;       // no bonus for mating
                
                // death: HardDeath BUT conditional rebirth at 30% population
                newSpecies.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::HardDeath;
                newSpecies.lifespanSeconds = 50.0f;       
                newSpecies.rebirthEnabled = false;
                newSpecies.conditionalRebirthEnabled = true;
                newSpecies.rebirthPopulationThreshold = 0.3f;  // rebirth if below 30%
                newSpecies.rebirthEnergy = 0.7f;
                
                // legacy energy  x
                newSpecies.canStealEnergy = false;       
                newSpecies.canGiveEnergy = false;
                newSpecies.energyDecayPerStep = 0.0f;      // ignored w food economy
                newSpecies.energyGainPerNeighbor = 0.0f;   // ignored
                newSpecies.passiveEnergyRegen = 0.0f;      // ignored
                
                newSpecies.sameSpeciesCohesionBoost = 1.5f;
                newSpecies.separateFromSameSpecies = false;
            }
            else if (selectedIndex == 1) // BLUE - altruistic helper
            {
                // BLUE: generous trail layers / farmers 
                // food economy: deposits extra thick trails (more food for ecosystem)
                // - can only mate with other species (parasitic like reproduction)
                // - eats own trail only (not predator)
                // - deposits more than it eats = net food production
                newSpecies.color = sf::Color(80, 150, 255);
                newSpecies.behaviorIntensity = intensity;
                newSpecies.moveSpeed = scaleBehaviorValue(1.5f, intensity, 1.0f, 2.5f);
                newSpecies.turnSpeed = scaleBehaviorValue(30.0f, intensity, 20.0f, 50.0f);
                newSpecies.sensorAngleSpacing = scaleBehaviorValue(25.0f, intensity, 15.0f, 40.0f);
                newSpecies.sensorOffsetDistance = scaleBehaviorValue(15.0f, intensity, 10.0f, 25.0f);
                newSpecies.attractionToSelf = scaleBehaviorValue(1.5f, intensity, 1.0f, 2.0f);
                newSpecies.attractionToOthers = scaleBehaviorValue(2.5f, intensity, 1.5f, 4.0f);  // love others :)
                
                // food economy settings - BLUE is a "farmer" deposits more than it eats
                newSpecies.foodEconomyEnabled = true;
                newSpecies.eatRate = 2.0f;              // moderate consumer
                newSpecies.movementEnergyCost = 0.015f;  // moderate cost
                newSpecies.canEatOtherTrails = false;   // only eats own trail
                newSpecies.trailFoodValue = 0.015f;     // efficient - gets more from less
                
                // reproduction: again mate only with other species (parasitic), split as backup
                newSpecies.splittingEnabled = true;        // can split when alone
                newSpecies.splitEnergyThreshold = 0.5f;    // higher threshold - harder to split
                newSpecies.splitCooldownTime = 8.0f;       // long cooldown - no spam splitting
                newSpecies.matingEnabled = true;
                newSpecies.crossSpeciesMating = true;
                newSpecies.onlyMateWithOtherSpecies = true; // parasitic - only mate with other species!
                newSpecies.matingRadius = 25.0f;
                newSpecies.matingEnergyCost = 0.10f;       // Lower mating cost
                newSpecies.offspringEnergy = 0.55f;        // Children start above split threshold!
                newSpecies.matingEnergyBonus = 0.25f;      // Big bonus from successful cross-species mating
                
                newSpecies.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::HardDeath;
                newSpecies.lifespanSeconds = 55.0f;       
                newSpecies.rebirthEnabled = false;
                newSpecies.conditionalRebirthEnabled = false;
                
                // legacy energy (disabled with food economy)
                newSpecies.canStealEnergy = false;
                newSpecies.canGiveEnergy = false;          // food economy replaces this - they "give" by depositing thick trails
                newSpecies.energyDecayPerStep = 0.0f;
                newSpecies.energyGainPerNeighbor = 0.0f;
                newSpecies.passiveEnergyRegen = 0.0f;
                
                newSpecies.sameSpeciesCohesionBoost = 1.2f;
            }
            else if (selectedIndex == 2) // GREEN - LONER TYPE NOMADS
            {
                // GREEN: self sufficient loners / hermits 
                // food economy: create and eats own trail loop
                // - avoid everyone at all costs
                // - only reproduce asexually (split/bud)
                // - pre-death budding (split right before dying)
                // - ultra efficient movement
                newSpecies.color = sf::Color(80, 255, 80);
                newSpecies.behaviorIntensity = intensity;
                newSpecies.moveSpeed = scaleBehaviorValue(3.5f, intensity, 2.5f, 5.0f);   // fast - escape artists
                newSpecies.turnSpeed = scaleBehaviorValue(90.0f, intensity, 60.0f, 130.0f);
                newSpecies.sensorAngleSpacing = scaleBehaviorValue(60.0f, intensity, 40.0f, 100.0f);
                newSpecies.sensorOffsetDistance = scaleBehaviorValue(25.0f, intensity, 18.0f, 35.0f);
                newSpecies.attractionToSelf = scaleBehaviorValue(0.3f, intensity, -0.2f, 0.8f);    // slight attraction to own kind (need to get energy)
                newSpecies.attractionToOthers = scaleBehaviorValue(-3.0f, intensity, -2.0f, -5.0f); // strongly avoid others
                
                // food economy settings - GREEN is a hermit, efficient but must keep moving
                newSpecies.foodEconomyEnabled = true;
                newSpecies.eatRate = 3.0f;              // moderate high consumer
                newSpecies.movementEnergyCost = 0.012f;  // low-ish cost - efficient loner
                newSpecies.canEatOtherTrails = false;   // self-sufficient - eats own trail only
                newSpecies.trailFoodValue = 0.012f;     // normal efficiency
                
                // reproduction: split only (asexual) - no mating
                newSpecies.splittingEnabled = true;
                newSpecies.splitEnergyThreshold = 0.45f;   // moderate threshold
                newSpecies.splitCooldownTime = 6.0f;       // longer cooldown
                newSpecies.matingEnabled = false;
                newSpecies.crossSpeciesMating = false;
                newSpecies.offspringEnergy = 0.5f;         // children start with decent energy
                
                // pre death budding - very aggressive
                newSpecies.preDeathBuddingEnabled = true;
                newSpecies.preDeathBudThreshold = 0.12f;   // bud when energy drops to 12%. a last resort
                
                // death: HardDeath... NO rebirth
                newSpecies.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::HardDeath;
                newSpecies.lifespanSeconds = 50.0f;        // (ignored with food economy)
                newSpecies.rebirthEnabled = false;
                newSpecies.conditionalRebirthEnabled = false;
                
                // legacy energy (disabled with food economy)
                newSpecies.canStealEnergy = false;
                newSpecies.canGiveEnergy = false;
                newSpecies.energyDecayPerStep = 0.0f;
                newSpecies.energyGainPerNeighbor = 0.0f;
                newSpecies.passiveEnergyRegen = 0.0f;
                
                newSpecies.sameSpeciesCohesionBoost = 0.8f;
                newSpecies.separateFromSameSpecies = false; // dont separate from own kind (need energy)
                newSpecies.separationWeight = 1.5f;
            }
            else if (selectedIndex == 3) // yellow
            {
                newSpecies.color = sf::Color(255, 255, 80);
                newSpecies.behaviorIntensity = intensity;
                newSpecies.moveSpeed = scaleBehaviorValue(2.5f, intensity, 1.0f, 4.0f);               // unpredictable speed bursts
                newSpecies.turnSpeed = scaleBehaviorValue(140.0f, intensity, 80.0f, 200.0f);          // extreme erratic movement
                newSpecies.sensorAngleSpacing = scaleBehaviorValue(80.0f, intensity, 45.0f, 150.0f);  // wide perception
                newSpecies.sensorOffsetDistance = scaleBehaviorValue(25.0f, intensity, 15.0f, 40.0f); // long range sensing
                newSpecies.attractionToSelf = scaleBehaviorValue(0.3f, intensity, -0.5f, 1.5f);       // unpredictable self interaction... sometimes swarms, sometimes repels
                newSpecies.attractionToOthers = scaleBehaviorValue(0.8f, intensity, -1.5f, 2.5f);     // should be chaotic interaction
                // Yellow: chaotic aliens burst into spores on death!
                newSpecies.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::SporeBurst;
                newSpecies.lifespanSeconds = 25.0f;  // short chaotic lives
                newSpecies.sporeCount = 4;           // more spores for sustainability
                newSpecies.sporeRadius = 20.0f;
                newSpecies.sporeMutationRate = 0.08f;
                newSpecies.splittingEnabled = false;
                newSpecies.matingEnabled = false;  // too chaotic to mate
                newSpecies.offspringEnergy = 0.8f;   // spores start with good energy
            }
            else if (selectedIndex == 4) // magenta
            {
                newSpecies.color = sf::Color(255, 80, 255);
                newSpecies.behaviorIntensity = intensity;
                newSpecies.moveSpeed = scaleBehaviorValue(1.8f, intensity, 1.2f, 2.8f);               // steady, regulated movement; brings stability
                newSpecies.turnSpeed = scaleBehaviorValue(40.0f, intensity, 25.0f, 65.0f);            // controlled turning; methodical and precise
                newSpecies.sensorAngleSpacing = scaleBehaviorValue(30.0f, intensity, 20.0f, 50.0f);   // focused sensing for order detection
                newSpecies.sensorOffsetDistance = scaleBehaviorValue(15.0f, intensity, 10.0f, 25.0f); // moderate range for local order enforcement
                newSpecies.attractionToSelf = scaleBehaviorValue(2.0f, intensity, 1.5f, 3.0f);        // strong self organization
                newSpecies.attractionToOthers = scaleBehaviorValue(1.0f, intensity, 0.5f, 1.8f);      // moderate attraction
                // Magenta: order enforcers can rebirth to maintain structure
                newSpecies.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::Rebirth;
                newSpecies.rebirthEnabled = true;
                newSpecies.lifespanSeconds = 50.0f;
                newSpecies.splittingEnabled = false;
                newSpecies.matingEnabled = true;
                newSpecies.matingRadius = 12.0f;
                newSpecies.crossSpeciesMating = false;  // orderly, same-species only
                newSpecies.matingEnergyCost = 0.12f;
                newSpecies.offspringEnergy = 0.7f;
                newSpecies.energyDecayPerStep = 0.001f;
                newSpecies.energyGainPerNeighbor = 0.0025f;
            }
            else if (selectedIndex == 5) // black
            {
                // bright toxic green for maximum visibility
                newSpecies.color = sf::Color(50, 255, 50); // toxic bright green color
                newSpecies.behaviorIntensity = intensity;
                newSpecies.moveSpeed = scaleBehaviorValue(2.5f, intensity, 1.0f, 4.5f);               // much faster hunting speed
                newSpecies.turnSpeed = scaleBehaviorValue(120.0f, intensity, 80.0f, 180.0f);          // extremely aggressive turning for quick direction changes
                newSpecies.sensorAngleSpacing = scaleBehaviorValue(60.0f, intensity, 40.0f, 120.0f);  // wide hunting net
                newSpecies.sensorOffsetDistance = scaleBehaviorValue(30.0f, intensity, 20.0f, 50.0f); // longer reach for detecting from far away
                newSpecies.attractionToSelf = scaleBehaviorValue(-1.5f, intensity, -2.5f, -0.5f);     // strong self avoidance to spread out for hunting
                newSpecies.attractionToOthers = scaleBehaviorValue(4.0f, intensity, 2.5f, 6.0f);      // massive attraction to other species
                // Parasitic: hard death, but can split to spread infection
                newSpecies.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::HardDeath;
                newSpecies.lifespanSeconds = 30.0f;
                newSpecies.splittingEnabled = true;
                newSpecies.splitEnergyThreshold = 1.5f;  // high threshold to split
                newSpecies.splitCooldownTime = 5.0f;     // long cooldown
                newSpecies.matingEnabled = false;
            }
            else if (selectedIndex == 6) // crimson
            {
                // deep blood red
                newSpecies.color = sf::Color(200, 0, 0);
                newSpecies.behaviorIntensity = intensity;
                newSpecies.moveSpeed = scaleBehaviorValue(3.5f, intensity, 2.0f, 5.5f);
                newSpecies.turnSpeed = scaleBehaviorValue(150.0f, intensity, 100.0f, 200.0f);         // violently aggressive turning
                newSpecies.sensorAngleSpacing = scaleBehaviorValue(90.0f, intensity, 60.0f, 150.0f);  // extremely wide radius
                newSpecies.sensorOffsetDistance = scaleBehaviorValue(35.0f, intensity, 25.0f, 50.0f); // long range sensing
                newSpecies.attractionToSelf = scaleBehaviorValue(1.5f, intensity, 1.0f, 2.5f);        // strong self organization
                newSpecies.attractionToOthers = scaleBehaviorValue(-5.0f, intensity, -3.5f, -7.0f);
                // Crimson death bringers: violent spore burst on death!
                newSpecies.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::SporeBurst;
                newSpecies.lifespanSeconds = 20.0f;  // live fast die young
                newSpecies.sporeCount = 5;           // violent burst
                newSpecies.sporeRadius = 25.0f;      // wide spread
                newSpecies.sporeMutationRate = 0.15f;
                newSpecies.splittingEnabled = false;
                newSpecies.matingEnabled = false;
            }
            else if (selectedIndex == 7) // white
            {
                newSpecies.color = sf::Color(255, 255, 255);
                newSpecies.behaviorIntensity = intensity;
                newSpecies.moveSpeed = scaleBehaviorValue(2.2f, intensity, 1.5f, 3.5f);               // fast response
                newSpecies.turnSpeed = scaleBehaviorValue(60.0f, intensity, 40.0f, 100.0f);           // responsive but controlled turning
                newSpecies.sensorAngleSpacing = scaleBehaviorValue(45.0f, intensity, 30.0f, 80.0f);   // wide protective sensing
                newSpecies.sensorOffsetDistance = scaleBehaviorValue(25.0f, intensity, 18.0f, 40.0f); // long range threat detection
                newSpecies.attractionToSelf = scaleBehaviorValue(1.8f, intensity, 1.2f, 2.8f);        // strong self organization
                newSpecies.attractionToOthers = scaleBehaviorValue(3.5f, intensity, 2.5f, 5.0f);
                // White guardians: rebirth with long lifespan to protect
                newSpecies.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::Rebirth;
                newSpecies.rebirthEnabled = true;
                newSpecies.lifespanSeconds = 90.0f;  // longest lived - protectors
                newSpecies.splittingEnabled = false;
                newSpecies.matingEnabled = true;
                newSpecies.matingRadius = 15.0f;
            }

            newSpecies.repulsionFromOthers = 0.0f;
            settings.speciesSettings.push_back(newSpecies);
        }
        return;
    }

    std::cout << "Classic 5+ species mode activated!" << std::endl;
    for (size_t i = 0; i < settings.speciesSettings.size(); ++i)
    {
        const auto &species = settings.speciesSettings[i];
        int originalIndex = i < activeSpeciesIndices.size() ? activeSpeciesIndices[i] : i;
        std::vector<std::string> speciesNames = {
            "Red: Strategic Warlord", "Blue: Harmony Builder", "Green: Nomadic Wanderer",
            "Yellow: Quantum Alien", "Magenta: Order Enforcer",
            "Black/Green: Parasitic Nightmare", "Crimson: Death Bringer", "White: Guardian Angel"};
        std::vector<std::string> speciesLabels = {"Red", "Blue", "Green", "Yellow", "Magenta", "Black", "Crimson", "White"};

        if (originalIndex < 8)
        {
            std::cout << "  " << speciesLabels[originalIndex] << " " << speciesNames[originalIndex] << " (intensity " << species.behaviorIntensity << ")" << std::endl;
        }
    }
}

void setupRandomSpecies(SimulationSettings &settings, int numSpecies)
{
    settings.speciesSettings.clear();

    std::cout << "Generating " << numSpecies << " random species with unique behaviors..." << std::endl;

    for (int i = 0; i < numSpecies; ++i)
    {
        SimulationSettings::SpeciesSettings randomSpecies = generateRandomSpecies(i);
        settings.speciesSettings.push_back(randomSpecies);

        std::cout << "  Species " << (i + 1) << ": RGB("
                  << (int)randomSpecies.color.r << ","
                  << (int)randomSpecies.color.g << ","
                  << (int)randomSpecies.color.b << ") "
                  << "Speed:" << std::fixed << std::setprecision(1) << randomSpecies.moveSpeed
                  << " Turn:" << randomSpecies.turnSpeed << "°"
                  << " Self:" << randomSpecies.attractionToSelf
                  << " Others:" << randomSpecies.attractionToOthers << std::endl;
    }

    std::cout << "Random species generation complete!" << std::endl;
}

void setupMultiSpecies(SimulationSettings &settings, int numSpecies)
{
    // preserve existing intensity values if they exist
    std::vector<int> existingIntensities;
    for (const auto &species : settings.speciesSettings)
    {
        existingIntensities.push_back(species.behaviorIntensity);
    }

    settings.speciesSettings.clear();

    if (numSpecies >= 1)
    {
        // red species: aggressive territorial (xenophobic)
        SimulationSettings::SpeciesSettings species1;
        species1.color = sf::Color(255, 80, 80);                                                   // bright red
        species1.behaviorIntensity = existingIntensities.size() >= 1 ? existingIntensities[0] : 2; // use existing or default

        // base values for default intensity (level 2)
        float baseSpeed = 2.0f;
        float baseTurnSpeed = 45.0f;
        float baseAngleSpacing = 30.0f;
        float baseOffsetDistance = 12.0f;
        float baseSelfAttraction = 1.8f;
        float baseOtherAttraction = -0.8f;

        // apply intensity scaling
        species1.moveSpeed = scaleBehaviorValue(baseSpeed, species1.behaviorIntensity);
        species1.turnSpeed = scaleBehaviorValue(baseTurnSpeed, species1.behaviorIntensity);
        species1.sensorAngleSpacing = scaleBehaviorValue(baseAngleSpacing, species1.behaviorIntensity);
        species1.sensorOffsetDistance = scaleBehaviorValue(baseOffsetDistance, species1.behaviorIntensity);
        species1.attractionToSelf = scaleBehaviorValue(baseSelfAttraction, species1.behaviorIntensity, 0.5f, 2.5f);
        species1.attractionToOthers = scaleBehaviorValue(baseOtherAttraction, species1.behaviorIntensity, -0.3f, -1.5f);
        species1.repulsionFromOthers = 0.0f;
        
        // REPRODUCTION: Red warriors - hard death, reproduce via splitting AND mating
        species1.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::HardDeath;
        species1.lifespanSeconds = 60.0f;
        species1.splittingEnabled = true;
        species1.splitEnergyThreshold = 0.55f;     // LOW threshold so offspring can split!
        species1.splitCooldownTime = 6.0f;         // Longer cooldown to balance
        species1.offspringEnergy = 0.7f;           // Children born ABOVE split threshold!
        species1.matingEnabled = true;
        species1.crossSpeciesMating = false;       // only mates with own kind
        species1.matingRadius = 15.0f;
        species1.matingEnergyCost = 0.15f;         // Lower mating cost
        species1.energyGainPerNeighbor = 0.002f;   // Warriors gain energy in packs
        species1.energyDecayPerStep = 0.0015f;     // Slower decay
        
        settings.speciesSettings.push_back(species1);
    }

    if (numSpecies >= 2)
    {
        // blue species: cooperative symbiotic (loves everyone :) )
        SimulationSettings::SpeciesSettings species2;
        species2.color = sf::Color(80, 150, 255);                                                  // bright blue
        species2.behaviorIntensity = existingIntensities.size() >= 2 ? existingIntensities[1] : 2; // use existing or default

        // base values for default intensity (level 2)
        float baseSpeed = 1.0f;
        float baseTurnSpeed = 20.0f;
        float baseAngleSpacing = 15.0f;
        float baseOffsetDistance = 8.0f;
        float baseSelfAttraction = 0.8f;
        float baseOtherAttraction = 1.4f;

        // apply intensity scaling
        species2.moveSpeed = scaleBehaviorValue(baseSpeed, species2.behaviorIntensity);
        species2.turnSpeed = scaleBehaviorValue(baseTurnSpeed, species2.behaviorIntensity);
        species2.sensorAngleSpacing = scaleBehaviorValue(baseAngleSpacing, species2.behaviorIntensity, 0.5f, 2.0f);
        species2.sensorOffsetDistance = scaleBehaviorValue(baseOffsetDistance, species2.behaviorIntensity);
        species2.attractionToSelf = scaleBehaviorValue(baseSelfAttraction, species2.behaviorIntensity, 0.4f, 1.5f);
        species2.attractionToOthers = scaleBehaviorValue(baseOtherAttraction, species2.behaviorIntensity, 0.7f, 2.2f);
        species2.repulsionFromOthers = 0.0f;
        
        // REPRODUCTION: Blue cooperative - rebirth, mates with anyone!
        species2.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::Rebirth;
        species2.rebirthEnabled = true;
        species2.lifespanSeconds = 60.0f;
        species2.splittingEnabled = false;
        species2.matingEnabled = true;
        species2.crossSpeciesMating = true;        // mates with ANY species!
        species2.hybridMutationRate = 0.1f;
        species2.matingRadius = 12.0f;
        
        settings.speciesSettings.push_back(species2);
    }

    if (numSpecies >= 3)
    {
        // green species: avoiding nomadic (runs from others, BUT attracts to own kind for mating)
        SimulationSettings::SpeciesSettings species3;
        species3.color = sf::Color(80, 255, 80);                                                   // bright green
        species3.behaviorIntensity = existingIntensities.size() >= 3 ? existingIntensities[2] : 2; // use existing or default

        // base values for default intensity (level 2)
        float baseSpeed = 2.5f;
        float baseTurnSpeed = 60.0f;
        float baseAngleSpacing = 45.0f;
        float baseOffsetDistance = 15.0f;
        float baseSelfAttraction = 1.2f;
        float baseOtherAttraction = -1.2f;

        // apply intensity scaling
        species3.moveSpeed = scaleBehaviorValue(baseSpeed, species3.behaviorIntensity);
        species3.turnSpeed = scaleBehaviorValue(baseTurnSpeed, species3.behaviorIntensity);
        species3.sensorAngleSpacing = scaleBehaviorValue(baseAngleSpacing, species3.behaviorIntensity, 0.8f, 2.2f);
        species3.sensorOffsetDistance = scaleBehaviorValue(baseOffsetDistance, species3.behaviorIntensity);
        species3.attractionToSelf = scaleBehaviorValue(baseSelfAttraction, species3.behaviorIntensity, 0.6f, 2.0f);
        species3.attractionToOthers = scaleBehaviorValue(baseOtherAttraction, species3.behaviorIntensity, -0.5f, -2.0f);
        species3.repulsionFromOthers = 0.0f;
        
        // KEY: Loners/nomads still need to find each other to mate!
        // They avoid OTHER species but are attracted to their own kind
        species3.sameSpeciesCohesionBoost = 2.5f;  // strongly attracted to own kind
        species3.separateFromSameSpecies = false;  // don't push away from own kind
        
        // REPRODUCTION: Green nomads reproduce via splitting (asexual)
        species3.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::HardDeath;
        species3.lifespanSeconds = 50.0f;
        species3.splittingEnabled = true;          // main reproduction method
        species3.splitEnergyThreshold = 0.5f;      // VERY LOW - loners split easily!
        species3.splitCooldownTime = 5.0f;         // longer cooldown to balance
        species3.offspringEnergy = 0.65f;          // Children born ABOVE split threshold!
        species3.matingEnabled = true;             // can mate if they find own kind
        species3.crossSpeciesMating = false;
        species3.matingRadius = 14.0f;
        species3.matingEnergyCost = 0.12f;         // Low mating cost for loners
        
        // Loners are energy-efficient (survive alone)
        species3.energyDecayPerStep = 0.0008f;     // VERY SLOW energy decay - nomads are efficient!
        species3.energyGainPerNeighbor = 0.003f;   // HIGH gain when with own kind (they cluster)
        
        settings.speciesSettings.push_back(species3);
    }

    if (numSpecies >= 4)
    {
        // yellow species: alien/chaotic but still bonds with own kind
        SimulationSettings::SpeciesSettings species4;
        species4.color = sf::Color(255, 255, 80);                                                  // bright yellow
        species4.behaviorIntensity = existingIntensities.size() >= 4 ? existingIntensities[3] : 2; // use existing or default

        // base values for default intensity (level 2) - alien/chaotic parameters
        float baseSpeed = 1.8f;         // moderate speed but will vary wildly
        float baseTurnSpeed = 90.0f;    // high turn speed for erratic movement
        float baseAngleSpacing = 60.0f; // wide sensor spacing for erratic sensing
        float baseOffsetDistance = 10.0f;
        float baseSelfAttraction = 0.5f;  // low self attraction - doesn't form strong trails
        float baseOtherAttraction = 0.0f; // neutral to others initially - will be dynamic

        // apply intensity scaling with alien characteristics
        species4.moveSpeed = scaleBehaviorValue(baseSpeed, species4.behaviorIntensity, 0.5f, 3.0f);        // can be very slow or very fast
        species4.turnSpeed = scaleBehaviorValue(baseTurnSpeed, species4.behaviorIntensity, 30.0f, 150.0f); // erratic turning
        species4.sensorAngleSpacing = scaleBehaviorValue(baseAngleSpacing, species4.behaviorIntensity, 20.0f, 120.0f);
        species4.sensorOffsetDistance = scaleBehaviorValue(baseOffsetDistance, species4.behaviorIntensity, 5.0f, 25.0f);
        species4.attractionToSelf = scaleBehaviorValue(baseSelfAttraction, species4.behaviorIntensity, 0.1f, 1.5f);
        species4.attractionToOthers = scaleBehaviorValue(baseOtherAttraction, species4.behaviorIntensity, -1.0f, 1.0f); // can attract or repel dynamically
        species4.repulsionFromOthers = 0.0f;                                                                            // will use dynamic behavior instead
        
        // aliens are chaotic but still find their own kind (hive mind)
        species4.sameSpeciesCohesionBoost = 2.0f;  // attracted to own kind
        species4.separateFromSameSpecies = false;  // don't push away from own kind
        
        // REPRODUCTION: Yellow aliens burst into spores on death!
        species4.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::SporeBurst;
        species4.lifespanSeconds = 30.0f;          // short chaotic lives
        species4.sporeCount = 4;                   // burst into 4 spores
        species4.sporeRadius = 15.0f;
        species4.sporeEnergy = 0.6f;
        species4.splittingEnabled = false;         // don't split, use spore burst
        species4.matingEnabled = false;            // too chaotic to mate traditionally
        
        settings.speciesSettings.push_back(species4);
    }

    if (numSpecies >= 5)
    {
        // magenta species
        SimulationSettings::SpeciesSettings species5;
        species5.color = sf::Color(255, 80, 255);                                                  // bright magenta
        species5.behaviorIntensity = existingIntensities.size() >= 5 ? existingIntensities[4] : 2; // use existing or default

        // base values for default intensity (level 2) - anti-alien/order parameters
        float baseSpeed = 1.5f;         // moderate, consistent speed
        float baseTurnSpeed = 30.0f;    // lower turn speed for smoother movement
        float baseAngleSpacing = 25.0f; // narrow sensor spacing for precise sensing
        float baseOffsetDistance = 10.0f;
        float baseSelfAttraction = 1.5f;  // strong self-attraction - forms organized structures
        float baseOtherAttraction = 0.5f; // mild attraction to others - seeks harmony

        // applies intensity scaling with anti-alien characteristics (opposite of alien ranges)
        species5.moveSpeed = scaleBehaviorValue(baseSpeed, species5.behaviorIntensity, 0.8f, 2.5f);       // controlled speed range
        species5.turnSpeed = scaleBehaviorValue(baseTurnSpeed, species5.behaviorIntensity, 15.0f, 60.0f); // gentle turning
        species5.sensorAngleSpacing = scaleBehaviorValue(baseAngleSpacing, species5.behaviorIntensity, 10.0f, 45.0f);
        species5.sensorOffsetDistance = scaleBehaviorValue(baseOffsetDistance, species5.behaviorIntensity, 8.0f, 15.0f);
        species5.attractionToSelf = scaleBehaviorValue(baseSelfAttraction, species5.behaviorIntensity, 0.8f, 2.5f);
        species5.attractionToOthers = scaleBehaviorValue(baseOtherAttraction, species5.behaviorIntensity, 0.2f, 1.2f); // harmonious interaction
        species5.repulsionFromOthers = 0.0f;                                                                           // no repulsion - seeks "harmony"
        
        // REPRODUCTION: Magenta order enforcers - rebirth to maintain structure
        species5.deathBehavior = SimulationSettings::SpeciesSettings::DeathBehavior::Rebirth;
        species5.rebirthEnabled = true;
        species5.lifespanSeconds = 50.0f;
        species5.splittingEnabled = false;
        species5.matingEnabled = true;
        species5.crossSpeciesMating = false;
        species5.matingRadius = 12.0f;
        
        settings.speciesSettings.push_back(species5);
    }

    std::cout << "Multi-species setup complete! " << numSpecies << " species with intensity-scaled behaviors:" << std::endl;
    std::cout << "  Red: Aggressive Territorial (intensity " << (numSpecies >= 1 ? settings.speciesSettings[0].behaviorIntensity : 0) << ")" << std::endl;
    if (numSpecies >= 2)
        std::cout << "  Blue: Cooperative Symbiotic (intensity " << settings.speciesSettings[1].behaviorIntensity << ")" << std::endl;
    if (numSpecies >= 3)
        std::cout << "  Green: Avoiding Nomadic (intensity " << settings.speciesSettings[2].behaviorIntensity << ")" << std::endl;
    if (numSpecies >= 4)
        std::cout << "  Yellow: Weird Alien (intensity " << settings.speciesSettings[3].behaviorIntensity << ")" << std::endl;
    if (numSpecies >= 5)
        std::cout << "  Magenta: Anti-Alien Order (intensity " << settings.speciesSettings[4].behaviorIntensity << ")" << std::endl;
    std::cout << "  [Tip] Use [I/O] for all species intensity, [H/J/K/,/.] for individual (1=mild, 2=default, 3=strong, 4=extreme)" << std::endl;
    std::cout << "  " << std::endl;
}

// updates only the intensities of existing species without rebuilding
void updateSpeciesIntensities(SimulationSettings &settings)
{
    if (currentSpeciesMode == SpeciesMode::Classic5)
    {
        // updates each active species with its new intensity while preserving the combination
        for (size_t i = 0; i < settings.speciesSettings.size() && i < activeSpeciesIndices.size(); ++i)
        {
            auto &species = settings.speciesSettings[i];
            int speciesIndex = activeSpeciesIndices[i];
            int intensity = species.behaviorIntensity;

            // apply the same logic as in rerollSpeciesCombination for each species type
            if (speciesIndex == 0) // red
            {
                species.moveSpeed = scaleBehaviorValue(2.0f, intensity);
                species.turnSpeed = scaleBehaviorValue(45.0f, intensity);
                species.sensorAngleSpacing = scaleBehaviorValue(30.0f, intensity);
                species.sensorOffsetDistance = scaleBehaviorValue(12.0f, intensity);
                species.attractionToSelf = scaleBehaviorValue(1.8f, intensity, 0.5f, 2.5f);
                species.attractionToOthers = scaleBehaviorValue(-0.8f, intensity, -0.3f, -1.5f);
            }
            else if (speciesIndex == 1) // blue
            {
                species.moveSpeed = scaleBehaviorValue(1.0f, intensity);
                species.turnSpeed = scaleBehaviorValue(20.0f, intensity);
                species.sensorAngleSpacing = scaleBehaviorValue(15.0f, intensity, 0.5f, 2.0f);
                species.sensorOffsetDistance = scaleBehaviorValue(8.0f, intensity);
                species.attractionToSelf = scaleBehaviorValue(0.8f, intensity, 0.4f, 1.5f);
                species.attractionToOthers = scaleBehaviorValue(1.4f, intensity, 0.7f, 2.2f);
            }
            else if (speciesIndex == 2) // green
            {
                species.moveSpeed = scaleBehaviorValue(2.5f, intensity);
                species.turnSpeed = scaleBehaviorValue(60.0f, intensity);
                species.sensorAngleSpacing = scaleBehaviorValue(45.0f, intensity, 0.8f, 2.2f);
                species.sensorOffsetDistance = scaleBehaviorValue(15.0f, intensity);
                species.attractionToSelf = scaleBehaviorValue(1.2f, intensity, 0.6f, 2.0f);
                species.attractionToOthers = scaleBehaviorValue(-1.2f, intensity, -0.5f, -2.0f);
            }
            else if (speciesIndex == 3) // yellow
            {
                species.moveSpeed = scaleBehaviorValue(1.8f, intensity, 0.5f, 3.0f);
                species.turnSpeed = scaleBehaviorValue(90.0f, intensity, 30.0f, 150.0f);
                species.sensorAngleSpacing = scaleBehaviorValue(60.0f, intensity, 20.0f, 120.0f);
                species.sensorOffsetDistance = scaleBehaviorValue(10.0f, intensity, 5.0f, 25.0f);
                species.attractionToSelf = scaleBehaviorValue(0.5f, intensity, 0.1f, 1.5f);
                species.attractionToOthers = scaleBehaviorValue(0.0f, intensity, -1.0f, 1.0f);
            }
            else if (speciesIndex == 4) // magenta
            {
                species.moveSpeed = scaleBehaviorValue(1.5f, intensity, 0.8f, 2.5f);
                species.turnSpeed = scaleBehaviorValue(30.0f, intensity, 15.0f, 60.0f);
                species.sensorAngleSpacing = scaleBehaviorValue(25.0f, intensity, 10.0f, 45.0f);
                species.sensorOffsetDistance = scaleBehaviorValue(10.0f, intensity, 8.0f, 15.0f);
                species.attractionToSelf = scaleBehaviorValue(1.5f, intensity, 0.8f, 2.5f);
                species.attractionToOthers = scaleBehaviorValue(0.5f, intensity, 0.2f, 1.2f);
            }
        }
    }
    else
    {
        // for other modes, rebuild completely as before
        setupSpecies(settings);
    }
}

// function to reroll species combination in classic5 mode
void rerollSpeciesCombination(SimulationSettings &settings)
{
    if (currentSpeciesMode != SpeciesMode::Classic5)
    {
        std::cout << "Species reroll only works in Classic 5 mode!" << std::endl;
        return;
    }

    static std::random_device rd;
    static std::mt19937 gen(rd());

    // randomly pick how many species (1-5)
    std::uniform_int_distribution<int> countDist(1, 5);
    int randomSpeciesCount = countDist(gen);

    // create a shuffled list of all 5 species indices
    std::vector<int> speciesIndices = {0, 1, 2, 3, 4};
    std::shuffle(speciesIndices.begin(), speciesIndices.end(), gen);

    // store the selected species indices
    activeSpeciesIndices.clear();
    for (int i = 0; i < randomSpeciesCount; ++i)
    {
        activeSpeciesIndices.push_back(speciesIndices[i]);
    }

    // store existing intensities for the species we'll keep
    std::vector<int> existingIntensities;
    for (const auto &species : settings.speciesSettings)
    {
        existingIntensities.push_back(species.behaviorIntensity);
    }

    // clear current species
    settings.speciesSettings.clear();

    std::vector<std::string> speciesNames = {"Red", "Blue", "Green", "Yellow", "Magenta"};
    std::vector<std::string> speciesLabels = {"Red", "Blue", "Green", "Yellow", "Magenta"};

    std::cout << "Species reroll! Random combo (" << randomSpeciesCount << " species): ";

    // adds the randomly selected species using the same logic as setupclassic5species
    for (int i = 0; i < randomSpeciesCount; ++i)
    {
        int selectedIndex = activeSpeciesIndices[i];
        SimulationSettings::SpeciesSettings newSpecies;

        // use existing intensity if available, otherwise default to 2
        int intensity = (i < (int)existingIntensities.size()) ? existingIntensities[i] : 2;

        if (selectedIndex == 0) // red
        {
            newSpecies.color = sf::Color(255, 80, 80);
            newSpecies.behaviorIntensity = intensity;
            newSpecies.moveSpeed = scaleBehaviorValue(2.0f, intensity);
            newSpecies.turnSpeed = scaleBehaviorValue(45.0f, intensity);
            newSpecies.sensorAngleSpacing = scaleBehaviorValue(30.0f, intensity);
            newSpecies.sensorOffsetDistance = scaleBehaviorValue(12.0f, intensity);
            newSpecies.attractionToSelf = scaleBehaviorValue(1.8f, intensity, 0.5f, 2.5f);
            newSpecies.attractionToOthers = scaleBehaviorValue(-0.8f, intensity, -0.3f, -1.5f);
            std::cout << "Red(255,80,80) ";
        }
        else if (selectedIndex == 1) // blue
        {
            newSpecies.color = sf::Color(80, 150, 255);
            newSpecies.behaviorIntensity = intensity;
            newSpecies.moveSpeed = scaleBehaviorValue(1.0f, intensity);
            newSpecies.turnSpeed = scaleBehaviorValue(20.0f, intensity);
            newSpecies.sensorAngleSpacing = scaleBehaviorValue(15.0f, intensity, 0.5f, 2.0f);
            newSpecies.sensorOffsetDistance = scaleBehaviorValue(8.0f, intensity);
            newSpecies.attractionToSelf = scaleBehaviorValue(0.8f, intensity, 0.4f, 1.5f);
            newSpecies.attractionToOthers = scaleBehaviorValue(1.4f, intensity, 0.7f, 2.2f);
            std::cout << "Blue(80,150,255) ";
        }
        else if (selectedIndex == 2) // green
        {
            newSpecies.color = sf::Color(80, 255, 80);
            newSpecies.behaviorIntensity = intensity;
            newSpecies.moveSpeed = scaleBehaviorValue(2.5f, intensity);
            newSpecies.turnSpeed = scaleBehaviorValue(60.0f, intensity);
            newSpecies.sensorAngleSpacing = scaleBehaviorValue(45.0f, intensity, 0.8f, 2.2f);
            newSpecies.sensorOffsetDistance = scaleBehaviorValue(15.0f, intensity);
            newSpecies.attractionToSelf = scaleBehaviorValue(1.2f, intensity, 0.6f, 2.0f);
            newSpecies.attractionToOthers = scaleBehaviorValue(-1.2f, intensity, -0.5f, -2.0f);
            std::cout << "Green(80,255,80) ";
        }
        else if (selectedIndex == 3) // yellow
        {
            newSpecies.color = sf::Color(255, 255, 80);
            newSpecies.behaviorIntensity = intensity;
            newSpecies.moveSpeed = scaleBehaviorValue(1.8f, intensity, 0.5f, 3.0f);
            newSpecies.turnSpeed = scaleBehaviorValue(90.0f, intensity, 30.0f, 150.0f);
            newSpecies.sensorAngleSpacing = scaleBehaviorValue(60.0f, intensity, 20.0f, 120.0f);
            newSpecies.sensorOffsetDistance = scaleBehaviorValue(10.0f, intensity, 5.0f, 25.0f);
            newSpecies.attractionToSelf = scaleBehaviorValue(0.5f, intensity, 0.1f, 1.5f);
            newSpecies.attractionToOthers = scaleBehaviorValue(0.0f, intensity, -1.0f, 1.0f);
            std::cout << "Yellow(255,255,80) ";
        }
        else if (selectedIndex == 4) // magenta
        {
            newSpecies.color = sf::Color(255, 80, 255);
            newSpecies.behaviorIntensity = intensity;
            newSpecies.moveSpeed = scaleBehaviorValue(1.5f, intensity, 0.8f, 2.5f);
            newSpecies.turnSpeed = scaleBehaviorValue(30.0f, intensity, 15.0f, 60.0f);
            newSpecies.sensorAngleSpacing = scaleBehaviorValue(25.0f, intensity, 10.0f, 45.0f);
            newSpecies.sensorOffsetDistance = scaleBehaviorValue(10.0f, intensity, 8.0f, 15.0f);
            newSpecies.attractionToSelf = scaleBehaviorValue(1.5f, intensity, 0.8f, 2.5f);
            newSpecies.attractionToOthers = scaleBehaviorValue(0.5f, intensity, 0.2f, 1.2f);
            std::cout << "Magenta(255,80,255) ";
        }

        newSpecies.repulsionFromOthers = 0.0f;
        settings.speciesSettings.push_back(newSpecies);
    }
    std::cout << std::endl;
    std::cout << "Active species indices: [";
    for (size_t i = 0; i < activeSpeciesIndices.size(); ++i)
    {
        std::cout << activeSpeciesIndices[i];
        if (i < activeSpeciesIndices.size() - 1)
            std::cout << ",";
    }
    std::cout << "]" << std::endl;
    std::cout << "Intensity controls: H=" << (activeSpeciesIndices.empty() ? "none" : activeSpeciesIndices.size() >= 1 ? "position0"
                                                                                                                       : "none")
              << " J=" << (activeSpeciesIndices.size() >= 2 ? "position1" : "none")
              << " K=" << (activeSpeciesIndices.size() >= 3 ? "position2" : "none")
              << " ,=" << (activeSpeciesIndices.size() >= 4 ? "position3" : "none")
              << " .=" << (activeSpeciesIndices.size() >= 5 ? "position4" : "none") << std::endl;
    std::cout << "Press G again to reroll for a completely different combination!" << std::endl;
}

// function to toggle a specific species on/off
void toggleSpecies(int speciesIndex, PhysarumSimulation &simulation, SimulationSettings &settings)
{
    if (currentSpeciesMode != SpeciesMode::Classic5)
        return;

    std::vector<std::string> speciesNames = {
        "Red", "Blue", "Green",
        "Yellow", "Magenta",
        "Black/Green", "Crimson", "White"};
    std::vector<std::string> speciesLabels = {
        "Red", "Blue", "Green", "Yellow", "Magenta", "Black", "Crimson", "White"};

    // check if species is currently active
    auto it = std::find(activeSpeciesIndices.begin(), activeSpeciesIndices.end(), speciesIndex);
    bool isActive = (it != activeSpeciesIndices.end());

    if (isActive)
    {
        // remove species up to one left
        if (activeSpeciesIndices.size() > 1)
        {
            activeSpeciesIndices.erase(it);

            if (speciesIndex >= 5)
            {
                std::cout << "DISABLED " << speciesLabels[speciesIndex] << " " << speciesNames[speciesIndex] << " species!" << std::endl;
            }
            else
            {
                std::cout << "DISABLED " << speciesLabels[speciesIndex] << " " << speciesNames[speciesIndex] << " species!" << std::endl;
            }
        }
        else
        {
            std::cout << "Cannot disable " << speciesLabels[speciesIndex] << " " << speciesNames[speciesIndex] << " - must have at least 1 species active!" << std::endl;
            return;
        }
    }
    else
    {
        // add species
        activeSpeciesIndices.push_back(speciesIndex);

        if (speciesIndex >= 5)
        {
            std::cout << "ENABLED " << speciesLabels[speciesIndex] << " " << speciesNames[speciesIndex] << " species!" << std::endl;
        }
        else
        {
            std::cout << "ENABLED " << speciesLabels[speciesIndex] << " " << speciesNames[speciesIndex] << " species!" << std::endl;
        }
    }

    // sorts the indices for consistent ordering
    std::sort(activeSpeciesIndices.begin(), activeSpeciesIndices.end());

    // display current active species
    std::cout << "Active species [" << activeSpeciesIndices.size() << "/8]: ";
    for (size_t i = 0; i < activeSpeciesIndices.size(); ++i)
    {
        int idx = activeSpeciesIndices[i];
        std::cout << speciesLabels[idx];
        if (i < activeSpeciesIndices.size() - 1)
            std::cout << " ";
    }
    std::cout << std::endl;
}

// function to get species mode description for hud display
std::string getSpeciesModeString(SpeciesMode mode, int randomCount)
{
    switch (mode)
    {
    case SpeciesMode::Single:
        return "Single Species";
    case SpeciesMode::Classic5:
        return "Classic 5+ (" + std::to_string(activeSpeciesIndices.size()) + "/8 active)";
    case SpeciesMode::Random:
        return "Random Species (" + std::to_string(randomCount) + ")";
    case SpeciesMode::AlgorithmBenchmark:
        return "Algorithm Benchmark (7 algos)";
    default:
        return "Unknown Mode";
    }
}

// function to setup species based on current mode
void setupSpecies(SimulationSettings &settings)
{
    switch (currentSpeciesMode)
    {
    case SpeciesMode::Single:
        setupSingleSpecies(settings);
        break;
    case SpeciesMode::Classic5:
        setupClassic5Species(settings);
        break;
    case SpeciesMode::Random:
        setupRandomSpecies(settings, randomSpeciesCount);
        break;
    case SpeciesMode::AlgorithmBenchmark:
        // benchmark mode handles its own setup via PhysarumSimulation::enterBenchmarkMode()
        break;
    }
}

// function to add a random classic 5 species
void addRandomClassic5Species(PhysarumSimulation &simulation, SimulationSettings &settings)
{
    if (currentSpeciesMode != SpeciesMode::Classic5)
        return;

    // get available species (those not currently active)
    std::vector<int> availableSpecies;
    for (int i = 0; i < 8; ++i)
    {
        if (std::find(activeSpeciesIndices.begin(), activeSpeciesIndices.end(), i) == activeSpeciesIndices.end())
        {
            availableSpecies.push_back(i);
        }
    }

    if (availableSpecies.empty())
    {
        std::cout << "All 8 species are already active!" << std::endl;
        return;
    }

    // randomly selects one of the available species
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, availableSpecies.size() - 1);
    int selectedSpecies = availableSpecies[dis(gen)];

    // add the selected species
    activeSpeciesIndices.push_back(selectedSpecies);
    std::sort(activeSpeciesIndices.begin(), activeSpeciesIndices.end());

    std::vector<std::string> speciesNames = {
        "Red (Strategic Warlord)", "Blue (Harmony Builder)", "Green (Nomadic Wanderer)",
        "Yellow (Quantum Alien)", "Magenta (Order Enforcer)",
        "Black/Green (Parasitic Nightmare)", "Crimson (Death Bringer)", "White (Guardian Angel)"};
    std::vector<std::string> speciesLabels = {
        "Red", "Blue", "Green", "Yellow", "Magenta", "Black", "Crimson", "White"};

    if (selectedSpecies >= 5)
    {
        std::cout << "ADDED " << speciesLabels[selectedSpecies] << " " << speciesNames[selectedSpecies] << " species!" << std::endl;
    }
    else
    {
        std::cout << "ADDED " << speciesLabels[selectedSpecies] << " " << speciesNames[selectedSpecies] << " species!" << std::endl;
    }

    std::cout << "Active species [" << activeSpeciesIndices.size() << "/8]: ";
    for (size_t i = 0; i < activeSpeciesIndices.size(); ++i)
    {
        int idx = activeSpeciesIndices[i];
        std::cout << speciesLabels[idx];
        if (i < activeSpeciesIndices.size() - 1)
            std::cout << " ";
    }
    std::cout << std::endl;
}

// function to remove a random classic 5 species
void removeRandomClassic5Species(PhysarumSimulation &simulation, SimulationSettings &settings)
{
    if (currentSpeciesMode != SpeciesMode::Classic5)
        return;

    if (activeSpeciesIndices.size() <= 1)
    {
        std::cout << "Cannot remove species - must have at least 1 species active!" << std::endl;
        return;
    }

    // randomly select one active species to remove
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, activeSpeciesIndices.size() - 1);
    int indexToRemove = dis(gen);
    int speciesIndex = activeSpeciesIndices[indexToRemove];

    // remove the selected species
    activeSpeciesIndices.erase(activeSpeciesIndices.begin() + indexToRemove);

    std::vector<std::string> speciesNames = {
        "Red (Strategic Warlord)", "Blue (Harmony Builder)", "Green (Nomadic Wanderer)",
        "Yellow (Quantum Alien)", "Magenta (Order Enforcer)",
        "Black/Green (Parasitic Nightmare)", "Crimson (Death Bringer)", "White (Guardian Angel)"};
    std::vector<std::string> speciesLabels = {
        "Red", "Blue", "Green", "Yellow", "Magenta", "Black", "Crimson", "White"};

    if (speciesIndex >= 5)
    {
        std::cout << "REMOVED " << speciesLabels[speciesIndex] << " " << speciesNames[speciesIndex] << " species!" << std::endl;
    }
    else
    {
        std::cout << "REMOVED " << speciesLabels[speciesIndex] << " " << speciesNames[speciesIndex] << " species!" << std::endl;
    }

    std::cout << "Active species [" << activeSpeciesIndices.size() << "/8]: ";
    for (size_t i = 0; i < activeSpeciesIndices.size(); ++i)
    {
        int idx = activeSpeciesIndices[i];
        std::cout << speciesLabels[idx];
        if (i < activeSpeciesIndices.size() - 1)
            std::cout << " ";
    }
    std::cout << std::endl;
}

int main()
{
    sf::RenderWindow window(sf::VideoMode({WINDOW_WIDTH, WINDOW_HEIGHT}), "Physarum Simulation");
    window.setFramerateLimit(60);

    // initialize font
    sf::Font font;
    if (!font.openFromFile("DejaVuSans.ttf") &&
        !font.openFromFile("bin/DejaVuSans.ttf") &&
        !font.openFromFile("source/DejaVuSans.ttf"))
    {
        std::cout << "Warning: Could not load font file, HUD text may not display properly" << std::endl;
    }

    SimulationSettings settings;
    settings.width = WINDOW_WIDTH;
    settings.height = WINDOW_HEIGHT;
    settings.numAgents = 10;                                          // good density for network formation
    settings.spawnMode = SimulationSettings::SpawnMode::InwardCircle; // creates better initial clustering

    // optimized trail parameters for cellular/network patterns
    settings.diffuseRate = 0.033f;      // moderate diffusion for smooth edges
    settings.decayRate = 0.031f;        // slower decay for persistent trails
    settings.trailWeight = 11.974f;     // strong trail deposition for defined pathways
    settings.displayThreshold = 0.010f; // show fine detail
    settings.blurEnabled = false;       // sharp edges for cellular definition

    // set custom slime color
    // examples:
    // sf::color(0, 255, 255)     // cyan
    // sf::color(255, 0, 255)     // magenta
    // sf::color(0, 255, 0)       // green
    // sf::color(255, 100, 0)     // orange
    // sf::color(100, 100, 255)   // light blue

    setupSpecies(settings);

    PhysarumSimulation simulation(settings);

    // initialize mouse settings for hud display
    MouseSettings mouseSettings;

    // performance timing
    sf::Clock clock;

    while (window.isOpen())
    {
        sf::Time deltaTime = clock.restart();
        // handle events
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }

            if (const auto *keyPressed = event->getIf<sf::Event::KeyPressed>())
            {
                auto &species = settings.speciesSettings.empty() ? settings.speciesSettings.emplace_back() : settings.speciesSettings[0];

                // trail settings (only when shift is not pressed)
                if (keyPressed->code == sf::Keyboard::Key::Num1 &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) && !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                    settings.diffuseRate = std::min(settings.diffuseRate + 0.01f, 1.0f);
                if (keyPressed->code == sf::Keyboard::Key::Num2 &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) && !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                    settings.diffuseRate = std::max(settings.diffuseRate - 0.01f, 0.0f);
                if (keyPressed->code == sf::Keyboard::Key::Num3 &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) && !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                    settings.decayRate = std::min(settings.decayRate + 0.001f, 1.0f);
                if (keyPressed->code == sf::Keyboard::Key::Num4 &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) && !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                    settings.decayRate = std::max(settings.decayRate - 0.001f, 0.0f);
                if (keyPressed->code == sf::Keyboard::Key::Num5)
                    settings.trailWeight = std::min(settings.trailWeight + 0.5f, 100.0f);
                if (keyPressed->code == sf::Keyboard::Key::Num6)
                    settings.trailWeight = std::max(settings.trailWeight - 0.5f, 0.0f);

                // movement settings
                if (keyPressed->code == sf::Keyboard::Key::E)
                    species.moveSpeed = std::min(species.moveSpeed + 0.1f, 50.0f);
                if (keyPressed->code == sf::Keyboard::Key::R)
                    species.moveSpeed = std::max(species.moveSpeed - 0.1f, 0.1f);
                if (keyPressed->code == sf::Keyboard::Key::Q)
                    species.turnSpeed = std::min(species.turnSpeed + 1.0f, 180.0f);
                if (keyPressed->code == sf::Keyboard::Key::W)
                    species.turnSpeed = std::max(species.turnSpeed - 1.0f, 0.0f);

                // sensor settings
                if (keyPressed->code == sf::Keyboard::Key::Num7)
                    species.sensorAngleSpacing = std::min(species.sensorAngleSpacing + 1.0f, 180.0f);
                if (keyPressed->code == sf::Keyboard::Key::Num8)
                    species.sensorAngleSpacing = std::max(species.sensorAngleSpacing - 1.0f, 0.0f);
                if (keyPressed->code == sf::Keyboard::Key::Num9)
                    species.sensorOffsetDistance = std::min(species.sensorOffsetDistance + 1.0f, 100.0f);
                if (keyPressed->code == sf::Keyboard::Key::Num0)
                    species.sensorOffsetDistance = std::max(species.sensorOffsetDistance - 1.0f, 1.0f);

                // display settings
                if (keyPressed->code == sf::Keyboard::Key::T)
                    settings.displayThreshold = std::min(settings.displayThreshold + 0.01f, 1.0f);
                if (keyPressed->code == sf::Keyboard::Key::Y)
                    settings.displayThreshold = std::max(settings.displayThreshold - 0.01f, 0.0f);
                if (keyPressed->code == sf::Keyboard::Key::B)
                    settings.blurEnabled = !settings.blurEnabled;

                if (keyPressed->code == sf::Keyboard::Key::D)
                {
                    bool shiftDown = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) ||
                                     sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);
                    if (simulation.isInBenchmarkMode() && !shiftDown) {
                        auto& bm = simulation.getBenchmarkManager();
                        bm.runDoublingExperiment();
                        std::cout << "Empirical doubling refreshed (A*/Greedy/JPS/Theta)" << std::endl;
                    } else {
                        simulation.toggleAgentOverlay();
                        std::cout << "Agent overlay: " << (simulation.isAgentOverlayEnabled() ? "ON" : "OFF") << std::endl;
                    }
                }

                // cluster labels control (shift+semicolon - plain semicolon is for damping)
                if (keyPressed->code == sf::Keyboard::Key::Semicolon &&
                    (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)))
                {
                    simulation.toggleClusterLabels();
                    std::cout << "Cluster labels: " << (simulation.areClusterLabelsEnabled() ? "ON" : "OFF") << std::endl;
                }

                // simulation control
                if (keyPressed->code == sf::Keyboard::Key::Space)
                {
                    if (simulation.isInBenchmarkMode()) {
                        // in benchmark mode: start benchmark or reset
                        if (!simulation.getBenchmarkManager().isBenchmarkActive()) {
                            simulation.startBenchmark();
                            std::cout << "Benchmark started!" << std::endl;
                        } else {
                            simulation.resetBenchmark();
                            std::cout << "Benchmark reset!" << std::endl;
                        }
                    } else {
                        // normal mode: reset simulation
                        simulation.updateSettings(settings);
                        simulation.reset();
                    }
                }

                // pause key (P) - for benchmark mode (not really working yet)
                if (keyPressed->code == sf::Keyboard::Key::P)
                {
                    bool shiftDown = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) ||
                                     sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);
                    if (simulation.isInBenchmarkMode()) {
                        if (shiftDown && simulation.getBenchmarkManager().isBenchmarkActive()) {
                            simulation.pauseBenchmark();
                        } else {
                            simulation.toggleBenchmarkPackPositions();
                        }
                    }
                }

                // benchmark algorithm toggle (Shift+1-7). toggling individual algorithm groups
                if (simulation.isInBenchmarkMode()) {
                    bool shiftDown = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) ||
                                     sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);
                    if (shiftDown) {
                        if (keyPressed->code == sf::Keyboard::Key::Num1) {
                            simulation.toggleBenchmarkAlgorithm(0);
                        } else if (keyPressed->code == sf::Keyboard::Key::Num2) {
                            simulation.toggleBenchmarkAlgorithm(1);
                        } else if (keyPressed->code == sf::Keyboard::Key::Num3) {
                            simulation.toggleBenchmarkAlgorithm(2);
                        } else if (keyPressed->code == sf::Keyboard::Key::Num4) {
                            simulation.toggleBenchmarkAlgorithm(3);
                        } else if (keyPressed->code == sf::Keyboard::Key::Num5) {
                            simulation.toggleBenchmarkAlgorithm(4);
                        } else if (keyPressed->code == sf::Keyboard::Key::Num6) {
                            simulation.toggleBenchmarkAlgorithm(5);
                        } else if (keyPressed->code == sf::Keyboard::Key::Num7) {
                            simulation.toggleBenchmarkAlgorithm(6);
                        }
                    }
                }

                // benchmark maze controls (only in benchmark mode)
                if (simulation.isInBenchmarkMode() && !simulation.getBenchmarkManager().isBenchmarkActive()) {
                    // N key: cycle maze type (next maze)
                    if (keyPressed->code == sf::Keyboard::Key::N) {
                        auto& bm = simulation.getBenchmarkManager();
                        bm.cycleMazeType();
                        bm.regenerateMaze();
                        simulation.resetBenchmark();
                        std::cout << "Maze type: " << BenchmarkManager::getMazeTypeName(bm.getMazeType()) << std::endl;
                    }
                    // R key: Regenerate maze (same type, new layout)
                    if (keyPressed->code == sf::Keyboard::Key::R) {
                        auto& bm = simulation.getBenchmarkManager();
                        bm.regenerateMaze();
                        simulation.resetBenchmark();
                        std::cout << "Maze regenerated!" << std::endl;
                    }
                    // +/= key: Increase difficulty
                    if (keyPressed->code == sf::Keyboard::Key::Equal) {
                        auto& bm = simulation.getBenchmarkManager();
                        bm.setMazeDifficulty(bm.getMazeDifficulty() + 0.1f);
                        bm.regenerateMaze();
                        simulation.resetBenchmark();
                        std::cout << "Difficulty: " << static_cast<int>(bm.getMazeDifficulty() * 100) << "%" << std::endl;
                    }
                    // - key: decrease difficulty
                    if (keyPressed->code == sf::Keyboard::Key::Hyphen) {
                        auto& bm = simulation.getBenchmarkManager();
                        bm.setMazeDifficulty(bm.getMazeDifficulty() - 0.1f);
                        bm.regenerateMaze();
                        simulation.resetBenchmark();
                        std::cout << "Difficulty: " << static_cast<int>(bm.getMazeDifficulty() * 100) << "%" << std::endl;
                    }
                }

                // randomize settings: only on plain z (no shift/ctrl/alt/cmd)
                if (keyPressed->code == sf::Keyboard::Key::Z &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LSystem) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RSystem))
                {
                    randomizeSettings(settings);
                    simulation.updateSettings(settings);
                    std::cout << "Press Space to reset with new random settings!" << std::endl;
                }

                // color changing shortcuts (only when shift is not pressed)
                if (keyPressed->code == sf::Keyboard::Key::C &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                {
                    // cycle through different colors
                    static int colorIndex = 0;
                    sf::Color colors[] = {
                        sf::Color(255, 230, 0),   // yellow (original)
                        sf::Color(0, 255, 255),   // cyan
                        sf::Color(255, 0, 255),   // magenta
                        sf::Color(0, 255, 0),     // green
                        sf::Color(255, 100, 0),   // orange
                        sf::Color(100, 100, 255), // light blue
                        sf::Color(255, 50, 50),   // red
                        sf::Color(200, 0, 200),   // purple
                        sf::Color(255, 255, 255), // white
                        sf::Color(255, 0, 0),     // bright red
                    };

                    if (settings.speciesSettings.empty())
                    {
                        settings.speciesSettings.emplace_back();
                    }

                    colorIndex = (colorIndex + 1) % 9;
                    settings.speciesSettings[0].color = colors[colorIndex];
                    simulation.updateSettings(settings);

                    std::cout << "Changed slime color! Press C again for next color." << std::endl;
                }

                // shift+c: toggle cross-species mating for all species
                if (keyPressed->code == sf::Keyboard::Key::C &&
                    (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)))
                {
                    if (!settings.speciesSettings.empty())
                    {
                        bool next = !settings.speciesSettings.front().crossSpeciesMating;
                        for (auto &sp : settings.speciesSettings)
                            sp.crossSpeciesMating = next;
                        std::cout << "Cross-species mating: " << (next ? "ON" : "OFF") << std::endl;
                    }
                }

                // shift+x: toggle mating for all species (avoid conflict with x transparency)
                if (keyPressed->code == sf::Keyboard::Key::X &&
                    (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)))
                {
                    if (!settings.speciesSettings.empty())
                    {
                        bool next = !settings.speciesSettings.front().matingEnabled;
                        for (auto &sp : settings.speciesSettings)
                            sp.matingEnabled = next;
                        std::cout << "Mating: " << (next ? "ON" : "OFF") << std::endl;
                    }
                }

                // shift+z: toggle asexual splitting for all species
                if (keyPressed->code == sf::Keyboard::Key::Z &&
                    (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)))
                {
                    if (!settings.speciesSettings.empty())
                    {
                        bool next = !settings.speciesSettings.front().splittingEnabled;
                        for (auto &sp : settings.speciesSettings)
                            sp.splittingEnabled = next;
                        simulation.updateSettings(settings);
                        std::cout << "Splitting: " << (next ? "ON" : "OFF") << std::endl;
                    }
                }

                // [ and ]: adjust mating radius (mirrors the first species across all)
                if (keyPressed->code == sf::Keyboard::Key::LBracket && !settings.speciesSettings.empty())
                {
                    float v = std::max(2.0f, settings.speciesSettings.front().matingRadius - 1.0f);
                    for (auto &sp : settings.speciesSettings)
                        sp.matingRadius = v;
                }
                if (keyPressed->code == sf::Keyboard::Key::RBracket && !settings.speciesSettings.empty())
                {
                    float v = std::min(200.0f, settings.speciesSettings.front().matingRadius + 1.0f);
                    for (auto &sp : settings.speciesSettings)
                        sp.matingRadius = v;
                }

                // - and =: adjust hybrid mutation rate
                if (keyPressed->code == sf::Keyboard::Key::Hyphen && !settings.speciesSettings.empty())
                {
                    float v = std::max(0.0f, settings.speciesSettings.front().hybridMutationRate - 0.01f);
                    for (auto &sp : settings.speciesSettings)
                        sp.hybridMutationRate = v;
                }
                if (keyPressed->code == sf::Keyboard::Key::Equal && !settings.speciesSettings.empty())
                {
                    float v = std::min(0.8f, settings.speciesSettings.front().hybridMutationRate + 0.01f);
                    for (auto &sp : settings.speciesSettings)
                        sp.hybridMutationRate = v;
                }

                // species mode toggle
                if (keyPressed->code == sf::Keyboard::Key::M)
                {
                    // cycle through the four modes (including algorithm benchmark)
                    switch (currentSpeciesMode)
                    {
                    case SpeciesMode::Single:
                        currentSpeciesMode = SpeciesMode::Classic5;
                        simulation.exitBenchmarkMode();  // make sure we're not in benchmark mode
                        setupSpecies(settings);
                        simulation.updateSettings(settings);
                        break;
                    case SpeciesMode::Classic5:
                        currentSpeciesMode = SpeciesMode::Random;
                        simulation.exitBenchmarkMode();
                        setupSpecies(settings);
                        simulation.updateSettings(settings);
                        break;
                    case SpeciesMode::Random:
                        currentSpeciesMode = SpeciesMode::AlgorithmBenchmark;
                        simulation.enterBenchmarkMode();
                        break;
                    case SpeciesMode::AlgorithmBenchmark:
                        currentSpeciesMode = SpeciesMode::Single;
                        simulation.exitBenchmarkMode();
                        setupSpecies(settings);
                        simulation.updateSettings(settings);
                        break;
                    }

                    std::cout << " Switched to " << getSpeciesModeString(currentSpeciesMode, randomSpeciesCount) << " mode!" << std::endl;
                }

                // behavior intensity controls for multi species
                if (keyPressed->code == sf::Keyboard::Key::I && !settings.speciesSettings.empty())
                {
                    // shift+i: increase global motion inertia (smoothing)
                    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                    {
                        settings.motionInertia = std::min(settings.motionInertia + 0.05f, 0.95f);
                        simulation.updateSettings(settings);
                        std::cout << "Motion inertia increased to " << settings.motionInertia << std::endl;
                        continue;
                    }
                    // increase intensity for all species (1-4)
                    for (auto &sp : settings.speciesSettings)
                    {
                        sp.behaviorIntensity = std::min(sp.behaviorIntensity + 1, 4);
                    }
                    // update intensities without rebuilding species combination
                    updateSpeciesIntensities(settings);
                    simulation.updateSettings(settings);
                    std::cout << "Increased behavior intensity to " << settings.speciesSettings[0].behaviorIntensity << " (1=mild, 4=extreme)" << std::endl;
                }

                if (keyPressed->code == sf::Keyboard::Key::O && !settings.speciesSettings.empty())
                {
                    // shift+o: decrease global motion inertia (smoothing)
                    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                    {
                        settings.motionInertia = std::max(settings.motionInertia - 0.05f, 0.0f);
                        simulation.updateSettings(settings);
                        std::cout << "Motion inertia decreased to " << settings.motionInertia << std::endl;
                        continue;
                    }
                    // decrease intensity for all species (1-4)
                    for (auto &sp : settings.speciesSettings)
                    {
                        sp.behaviorIntensity = std::max(sp.behaviorIntensity - 1, 1);
                    }
                    // update intensities without rebuilding species combination
                    updateSpeciesIntensities(settings);
                    simulation.updateSettings(settings);
                    std::cout << " Decreased behavior intensity to " << settings.speciesSettings[0].behaviorIntensity << " (1=mild, 4=extreme)" << std::endl;
                }

                // individual species intensity controls
                if (keyPressed->code == sf::Keyboard::Key::H && currentSpeciesMode != SpeciesMode::Random)
                {
                    if (currentSpeciesMode == SpeciesMode::Classic5)
                    {
                        // in classic5 mode tries to adjust first visible species (position 0)
                        if (!settings.speciesSettings.empty())
                        {
                            int position = 0; // always adjust first species in the current list
                            int originalSpeciesIndex = activeSpeciesIndices.empty() ? 0 : activeSpeciesIndices[position];
                            settings.speciesSettings[position].behaviorIntensity = (settings.speciesSettings[position].behaviorIntensity % 4) + 1;
                            updateSpeciesIntensities(settings);
                            simulation.updateSettings(settings);

                            // show which species we actually adjusted
                            std::string speciesName = (originalSpeciesIndex == 0) ? "Red" : (originalSpeciesIndex == 1) ? "Blue"
                                                                                        : (originalSpeciesIndex == 2)   ? "Green"
                                                                                        : (originalSpeciesIndex == 3)   ? "Yellow"
                                                                                                                        : "Magenta";
                            std::cout << "H key adjusted " << speciesName << " species (position 0) intensity: " << settings.speciesSettings[position].behaviorIntensity << std::endl;
                        }
                        else
                        {
                            std::cout << " No species available to adjust!" << std::endl;
                        }
                    }
                    else
                    {
                        // single species mode - adjust the only species
                        settings.speciesSettings[0].behaviorIntensity = (settings.speciesSettings[0].behaviorIntensity % 4) + 1;
                        updateSpeciesIntensities(settings);
                        simulation.updateSettings(settings);
                        std::cout << "Species intensity: " << settings.speciesSettings[0].behaviorIntensity << std::endl;
                    }
                }

                if (keyPressed->code == sf::Keyboard::Key::J && currentSpeciesMode == SpeciesMode::Classic5)
                {
                    // adjust second visible species (position 1) if it exists
                    if (settings.speciesSettings.size() >= 2)
                    {
                        int position = 1; // always adjust second species in the current list
                        int originalSpeciesIndex = activeSpeciesIndices.size() >= 2 ? activeSpeciesIndices[position] : 1;
                        settings.speciesSettings[position].behaviorIntensity = (settings.speciesSettings[position].behaviorIntensity % 4) + 1;
                        updateSpeciesIntensities(settings);
                        simulation.updateSettings(settings);

                        // show which species we actually adjusted
                        std::string speciesName = (originalSpeciesIndex == 0) ? "Red" : (originalSpeciesIndex == 1) ? "Blue"
                                                                                    : (originalSpeciesIndex == 2)   ? "Green"
                                                                                    : (originalSpeciesIndex == 3)   ? "Yellow"
                                                                                                                    : "Magenta";
                        std::cout << " J key adjusted " << speciesName << " species (position 1) intensity: " << settings.speciesSettings[position].behaviorIntensity << std::endl;
                    }
                    else
                    {
                        std::cout << " No second species available to adjust!" << std::endl;
                    }
                }

                if (keyPressed->code == sf::Keyboard::Key::K && currentSpeciesMode == SpeciesMode::Classic5)
                {
                    // adjust third visible species (position 2) if it exists
                    if (settings.speciesSettings.size() >= 3)
                    {
                        int position = 2; // always adjust third species in the current list
                        int originalSpeciesIndex = activeSpeciesIndices.size() >= 3 ? activeSpeciesIndices[position] : 2;
                        settings.speciesSettings[position].behaviorIntensity = (settings.speciesSettings[position].behaviorIntensity % 4) + 1;
                        updateSpeciesIntensities(settings);
                        simulation.updateSettings(settings);

                        // show which species we actually adjusted
                        std::string speciesName = (originalSpeciesIndex == 0) ? "Red" : (originalSpeciesIndex == 1) ? "Blue"
                                                                                    : (originalSpeciesIndex == 2)   ? "Green"
                                                                                    : (originalSpeciesIndex == 3)   ? "Yellow"
                                                                                                                    : "Magenta";
                        std::cout << " K key adjusted " << speciesName << " species (position 2) intensity: " << settings.speciesSettings[position].behaviorIntensity << std::endl;
                    }
                    else
                    {
                        std::cout << " No third species available to adjust!" << std::endl;
                    }
                }

                if (keyPressed->code == sf::Keyboard::Key::Comma && currentSpeciesMode == SpeciesMode::Classic5)
                {
                    // adjust fourth visible species (position 3) if it exists
                    if (settings.speciesSettings.size() >= 4)
                    {
                        int position = 3; // always adjust fourth species in the current list
                        int originalSpeciesIndex = activeSpeciesIndices.size() >= 4 ? activeSpeciesIndices[position] : 3;
                        settings.speciesSettings[position].behaviorIntensity = (settings.speciesSettings[position].behaviorIntensity % 4) + 1;
                        updateSpeciesIntensities(settings);
                        simulation.updateSettings(settings);

                        // show which species we actually adjusted
                        std::string speciesName = (originalSpeciesIndex == 0) ? "Red" : (originalSpeciesIndex == 1) ? "Blue"
                                                                                    : (originalSpeciesIndex == 2)   ? "Green"
                                                                                    : (originalSpeciesIndex == 3)   ? "Yellow"
                                                                                                                    : "Magenta";
                        std::cout << " Comma key adjusted " << speciesName << " species (position 3) intensity: " << settings.speciesSettings[position].behaviorIntensity << std::endl;
                    }
                    else
                    {
                        std::cout << " No fourth species available to adjust!" << std::endl;
                    }
                }

                if (keyPressed->code == sf::Keyboard::Key::Period && currentSpeciesMode == SpeciesMode::Classic5)
                {
                    // adjust fifth visible species (position 4) if it exists
                    if (settings.speciesSettings.size() >= 5)
                    {
                        int position = 4; // always adjust fifth species in the current list
                        int originalSpeciesIndex = activeSpeciesIndices.size() >= 5 ? activeSpeciesIndices[position] : 4;
                        settings.speciesSettings[position].behaviorIntensity = (settings.speciesSettings[position].behaviorIntensity % 4) + 1;
                        updateSpeciesIntensities(settings);
                        simulation.updateSettings(settings);

                        // shows which species we actually adjusted
                        std::string speciesName = (originalSpeciesIndex == 0) ? "Red" : (originalSpeciesIndex == 1) ? "Blue"
                                                                                    : (originalSpeciesIndex == 2)   ? "Green"
                                                                                    : (originalSpeciesIndex == 3)   ? "Yellow"
                                                                                                                    : "Magenta";
                        std::cout << " Period key adjusted " << speciesName << " species (position 4) intensity: " << settings.speciesSettings[position].behaviorIntensity << std::endl;
                    }
                    else
                    {
                        std::cout << " No fifth species available to adjust!" << std::endl;
                    }
                }

                if (keyPressed->code == sf::Keyboard::Key::Up)
                {
                    // checks if shift is held for mouse strength adjustment
                    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) ||
                        sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                    {
                        mouseSettings.foodStrength = std::min(mouseSettings.foodStrength + 5.0f, 1000.0f);
                        mouseSettings.repellentStrength = -mouseSettings.foodStrength * 0.5f;
                    }
                    else if (simulation.isInBenchmarkMode())
                    {
                        // in benchmark mode: dynamically add agents per algorithm
                        simulation.adjustBenchmarkAgentCount(25);
                        settings.benchmarkSettings.agentsPerAlgorithm = 
                            simulation.getSettings().benchmarkSettings.agentsPerAlgorithm;
                    }
                    else
                    {
                        simulation.adjustAgentCount(10000);  // add 10000 agents
                        settings.numAgents = simulation.getAgentCount();
                    }
                }
                if (keyPressed->code == sf::Keyboard::Key::Down)
                {
                    // checks if shift is held for mouse strength adjustment
                    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) ||
                        sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                    {
                        mouseSettings.foodStrength = std::max(mouseSettings.foodStrength - 5.0f, 5.0f);
                        mouseSettings.repellentStrength = -mouseSettings.foodStrength * 0.5f;
                    }
                    else if (simulation.isInBenchmarkMode())
                    {
                        // In benchmark mode: dynamically remove agents per algorithm
                        simulation.adjustBenchmarkAgentCount(-10);
                        settings.benchmarkSettings.agentsPerAlgorithm = 
                            simulation.getSettings().benchmarkSettings.agentsPerAlgorithm;
                    }
                    else
                    {
                        simulation.adjustAgentCount(-5000);  // Remove 1000 agents
                        settings.numAgents = simulation.getAgentCount();
                    }
                }

                // save/load
                // shift+s: toggle slime shading (avoid clashing with save)
                if (keyPressed->code == sf::Keyboard::Key::S &&
                    (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)))
                {
                    settings.slimeShadingEnabled = !settings.slimeShadingEnabled;
                    simulation.updateSettings(settings);
                    std::cout << "Slime Shading: " << (settings.slimeShadingEnabled ? "ON" : "OFF") << std::endl;
                }

                // shift+a: toggle anisotropic splats
                if (keyPressed->code == sf::Keyboard::Key::A &&
                    (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)))
                {
                    settings.anisotropicSplatsEnabled = !settings.anisotropicSplatsEnabled;
                    simulation.updateSettings(settings);
                    std::cout << "Anisotropic Splats: " << (settings.anisotropicSplatsEnabled ? "ON" : "OFF") << std::endl;
                }

                // u/p: adjust compliance strength (k)
                if (keyPressed->code == sf::Keyboard::Key::U)
                {
                    settings.complianceStrength = std::max(0.0f, settings.complianceStrength - 0.05f);
                    simulation.updateSettings(settings);
                    std::cout << "Compliance k: " << settings.complianceStrength << std::endl;
                }
                if (keyPressed->code == sf::Keyboard::Key::P)
                {
                    settings.complianceStrength = std::min(2.0f, settings.complianceStrength + 0.05f);
                    simulation.updateSettings(settings);
                    std::cout << "Compliance k: " << settings.complianceStrength << std::endl;
                }

                // ; and ' : adjust damping (d) - plain semicolon only (shift+; is cluster labels)
                if (keyPressed->code == sf::Keyboard::Key::Semicolon &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                {
                    settings.complianceDamping = std::max(0.0f, settings.complianceDamping - 0.05f);
                    simulation.updateSettings(settings);
                    std::cout << "Damping d: " << settings.complianceDamping << std::endl;
                }
                if (keyPressed->code == sf::Keyboard::Key::Apostrophe)
                {
                    settings.complianceDamping = std::min(2.0f, settings.complianceDamping + 0.05f);
                    simulation.updateSettings(settings);
                    std::cout << "Damping d: " << settings.complianceDamping << std::endl;
                }

                if (keyPressed->code == sf::Keyboard::Key::S &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                {
                    if (settings.saveToFile("simulation_settings.txt"))
                    {
                        std::cout << "Settings saved successfully" << std::endl;
                    }
                }
                if (keyPressed->code == sf::Keyboard::Key::L)
                {
                    SimulationSettings newSettings;
                    if (newSettings.loadFromFile("simulation_settings.txt"))
                    {
                        settings = newSettings;
                        simulation.~PhysarumSimulation();
                        new (&simulation) PhysarumSimulation(settings);
                        std::cout << "Settings loaded successfully" << std::endl;
                    }
                }

                // species count controls for both random and classic5 modes
                if (keyPressed->code == sf::Keyboard::Key::N)
                {
                    if (currentSpeciesMode == SpeciesMode::Random)
                    {
                        randomSpeciesCount = std::min(randomSpeciesCount + 1, 10);
                        setupSpecies(settings);
                        simulation.updateSettings(settings);
                        std::cout << "Increased random species count to " << randomSpeciesCount << std::endl;
                    }
                    else if (currentSpeciesMode == SpeciesMode::Classic5)
                    {
                        // add a random species from the classic 5+ that isn't already present
                        if (activeSpeciesIndices.size() < 8) // allow up to 8 species (5 classic + 3 malevolent)
                        {
                            addRandomClassic5Species(simulation, settings);
                            setupSpecies(settings);
                            simulation.updateSettings(settings);
                            simulation.reset();

                            // check if we added a malevolent species (indices 5-7)
                            bool hasParasite = std::find(activeSpeciesIndices.begin(), activeSpeciesIndices.end(), 5) != activeSpeciesIndices.end();
                            bool hasDeathBringer = std::find(activeSpeciesIndices.begin(), activeSpeciesIndices.end(), 6) != activeSpeciesIndices.end();
                            bool hasGuardian = std::find(activeSpeciesIndices.begin(), activeSpeciesIndices.end(), 7) != activeSpeciesIndices.end();

                            if (hasParasite || hasDeathBringer || hasGuardian)
                            {
                                std::cout << " MALEVOLENT SPECIES UNLEASHED! Now have " << activeSpeciesIndices.size() << " species active (including special ones!)" << std::endl;
                            }
                            else
                            {
                                std::cout << " Added species! Now have " << activeSpeciesIndices.size() << " Classic species active" << std::endl;
                            }
                        }
                        else
                        {
                            std::cout << "All 8 species (5 Classic + 3 Malevolent) are already active! Maximum chaos achieved!" << std::endl;
                        }
                    }
                }

                if (keyPressed->code == sf::Keyboard::Key::V)
                {
                    if (currentSpeciesMode == SpeciesMode::Random)
                    {
                        randomSpeciesCount = std::max(randomSpeciesCount - 1, 1);
                        setupSpecies(settings);
                        simulation.updateSettings(settings);
                        std::cout << "Decreased random species count to " << randomSpeciesCount << std::endl;
                    }
                    else if (currentSpeciesMode == SpeciesMode::Classic5)
                    {
                        // will remove a random species from the active ones
                        if (activeSpeciesIndices.size() > 1)
                        {
                            removeRandomClassic5Species(simulation, settings);
                            setupSpecies(settings);
                            simulation.updateSettings(settings);
                            simulation.reset();
                            std::cout << " Removed species! Now have " << activeSpeciesIndices.size() << " Classic species active" << std::endl;
                        }
                        else
                        {
                            std::cout << " Must have at least 1 species active!" << std::endl;
                        }
                    }
                }

                // individual species toggle controls (shift + number)
                // only available in classic5 mode for precise species control
                if (currentSpeciesMode == SpeciesMode::Classic5)
                {
                    bool shiftPressed = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);

                    if (shiftPressed)
                    {
                        if (keyPressed->code == sf::Keyboard::Key::Num1)
                        {
                            toggleSpecies(0, simulation, settings); // red
                            setupSpecies(settings);
                            simulation.updateSettings(settings);
                            simulation.reset();
                        }
                        else if (keyPressed->code == sf::Keyboard::Key::Num2)
                        {
                            toggleSpecies(1, simulation, settings); // blue
                            setupSpecies(settings);
                            simulation.updateSettings(settings);
                            simulation.reset();
                        }
                        else if (keyPressed->code == sf::Keyboard::Key::Num3)
                        {
                            toggleSpecies(2, simulation, settings); // green
                            setupSpecies(settings);
                            simulation.updateSettings(settings);
                            simulation.reset();
                        }
                        else if (keyPressed->code == sf::Keyboard::Key::Num4)
                        {
                            toggleSpecies(3, simulation, settings); // yellow
                            setupSpecies(settings);
                            simulation.updateSettings(settings);
                            simulation.reset();
                        }
                        else if (keyPressed->code == sf::Keyboard::Key::Num5)
                        {
                            toggleSpecies(4, simulation, settings); // magenta
                            setupSpecies(settings);
                            simulation.updateSettings(settings);
                            simulation.reset();
                        }
                        else if (keyPressed->code == sf::Keyboard::Key::Num6)
                        {
                            toggleSpecies(5, simulation, settings); // black
                            setupSpecies(settings);
                            simulation.updateSettings(settings);
                            simulation.reset();
                        }
                        else if (keyPressed->code == sf::Keyboard::Key::Num7)
                        {
                            toggleSpecies(6, simulation, settings); // crimson
                            setupSpecies(settings);
                            simulation.updateSettings(settings);
                            simulation.reset();
                        }
                        else if (keyPressed->code == sf::Keyboard::Key::Num8)
                        {
                            toggleSpecies(7, simulation, settings); // white
                            setupSpecies(settings);
                            simulation.updateSettings(settings);
                            simulation.reset();
                        }
                    }
                }

                // regenerate species for both modes
                if (keyPressed->code == sf::Keyboard::Key::G)
                {
                    if (currentSpeciesMode == SpeciesMode::Random)
                    {
                        setupSpecies(settings);
                        simulation.updateSettings(settings);
                        std::cout << "Regenerated " << randomSpeciesCount << " new random species!" << std::endl;
                    }
                    else if (currentSpeciesMode == SpeciesMode::Classic5)
                    {
                        rerollSpeciesCombination(settings);
                        simulation.updateSettings(settings);
                        simulation.reset(); // reset to recreate agents with correct species masks
                    }
                }

                // hud control keys (only when h is not used for species intensity)
                if (keyPressed->code == sf::Keyboard::Key::H)
                {
                    // only toggle hud if we're not in a mode where h controls species intensity
                    bool hUsedForSpecies = (currentSpeciesMode == SpeciesMode::Classic5 && !settings.speciesSettings.empty()) ||
                                           (currentSpeciesMode == SpeciesMode::Single && !settings.speciesSettings.empty());

                    if (!hUsedForSpecies)
                    {
                        // toggle hud visibility
                        currentHudMode = static_cast<HudMode>((static_cast<int>(currentHudMode) + 1) % 3);
                        switch (currentHudMode)
                        {
                        case HudMode::Full:
                            std::cout << " HUD: Full detailed view" << std::endl;
                            break;
                        case HudMode::Compact:
                            std::cout << " HUD: Compact minimal view" << std::endl;
                            break;
                        case HudMode::Hidden:
                            std::cout << " HUD: Hidden (press H to show)" << std::endl;
                            break;
                        }
                    }
                }

                // shift+f: toggle ALL ui off/on instantly
                // TODO: update to not toggle off overlay texture
                if (keyPressed->code == sf::Keyboard::Key::F &&
                    (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)))
                {
                    allUIHidden = !allUIHidden;
                    simulation.setHideAllUI(allUIHidden);  // tells the simulation to hide its UI too
                    if (allUIHidden) {
                        savedHudMode = currentHudMode;  // save current mode
                        currentHudMode = HudMode::Hidden;
                        std::cout << " ALL UI: OFF (Shift+F to restore)" << std::endl;
                    } else {
                        currentHudMode = savedHudMode;  // restore previous mode
                        std::cout << " ALL UI: ON (restored to " << (savedHudMode == HudMode::Full ? "Full" : "Compact") << ")" << std::endl;
                    }
                }
                // f alone: cycle hud mode (full -> compact -> hidden -> full)
                else if (keyPressed->code == sf::Keyboard::Key::F &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                {
                    allUIHidden = false;  // clears the "all hidden" state when manually cycling
                    simulation.setHideAllUI(false);  // re enable simulation UI
                    currentHudMode = static_cast<HudMode>((static_cast<int>(currentHudMode) + 1) % 3);
                    switch (currentHudMode)
                    {
                    case HudMode::Full:
                        std::cout << " HUD Mode: Full (detailed)" << std::endl;
                        break;
                    case HudMode::Compact:
                        std::cout << " HUD Mode: Compact (minimal)" << std::endl;
                        break;
                    case HudMode::Hidden:
                        std::cout << " HUD Mode: Hidden" << std::endl;
                        break;
                    }
                }

                // x without shift: toggle hud transparency (shift+x is used for mating)
                if (keyPressed->code == sf::Keyboard::Key::X &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift))
                {
                    // toggle hud transparency
                    hudTransparency = !hudTransparency;
                    std::cout << " HUD Transparency: " << (hudTransparency ? "ON" : "OFF") << std::endl;
                }

                // hud position controls (shift+1-4)
                if (keyPressed->code == sf::Keyboard::Key::Num1 &&
                    (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)))
                {
                    currentHudPosition = HudPosition::TopLeft;
                    std::cout << " HUD Position: Top Left" << std::endl;
                }
                else if (keyPressed->code == sf::Keyboard::Key::Num2 &&
                         (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)))
                {
                    currentHudPosition = HudPosition::TopRight;
                    std::cout << " HUD Position: Top Right" << std::endl;
                }
                else if (keyPressed->code == sf::Keyboard::Key::Num3 &&
                         (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)))
                {
                    currentHudPosition = HudPosition::BottomLeft;
                    std::cout << " HUD Position: Bottom Left" << std::endl;
                }
                else if (keyPressed->code == sf::Keyboard::Key::Num4 &&
                         (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)))
                {
                    currentHudPosition = HudPosition::BottomRight;
                    std::cout << " HUD Position: Bottom Right" << std::endl;
                }

                // updates simulation settings
                simulation.updateSettings(settings);
            }

            // handle mouse button events for food/repellent placement
            if (const auto *mousePressed = event->getIf<sf::Event::MouseButtonPressed>())
            {
                sf::Vector2i mousePos = sf::Mouse::getPosition(window);

                // check for modifier keys (alt or cmd/option for pellets)
                bool altPressed = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl) ||
                                  sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl) ||
                                  sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt) ||
                                  sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt) ||
                                  sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LSystem) || // cmd key on macos
                                  sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RSystem);   // right cmd key on macos...

                // scales the food strength based on agent count for consistent visibility
                float agentScalingFactor = std::sqrt(settings.numAgents / 1000.0f);
                float scaledFoodStrength = mouseSettings.foodStrength * std::max(1.0f, agentScalingFactor);
                float scaledRepellentStrength = mouseSettings.repellentStrength * std::max(1.0f, agentScalingFactor);

                if (mousePressed->button == sf::Mouse::Button::Left)
                {
                    if (altPressed)
                    {
                        // alt/cmd + left click - place gentle attractive food pellet
                        simulation.addFoodPellet(mousePos.x, mousePos.y, scaledFoodStrength * 5.0f, 200.0f, 0);
                        std::cout << "Placed ATTRACTIVE FOOD PELLET at (" << mousePos.x << ", " << mousePos.y
                                  << ") strength: " << (scaledFoodStrength * 5.0f) << " radius: 200px" << std::endl;
                    }
                    else
                    {
                        // left mouse button - place normal food/attractant trail
                        simulation.depositFood(mousePos.x, mousePos.y, scaledFoodStrength, mouseSettings.brushRadius);
                        std::cout << "Placed food trail at (" << mousePos.x << ", " << mousePos.y << ") strength: " << scaledFoodStrength
                                  << " (base: " << mouseSettings.foodStrength << " × " << agentScalingFactor << ")" << std::endl;
                    }
                }
                else if (mousePressed->button == sf::Mouse::Button::Right)
                {
                    if (altPressed)
                    {
                        // alt/cmd + right click - place gentle repulsive pellet
                        simulation.addFoodPellet(mousePos.x, mousePos.y, scaledRepellentStrength * 5.0f, 200.0f, 0);
                        std::cout << "Placed REPULSIVE FOOD PELLET at (" << mousePos.x << ", " << mousePos.y
                                  << ") strength: " << (scaledRepellentStrength * 5.0f) << " radius: 200px" << std::endl;
                    }
                    else
                    {
                        // right mouse button - place normal repellent trail
                        simulation.depositRepellent(mousePos.x, mousePos.y, scaledRepellentStrength, mouseSettings.brushRadius);
                        std::cout << "Placed repellent trail at (" << mousePos.x << ", " << mousePos.y << ") strength: " << scaledRepellentStrength
                                  << " (base: " << mouseSettings.repellentStrength << " × " << agentScalingFactor << ")" << std::endl;
                    }
                }
            }
        }

        // continuous mouse placement (hold and drag for a sorta brush like drawing though when resizing window its placed in a wrong spot)
        if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left) || sf::Mouse::isButtonPressed(sf::Mouse::Button::Right))
        {
            sf::Vector2i mousePos = sf::Mouse::getPosition(window);

            // only gets placed if mouse is within window bounds
            if (mousePos.x >= 0 && mousePos.x < WINDOW_WIDTH &&
                mousePos.y >= 0 && mousePos.y < WINDOW_HEIGHT)
            {
                // scale food strength based on agent count for consistent visibility
                float agentScalingFactor = std::sqrt(settings.numAgents / 1000.0f); // square root scaling for balance
                float scaledFoodStrength = mouseSettings.foodStrength * std::max(1.0f, agentScalingFactor);
                float scaledRepellentStrength = mouseSettings.repellentStrength * std::max(1.0f, agentScalingFactor);

                if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left))
                {
                    // continuous food placement with reduced strength for smooth drawing
                    simulation.depositFood(mousePos.x, mousePos.y, scaledFoodStrength * 0.1f, mouseSettings.brushRadius);
                }
                else if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Right))
                {
                    // continuous repellent placement with reduced strength for smooth drawing
                    simulation.depositRepellent(mousePos.x, mousePos.y, scaledRepellentStrength * 0.1f, mouseSettings.brushRadius);
                }
            }
        }

        // update simulation
        simulation.update(deltaTime.asSeconds());

        // render
        window.clear(sf::Color::Black);
        simulation.draw(window, font);

        // draws the hud based on current mode
        switch (currentHudMode)
        {
        case HudMode::Full:
            drawSimpleHUD(window, font, settings, mouseSettings, simulation.getAgentCount(), currentSpeciesMode, randomSpeciesCount);
            break;
        case HudMode::Compact:
            drawCompactHUD(window, font, settings, mouseSettings, simulation.getAgentCount(), currentSpeciesMode, randomSpeciesCount, currentHudPosition);
            break;
        case HudMode::Hidden:
            // or draw nothing
            break;
        }

        window.display();
    }

    return 0;
}
