#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <string>

class SimulationSettings
{
public:
    // algorithm categories:
    // EXPLORERS - dont know goal they must discover it (true exploration) i.e. ignorant, naive search, uninformed search, blind search
    // PATHFINDERS - know goal upfront, compute optimal path (the cheaters)
    enum class Algos{
        // blind search
        Slime,         // slime mold: trail based biological intelligence (not really working in benchmark mode)
        DFS,           // depth first search: burns through maze like lightning
        Dijkstra,      // dijkstra: uniform cost search with diagonal costs
        
        // goal aware
        AStar,         // A*: uses distance to goal heuristic
        Greedy,        // greedy best first: beelines toward goal, suboptimal though
        JPS,           // jump point search: "optimized" A*
        Theta,         // any angle theta*: smooth paths with goal heuristic
    };

    // returns true if algorithm is uninformed (no goal heuristic)
    // explorers = blind/uninformed search (slime [control], bidirectional, etc)
    // pathfinders = informed search with heuristics (A*, greedy, jump, theta*)
    static bool isExplorer(Algos a) {
        switch(a) {
            case Algos::DFS:            // blind depth first traversal
            case Algos::Dijkstra:       // uniform cost search, no goal heuristic
            case Algos::Slime:
                return true;
            default:
                return false;
        }
    }
    
    // returns true if algorithm needs goal position to work
    static bool isPathfinder(Algos a) {
        return !isExplorer(a);
    }

    enum class AlgoAssignment {
        Off,
        SpeciesUnique,
        AgentUnique, // TODO agent unique algorithm assignment
    };

    AlgoAssignment algoAssignment = AlgoAssignment::Off;

    std::vector<Algos> speciesAlgos;

    static const char *algoNames(Algos a){
        switch(a){
            case Algos::Slime:
                return "Slime";
            case Algos::DFS:
                return "DFS";
            case Algos::Dijkstra:
                return "Dijkstra";
            case Algos::AStar:
                return "A*";
            case Algos::Greedy:
                return "Greedy";
            case Algos::JPS:
                return "JPS";
            case Algos::Theta:
                return "Theta*";
            default:
                return "Unknown";
        }
    }
    
    // get category name for display
    static const char* algoCategory(Algos a) {
        return isExplorer(a) ? "Explorer" : "Pathfinder";
    }


    // simulation settings
    int stepsPerFrame = 1;
    int width = 800;
    int height = 600;
    int numAgents = 50000;

    enum class SpawnMode
    {
        Random,
        Point,
        InwardCircle,
        RandomCircle,
        Clusters
    };
    SpawnMode spawnMode = SpawnMode::Clusters;

    // algorithm benchmark mode settings
    struct BenchmarkSettings {
        bool enabled = false;              // is benchmark mode active
        int agentsPerAlgorithm = 50;        // agents per algorithm species (1 for debugging)
        int obstacleCount = 60;            // number of random obstacles (more dense)
        int obstacleMinSize = 2;           // min obstacle size in grid cells
        int obstacleMaxSize = 16;          // max obstacle size in grid cells (larger variety)
        float goalArrivalRadius = 25.0f;   // distance to goal to count as "arrived"
        float agentMoveSpeed = 3.0f;       // movement speed in benchmark mode
        int pathCellSize = 8;              // grid cell size for pathfinding (larger = faster)
        float mazeDensity = 0.6f;          // maze wall density (0.0-1.0)
        bool useMazeLayout = true;         // use maze style walls instead of random blocks in previous rough version
    };
    BenchmarkSettings benchmarkSettings;

    // trail settings
    float trailWeight = 9.0f;
    float decayRate = 0.01f;
    float diffuseRate = 0.2f;
    float displayThreshold = 0.1f;
    bool blurEnabled = true;         // enable gentle blur for smoother trails
    bool slimeShadingEnabled = false; // post process slime shading (cpu) toggle
    // motion smoothing/inertia for squishier movement (0=no smoothing, 0.95=very heavy)
    float motionInertia = 0.15f;

    // oriented gaussian splats (image-gs inspired)
    bool anisotropicSplatsEnabled = true; // draw oriented elliptical deposits aligned to agent velocity
    float splatSigmaParallel = 1.8f;      // std dev along motion direction (pixels)
    float splatSigmaPerp = 0.7f;          // std dev perpendicular to motion (pixels)
    float splatIntensityScale = 1.0f;     // multiplier for deposit intensity

    // second order compliance for heading dynamics (elastic locomotion inspired)
    float complianceStrength = 0.3f; // 0..1 how much desired turn accelerates angular velocity
    float complianceDamping = 0.5f;  // 0..1 damping on angular velocity

    // species settings (support for multiple species)
    struct SpeciesSettings
    {
        // lifecycle death handling per species
        enum class DeathBehavior
        {
            Rebirth,
            HardDeath,
            SporeBurst
        };

        // movement settings optimized for cellular network patterns
        float moveSpeed = 2.431f;  // moderate speed for stable network formation
        float turnSpeed = 16.037f; // balanced turning for exploration

        // sensor settings for network detection
        float sensorAngleSpacing = 30.0f;     // degrees - wider spacing for network sensing
        float sensorOffsetDistance = 40.730f; // longer reach for network connections
        int sensorSize = 1;

        // multi-species interaction settings
        float attractionToSelf = 1.0f;     // how much this species likes its own trail (1.0 = normal)
        float attractionToOthers = 0.3f;   // how much this species likes other species' trails
        float repulsionFromOthers = 10.0f; // negative attraction (repulsion)

        // "behavior intensity": 1 = mild, 2 = default, 3 = strong, 4 = extreme
        int behaviorIntensity = 2;

        // emergent neighbor behavior weights (boids-like)
        float alignmentWeight = 0.6f;   // align heading with nearby agents
        float cohesionWeight = 0.45f;   // steer toward local center of mass
        float separationWeight = 1.1f;  // strongly avoid crowding
        float separationRadius = 12.0f; // pixels; inside this, push away
        
        // same species affinity (for territorial/loner species that still need to mate)
        float sameSpeciesCohesionBoost = 1.0f;  // multiplier for cohesion toward same species (>1 = attracted, <1 = indifferent)
        bool separateFromSameSpecies = true;    // if false separation only applies to OTHER species (lets same species cluster)

        // quorum behavior: when many neighbors are present, bias to go straight / maintain flow
        float quorumThreshold = 6.0f; // neighbors count to trigger quorum effects
        float quorumTurnBias = 0.35f; // add to "front" sensor when quorum is reached

        // internal oscillator to induce waves/spirals
        float oscillatorStrength = 0.25f; // sensor side-bias amplitude
        float oscillatorFrequency = 0.6f; // hz-ish; relative to ~60 fps tick

        // reproduction and genetics
        bool matingEnabled = true;        // allow reproduction between agents
        float matingRadius = 14.0f;       // distance threshold to mate
        float matingEnergyCost = 0.25f;   // energy cost per parent on mating (0..1 scale)
        float offspringEnergy = 0.6f;     // starting energy of child
        bool crossSpeciesMating = true;   // allow hybrid offspring from different species
        bool onlyMateWithOtherSpecies = false; // if true, can ONLY mate with different species (Blue parasitic)
        float hybridMutationRate = 0.05f; // mutation rate applied to hybrid phenotypes

        // asexual splitting
        bool splittingEnabled = true;       // allow asexual splitting when energy is high
        float splitEnergyThreshold = 1.35f; // if energy exceeds this, agent can split
        float splitCooldownTime = 2.5f;     // seconds before it can split again

        // life cycle / rebirth
        bool rebirthEnabled = true;                           // if true and death behavior==rebirth, agent soft resets instead of being removed
        float rebirthEnergy = 0.7f;                           // energy on rebirth
        float lifespanSeconds = 10.0f;                       // natural lifespan in seconds
        DeathBehavior deathBehavior = DeathBehavior::Rebirth; // species-specific death handling

        // spore burst settings (used when death behavior == sporeburst)
        int sporeCount = 4;              // number of spores to spawn on death
        float sporeRadius = 10.0f;       // spawn radius (pixels) around parent
        float sporeEnergy = 0.5f;        // initial energy of spores
        float sporeMutationRate = 0.08f; // extra mutation applied to spores
        // energy dynamics
        float energyDecayPerStep = 0.003f;     // base energy drain per tick
        float energyGainPerNeighbor = 0.0008f; // energy gained when in crowds (per neighbor up to quorum)
        float passiveEnergyRegen = 0.0f;       // passive energy gain per frame (for loners who can't cluster)
        
        // Food eco: trail = food, eating depletes trail, death only from starvation
        bool foodEconomyEnabled = false;       // if true, use new food economy system
        float eatRate = 0.05f;                 // max amount of trail to consume per step
        float movementEnergyCost = 0.001f;     // energy cost per unit of movement
        bool canEatOtherTrails = false;        // Red predator: can eat other species' trails
        float trailFoodValue = 1.0f;           // multiplier for how much energy food provides
        
        // energy stealing (Red territorial behavior)
        bool canStealEnergy = false;           // if true, steals energy from nearby non-same-species
        float energyStealRadius = 20.0f;       // radius to steal from
        float energyStealRate = 0.002f;        // energy stolen per frame per victim
        float energyHuntThreshold = 0.4f;      // below this energy, actively hunt for victims
        
        // energy giving (Blue altruistic behavior)
        bool canGiveEnergy = false;            // if true, gives energy to nearby non-same-species
        float energyGiveRadius = 25.0f;        // radius to give energy
        float energyGiveRate = 0.001f;         // energy given per frame per recipient
        float energyGiveThreshold = 0.6f;      // only give if own energy above this
        
        // conditional rebirth (Red emergency backup)
        bool conditionalRebirthEnabled = false; // rebirth only if population below threshold
        float rebirthPopulationThreshold = 0.3f; // rebirth triggers if pop < 30% of starting
        
        // pre-death budding (Green loner behavior)
        bool preDeathBuddingEnabled = false;   // split right before dying
        float preDeathBudThreshold = 0.1f;     // bud when energy drops below this
        
        // mating energy bonus
        float matingEnergyBonus = 0.0f;        // extra energy gained when successfully mating

        // display settings
        sf::Color color = sf::Color(255, 230, 0);

        SpeciesSettings() = default;
        SpeciesSettings(float speed, float turn, float angle, float dist, sf::Color col)
            : moveSpeed(speed), turnSpeed(turn), sensorAngleSpacing(angle),
              sensorOffsetDistance(dist), color(col) {}
    };

    std::vector<SpeciesSettings> speciesSettings;

    SimulationSettings()
    {
        // the default species
        speciesSettings.push_back(SpeciesSettings());
    }

    bool saveToFile(const std::string &filename) const;
    bool loadFromFile(const std::string &filename);

    void validateAndClamp();

private:
    float degToRad(float degrees) const { return degrees * M_PI / 180.0f; }
    float radToDeg(float radians) const { return radians * 180.0f / M_PI; }
};
