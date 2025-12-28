#include "Agent.h"
#include "TrailMap.h"
#include "SpatialGrid.h"
#include "OptimizedTrailMap.h"
#include "PhysarumSimulation.h"
#include "FoodPellet.h"
#include <cmath>
#include <random>
#include <algorithm>
#include <limits>
#include <iostream>
#include <unordered_map>
#include <map>

// for float pi to avoid double promotion warnings
static constexpr float M_PIf = 3.14159265358979323846f;

// TODO: restructure to change this
struct FoodPellet;

// thread safe per agent static state for all agent types
namespace
{
    // yellow slime (erratic) state
    std::unordered_map<size_t, float> alienSpeedPhases;
    std::unordered_map<size_t, int> alienSpeedModes;
    // magenta slime (stable) state
    std::unordered_map<size_t, float> antiAlienSpeedPhases;
    std::unordered_map<size_t, int> antiAlienSpeedModes;
    std::unordered_map<size_t, int> antiAlienSpeedCounters;
    // black parasitic (hunting) state
    std::unordered_map<size_t, float> parasiticSpeedPhases;
    std::unordered_map<size_t, int> parasiticHuntModes;
    std::unordered_map<size_t, int> parasiticBurstCounters;
    // crimson state
    std::unordered_map<size_t, float> deathSpeedPhases;
    std::unordered_map<size_t, int> deathRageModes;
    std::unordered_map<size_t, float> deathRageBuildup;
    // white guardian angel (protection) state
    std::unordered_map<size_t, float> guardianSpeedPhases;
    std::unordered_map<size_t, int> guardianProtectionModes;
    std::unordered_map<size_t, float> guardianDutyLevel;
    // all other static state maps would go here...

    // mutexes for each species state
    std::mutex alien_mutex;
    std::mutex anti_alien_mutex;
    std::mutex parasitic_mutex;
    std::mutex death_bringer_mutex;
    std::mutex guardian_mutex;

    // initializes alien state for a new agent if it doesn't exist yet.
    // speedPhases drives sinusoidal speed oscillation; speedModes selects behavior variant (burst/rhythmic/etc).
    void forceAlienState(size_t id)
    {
        if (alienSpeedPhases.find(id) == alienSpeedPhases.end())
        {
            static thread_local std::mt19937 gen(std::random_device{}());
            static thread_local std::uniform_real_distribution<float> alienMoveRand(0.0f, 1.0f);
            alienSpeedPhases[id] = alienMoveRand(gen) * 2.0f * M_PIf;
            alienSpeedModes[id] = static_cast<int>(alienMoveRand(gen) * 4);
        }
    }
    // removes alien state when agent is destroyed to prevent memory leaks and stale data
    void eraseAlienState(size_t id)
    {
        alienSpeedPhases.erase(id);
        alienSpeedModes.erase(id);
    }
    // magenta (order enforcer) state init - counters track step position for geometric patterns
    void forceAntiAlienState(size_t id)
    {
        if (antiAlienSpeedPhases.find(id) == antiAlienSpeedPhases.end())
        {
            static thread_local std::mt19937 gen(std::random_device{}());
            static thread_local std::uniform_real_distribution<float> antiAlienMoveRand(0.0f, 1.0f);
            antiAlienSpeedPhases[id] = antiAlienMoveRand(gen) * 2.0f * M_PIf;
            antiAlienSpeedModes[id] = static_cast<int>(antiAlienMoveRand(gen) * 4);
            antiAlienSpeedCounters[id] = 0;
        }
    }
    void eraseAntiAlienState(size_t id)
    {
        antiAlienSpeedPhases.erase(id);
        antiAlienSpeedModes.erase(id);
        antiAlienSpeedCounters.erase(id);
    }
    // black parasitic state init - huntModes determines predator strategy, burstCounters track attack cooldowns
    void forceParasiticState(size_t id)
    {
        if (parasiticSpeedPhases.find(id) == parasiticSpeedPhases.end())
        {
            static thread_local std::mt19937 gen(std::random_device{}());
            static thread_local std::uniform_real_distribution<float> parasiticRand(0.0f, 1.0f);
            parasiticSpeedPhases[id] = parasiticRand(gen) * 2.0f * M_PIf;
            parasiticHuntModes[id] = static_cast<int>(parasiticRand(gen) * 3);
            parasiticBurstCounters[id] = 0;
        }
    }
    void eraseParasiticState(size_t id)
    {
        parasiticSpeedPhases.erase(id);
        parasiticHuntModes.erase(id);
        parasiticBurstCounters.erase(id);
    }
    // crimson death bringer state init - rageModes controls attack pattern, rageBuildup is a charge meter
    void forceDeathBringerState(size_t id)
    {
        if (deathSpeedPhases.find(id) == deathSpeedPhases.end())
        {
            static thread_local std::mt19937 gen(std::random_device{}());
            static thread_local std::uniform_real_distribution<float> deathRand(0.0f, 1.0f);
            deathSpeedPhases[id] = deathRand(gen) * 2.0f * M_PIf;
            deathRageModes[id] = static_cast<int>(deathRand(gen) * 3);
            deathRageBuildup[id] = deathRand(gen) * 0.5f;
        }
    }
    void eraseDeathBringerState(size_t id)
    {
        deathSpeedPhases.erase(id);
        deathRageModes.erase(id);
        deathRageBuildup.erase(id);
    }
    // white guardian angel state init - protectionModes sets patrol style, dutyLevel affects dedication intensity
    void forceGuardianState(size_t id)
    {
        if (guardianSpeedPhases.find(id) == guardianSpeedPhases.end())
        {
            static thread_local std::mt19937 gen(std::random_device{}());
            static thread_local std::uniform_real_distribution<float> guardianRand(0.0f, 1.0f);
            guardianSpeedPhases[id] = guardianRand(gen) * 2.0f * M_PIf;
            guardianProtectionModes[id] = static_cast<int>(guardianRand(gen) * 3);
            guardianDutyLevel[id] = guardianRand(gen) * 0.3f + 0.7f; // start with high duty
        }
    }
    void eraseGuardianState(size_t id)
    {
        guardianSpeedPhases.erase(id);
        guardianProtectionModes.erase(id);
        guardianDutyLevel.erase(id);
    }
}

Agent::Agent(float x, float y, float a, int species)
    : position(x, y), angle(a), previousAngle(a), speciesIndex(species),
      energy(1.0f), stateTimer(0.0f), behaviorState(0), speciesMask(1, 0, 0)
{
    // default species mask - will be updated by setSpeciesMask when species info is available
    setDefaultSpeciesMask(species);
    // initialize per species behavioral state for this agent.
    // each lock_guard block acquires the species-specific mutex, then calls the force* helper
    // which adds entries to the static maps only if this agent ID isn't already present.
    // this ensures each agent starts with randomized behavior modes without overwriting existing state.
    size_t id = reinterpret_cast<size_t>(this);
    {
        std::lock_guard<std::mutex> lock(alien_mutex);
        forceAlienState(id);
    }
    {
        std::lock_guard<std::mutex> lock(anti_alien_mutex);
        forceAntiAlienState(id);
    }
    {
        std::lock_guard<std::mutex> lock(parasitic_mutex);
        forceParasiticState(id);
    }
    {
        std::lock_guard<std::mutex> lock(death_bringer_mutex);
        forceDeathBringerState(id);
    }
    {
        std::lock_guard<std::mutex> lock(guardian_mutex);
        forceGuardianState(id);
    }
}
void Agent::applyGenomeToCachedParams(const SimulationSettings &settings)
{
    if (speciesIndex < 0 || speciesIndex >= static_cast<int>(settings.speciesSettings.size()))
        return;
    const auto &sp = settings.speciesSettings[speciesIndex];
    moveSpeed = sp.moveSpeed * (hasGenome ? genome.moveSpeedScale : 1.0f);
    turnSpeed = sp.turnSpeed * (hasGenome ? genome.turnSpeedScale : 1.0f);
    sensorRange = sp.sensorOffsetDistance * (hasGenome ? genome.sensorDistScale : 1.0f);
    // precompute the max turn per step (radians) for clamp in applyInertia
    maxTurnPerStep = std::clamp(turnSpeed * M_PIf / 180.0f, 0.05f, M_PIf);
}

static float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

Agent Agent::createOffspring(const Agent &a, const Agent &b, const SimulationSettings &settings)
{
    // place child near mid point
    sf::Vector2f mid((a.position.x + b.position.x) * 0.5f, (a.position.y + b.position.y) * 0.5f);
    float ang = (a.angle + b.angle) * 0.5f;
    int species = a.speciesIndex;
    bool cross = (settings.speciesSettings[species].crossSpeciesMating && a.speciesIndex != b.speciesIndex);
    if (cross)
    {
        // chooses one parent species at random for channel/color but the mark lineage
        species = (std::rand() & 1) ? a.speciesIndex : b.speciesIndex;
    }
    Agent child(mid.x, mid.y, ang, species);
    child.hasGenome = true;
    child.parentSpeciesA = a.speciesIndex;
    child.parentSpeciesB = b.speciesIndex;
    // blend and mutate genome scales
    auto blend = [](float x, float y)
    { return 0.5f * (x + y); };
    auto mutate = [&](float v, float rate)
    {
        float delta = ((std::rand() / (float)RAND_MAX) - 0.5f) * 2.0f * rate;
        return clampf(v * (1.0f + delta), 0.5f, 1.5f);
    };
    float mr = settings.speciesSettings[species].hybridMutationRate;
    child.genome.moveSpeedScale = mutate(blend(a.genome.moveSpeedScale, b.genome.moveSpeedScale), mr);
    child.genome.turnSpeedScale = mutate(blend(a.genome.turnSpeedScale, b.genome.turnSpeedScale), mr);
    child.genome.sensorAngleScale = mutate(blend(a.genome.sensorAngleScale, b.genome.sensorAngleScale), mr);
    child.genome.sensorDistScale = mutate(blend(a.genome.sensorDistScale, b.genome.sensorDistScale), mr);
    child.genome.alignWScale = mutate(blend(a.genome.alignWScale, b.genome.alignWScale), mr);
    child.genome.cohWScale = mutate(blend(a.genome.cohWScale, b.genome.cohWScale), mr);
    child.genome.sepWScale = mutate(blend(a.genome.sepWScale, b.genome.sepWScale), mr);
    child.genome.oscStrengthScale = mutate(blend(a.genome.oscStrengthScale, b.genome.oscStrengthScale), mr);
    child.genome.oscFreqScale = mutate(blend(a.genome.oscFreqScale, b.genome.oscFreqScale), mr);
    child.energy = settings.speciesSettings[species].offspringEnergy;
    child.applyGenomeToCachedParams(settings);
    return child;
}

Agent::LifeEvent Agent::updateEnergyAndState(const SimulationSettings &settings)
{
    if (speciesIndex < 0 || speciesIndex >= static_cast<int>(settings.speciesSettings.size()))
        return LifeEvent::None;
    const auto &sp = settings.speciesSettings[speciesIndex];
    // age and cooldowns
    ageSeconds += 1.0f / 60.0f;
    if (mateCooldown > 0)
        mateCooldown -= 1.0f / 60.0f;
    if (splitCooldown > 0)
        splitCooldown -= 1.0f / 60.0f;

    // reworked so there isnt magic energy decay - energy comes from eating, costs from moving blah blah
    if (!sp.foodEconomyEnabled)
    {
        // legacy: energy drift with a crowd bonus (approx via the quorum threshold neighborhood size)
        energy -= sp.energyDecayPerStep;
        // crowd energy bonus will be added in senseWithSpatialGrid using neighborCount
    }

    // rebirth if lifespan exceeded or out of energy
    // death only from starvation not lifespan timer exceedance 
    bool expired;
    if (sp.foodEconomyEnabled)
    {
        expired = (energy <= 0.0f);  // only die from starvation
    }
    else
    {
        expired = (ageSeconds > sp.lifespanSeconds || energy <= 0.05f);
    }
    
    if (expired)
    {
        using DB = SimulationSettings::SpeciesSettings::DeathBehavior;
        switch (sp.deathBehavior)
        {
        case DB::Rebirth:
            if (sp.rebirthEnabled)
            {
                // soft reset with mutation
                hasGenome = true;
                genome.moveSpeedScale = clampf(genome.moveSpeedScale * (0.98f + 0.04f * ((std::rand() / (float)RAND_MAX) - 0.5f)), 0.5f, 1.5f);
                genome.turnSpeedScale = clampf(genome.turnSpeedScale * (0.98f + 0.04f * ((std::rand() / (float)RAND_MAX) - 0.5f)), 0.5f, 1.5f);
                energy = sp.rebirthEnergy;
                ageSeconds = 0.0f;
                applyGenomeToCachedParams(settings);
                return LifeEvent::Rebirth;
            }
            // if rebirth disabled then just fall through to hard death ( TODO: confirm works)
            [[fallthrough]];
        case DB::HardDeath:
        case DB::SporeBurst:
            return LifeEvent::Died;
        }
    }
    return LifeEvent::None;
}

Agent::~Agent()
{
    // cleans up all per agent state from the static maps when this agent is destroyed
    // this hopefully prevents memory leaks so a new agent allocated at the same address
    // and won't accidentally inherit stale behavioral state from a dead agent
    size_t id = reinterpret_cast<size_t>(this);
    {
        std::lock_guard<std::mutex> lock(alien_mutex);
        eraseAlienState(id);
    }
    {
        std::lock_guard<std::mutex> lock(anti_alien_mutex);
        eraseAntiAlienState(id);
    }
    {
        std::lock_guard<std::mutex> lock(parasitic_mutex);
        eraseParasiticState(id);
    }
    {
        std::lock_guard<std::mutex> lock(death_bringer_mutex);
        eraseDeathBringerState(id);
    }
    {
        std::lock_guard<std::mutex> lock(guardian_mutex);
        eraseGuardianState(id);
    }
}

// second order compliant heading smoothing using angular velocity
// instead of snapping to the new heading instantly this will create a smoother more organic turn
// by modeling the agents heading as a damped spring system (physics n such)
void Agent::applyInertia(const SimulationSettings &settings)
{
    // inertiaFactor controls how much of the previous heading to retain (0 = instant snap | 0.99 = very sluggish)
    float inertiaFactor = std::clamp(settings.motionInertia, 0.0f, 0.99f);

    // computing the signed angle error between current desired heading and where we were last frame
    // remainderf wraps the result to [-pi, pi] so we always take the shortest turn direction.
    float error = std::remainderf(angle - previousAngle, 2.0f * M_PIf);

    // use member angularVelocity for current rotational speed (radians per frame)
    float w = angularVelocity;

    // discrete second order spring damper update:
    // k (complianceStrength) acts as spring constant higher values accelerate toward target faster
    // c (complianceDamping) acts as damper prevents overshooting and oscillation
    // the update adds spring acceleration and then applies damping to the velocity
    float k = settings.complianceStrength;
    float c = settings.complianceDamping;
    w += k * error;                         // accelerate toward target based on error magnitude
    w *= std::max(0.0f, 1.0f - c * 0.5f);   // apply damping to prevent oscillation

    // final smoothed angle combines: base heading + fraction of error + accumulated angular velocity
    float smoothed = previousAngle + error * (1.0f - inertiaFactor) + w;

    // clamp turn rate to species maximum to prevent unrealistic instant direction changes
    float maxTurn = maxTurnPerStep;
    float delta = std::remainderf(smoothed - previousAngle, 2.0f * M_PIf);
    delta = std::clamp(delta, -maxTurn, maxTurn);
    smoothed = previousAngle + delta;

    previousAngle = smoothed;
    angle = smoothed;
    angularVelocity = w;
}

void Agent::setDefaultSpeciesMask(int localSpeciesIndex)
{
    // sets up all the species mask for multi species support (up to 8 species)
    // this just maps local species index (position in settings array) to rendering channels
    if (localSpeciesIndex == 0)
        speciesMask = sf::Vector3i(1, 0, 0); // red in r channel
    else if (localSpeciesIndex == 1)
        speciesMask = sf::Vector3i(0, 1, 0); // blue in g channel
    else if (localSpeciesIndex == 2)
        speciesMask = sf::Vector3i(0, 0, 1); // green in b channel
    else if (localSpeciesIndex == 3)
        speciesMask = sf::Vector3i(1, 1, 0); // yellow (red + green channels)
    else if (localSpeciesIndex == 4)
        speciesMask = sf::Vector3i(1, 0, 1); // magenta (red + blue channels)
    else if (localSpeciesIndex == 5)
        speciesMask = sf::Vector3i(0, 0, 0); // black - special dark rendering
    else if (localSpeciesIndex == 6)
        speciesMask = sf::Vector3i(1, 0, 0); // crimson - red channel with high intensity
    else if (localSpeciesIndex == 7)
        speciesMask = sf::Vector3i(1, 1, 1); // white - all channels for bright white
    else
        speciesMask = sf::Vector3i(1, 1, 1); // default: white (all channels)
}

void Agent::setSpeciesMaskFromOriginalIndex(int originalSpeciesIndex)
{
    // set species mask based on the original species type (0-7 for all species)
    // this makes sure the correct color rendering after species reroll
    if (originalSpeciesIndex == 0)
        speciesMask = sf::Vector3i(1, 0, 0); // red in r channel
    else if (originalSpeciesIndex == 1)
        speciesMask = sf::Vector3i(0, 1, 0); // blue in g channel
    else if (originalSpeciesIndex == 2)
        speciesMask = sf::Vector3i(0, 0, 1); // green in b channel
    else if (originalSpeciesIndex == 3)
        speciesMask = sf::Vector3i(1, 1, 0); // yellow (red + green channels)
    else if (originalSpeciesIndex == 4)
        speciesMask = sf::Vector3i(1, 0, 1); // magenta (red + blue channels)
    else if (originalSpeciesIndex == 5)
        speciesMask = sf::Vector3i(0, 0, 0); // black - special dark rendering
    else if (originalSpeciesIndex == 6)
        speciesMask = sf::Vector3i(1, 0, 0); // crimson - red channel with high intensity
    else if (originalSpeciesIndex == 7)
        speciesMask = sf::Vector3i(1, 1, 1); // white - all channels for bright white
    else
        speciesMask = sf::Vector3i(1, 1, 1); // default: white (all channels)
}

void Agent::move(const SimulationSettings &settings)
{
    if (speciesIndex >= static_cast<int>(settings.speciesSettings.size()))
        return;

    const auto &species = settings.speciesSettings[speciesIndex];

    float moveSpeed = species.moveSpeed;

    // yellow alien movement: designed to feel "otherworldly" and unpredictable.
    // detects yellow species by RGB values rather than hardcoded index for flexibility.
    // chaosLevel scales with behaviorIntensity to control how erratic the movement becomes.
    bool isYellowAlienSpecies = (species.color.r > 200 && species.color.g > 200 && species.color.b < 150);
    if (isYellowAlienSpecies)
    {
        float chaosLevel = species.behaviorIntensity * 0.15f;
        float phase;
        int mode;
        // initialize per agent erratic state on first frame.
        // using instance variables here instead of static maps for simpler hot-path access.
        if (!alienStateInit)
        {
            static thread_local std::mt19937 gen(std::random_device{}());
            static thread_local std::uniform_real_distribution<float> alienMoveRand(0.0f, 1.0f);
            alienSpeedPhase = alienMoveRand(gen) * 2.0f * M_PIf;
            alienSpeedMode = static_cast<int>(alienMoveRand(gen) * 4);
            alienStateInit = true;
        }
        // advance the oscillation phase each frame; occasionally switch behavior mode.
        // higher chaosLevel = faster phase advance and more frequent mode switches.
        alienSpeedPhase += 0.08f + chaosLevel * 0.1f;
        {
            static thread_local std::mt19937 gen(std::random_device{}());
            static thread_local std::uniform_real_distribution<float> alienMoveRand(0.0f, 1.0f);
            if (alienMoveRand(gen) < chaosLevel * 0.02f)
                alienSpeedMode = static_cast<int>(alienMoveRand(gen) * 4);
        }
        phase = alienSpeedPhase;
        mode = alienSpeedMode;

        // each mode creates a different speed pattern over time.
        // burst = sudden accelerations, rhythmic = smooth oscillation,
        // jittery = small random variations, erratic = unpredictable but bounded.
        switch (mode)
        {
        case 0: // burst movement - sudden speed changes but realistic
        {
            // probabilistic speed changes: rare 2x burst, rarer slow-down, otherwise gentle sine wave.
            // chaosLevel affects both probability and magnitude of speed changes.
            static thread_local std::mt19937 gen(std::random_device{}());
            static thread_local std::uniform_real_distribution<float> alienMoveRand(0.0f, 1.0f);
            if (alienMoveRand(gen) < chaosLevel * 0.1f)
            {
                moveSpeed *= 2.0f + chaosLevel; // moderate burst (max ~2.5x)
            }
            else if (alienMoveRand(gen) < chaosLevel * 0.05f)
            {
                moveSpeed *= 0.2f; // slow here down significantly
            }
            else
            {
                moveSpeed *= 0.7f + std::sin(phase * 2.0f) * 0.3f; // gentle oscillation
            }
            break;
        }

        case 1: // rhythmic movement - sin*cos creates complex but smooth pattern
        {
            float rhythm = 1.0f + std::sin(phase * 1.5f) * std::cos(phase * 3.0f) * (0.4f + chaosLevel * 0.3f);
            moveSpeed *= std::abs(rhythm); // always positive :)
            break;
        }

        case 2: // jittery movement - rapid but small variations
        {
            static thread_local std::mt19937 gen(std::random_device{}());
            static thread_local std::uniform_real_distribution<float> alienMoveRand(0.0f, 1.0f);
            if (alienMoveRand(gen) < chaosLevel * 0.05f)
            {
                moveSpeed *= 2.5f + chaosLevel * 0.5f; // quick dash (max ~3xish)
            }
            else
            {
                moveSpeed *= 0.8f + alienMoveRand(gen) * 0.6f; // normal jitter (0.8-1.4x)
            }
            break;
        }

        case 3: // erratic movement - unpredictable but bounded
        {
            static thread_local std::mt19937 gen(std::random_device{}());
            static thread_local std::uniform_real_distribution<float> alienMoveRand(0.0f, 1.0f);
            float erraticFactor = 0.5f + alienMoveRand(gen) * (1.5f + chaosLevel);
            // no more backward movement - always forward but variable speed
            moveSpeed *= erraticFactor; // range: 0.5x to ~2.2x
            break;
        }
        }

        // clamp to more reasonable bounds (no negative speed, max 3x normal)
        moveSpeed = std::clamp(moveSpeed, species.moveSpeed * 0.1f, species.moveSpeed * 3.0f);
    }
    // magenta (species 4) - the anti-alien: predictable, geometric, stable.
    // designed to visually contrast with yellow's chaos by being completely orderly.
    // orderLevel scales conservatively (0.15x) to keep movement very consistent.
    else if (speciesIndex == 4)
    {
        static thread_local std::mt19937 gen(std::random_device{}());

        float orderLevel = species.behaviorIntensity * 0.15f; // reduced order impact for realism

        // initialize per agent orderly state once - similar pattern to yellow but slower phase advance.
        if (!antiAlienStateInit)
        {
            static thread_local std::uniform_real_distribution<float> antiAlienMoveRand(0.0f, 1.0f);
            antiAlienSpeedPhase = antiAlienMoveRand(gen) * 2.0f * M_PIf;
            antiAlienSpeedMode = static_cast<int>(antiAlienMoveRand(gen) * 4);
            antiAlienSpeedCounter = 0;
            antiAlienStateInit = true;
        }

        // update phase slowly for more deliberate movement.
        // counter triggers sequential mode switches at regular intervals rather than random.
        antiAlienSpeedPhase += 0.03f + orderLevel * 0.03f;
        antiAlienSpeedCounter++;

        // switch modes every ~120-180 frames in sequence (not random) for predictability.
        if (antiAlienSpeedCounter % (120 + static_cast<int>(orderLevel * 60)) == 0)
        {
            static thread_local std::uniform_real_distribution<float> antiAlienMoveRand(0.0f, 1.0f);
            antiAlienSpeedMode = (antiAlienSpeedMode + 1) % 4; // sequential switching
        }

        float phase = antiAlienSpeedPhase;
        int mode = antiAlienSpeedMode;

        switch (mode)
        {
        case 0: // steady cruiser - highly consistent speed
        {
            float steadyModulation = 1.0f + std::sin(phase) * 0.05f; // minimal variation
            moveSpeed *= steadyModulation;
            break;
        }

        case 1: // harmonic oscillator - gentle, smooth speed changes
        {
            float harmonicSpeed = 1.0f + std::cos(phase * 1.5f) * 0.2f; // range: 0.8x to 1.2x
            moveSpeed *= harmonicSpeed;
            break;
        }

        case 2: // gradual accelerator - smooth speed transitions
        {
            float acceleration = 0.9f + (std::sin(phase * 0.3f) + 1.0f) * 0.2f; // range: 0.9x to 1.3x
            moveSpeed *= acceleration;
            break;
        }

        case 3: // perfect consistency - minimal variation
        {
            moveSpeed *= 1.0f + orderLevel * 0.05f; // tiny boost, very stable
            break;
        }
        }

        // clamp to more conservative, realistic bounds
        moveSpeed = std::clamp(moveSpeed, species.moveSpeed * 0.7f, species.moveSpeed * 1.5f);
    }
    // black parasitic slime (species 5) - predator/infiltrator behavior.
    // three distinct hunting modes: stealth for sneaking, burst for attacks, infiltration for variable speed.
    // uses mutex-protected static maps because parasitic behavior needs more complex state tracking.
    else if (speciesIndex == 5 || (species.color.r < 60 && species.color.g < 60 && species.color.b < 60))
    {
        static thread_local std::mt19937 gen(std::random_device{}());
        static thread_local std::uniform_real_distribution<float> parasiticRand(0.0f, 1.0f);

        size_t agentId = reinterpret_cast<size_t>(this);
        float parasiteLevel = species.behaviorIntensity * 0.2f;

        // state management under mutex: phase advances each frame, burstCounters track attack cooldowns,
        // huntModes can switch randomly based on parasiteLevel probability.
        {
            std::lock_guard<std::mutex> lock(parasitic_mutex);
            forceParasiticState(agentId);

            parasiticSpeedPhases[agentId] += 0.04f + parasiteLevel * 0.08f;
            parasiticBurstCounters[agentId]++;;

            // switch hunting modes occasionally
            if (parasiticRand(gen) < parasiteLevel * 0.03f)
            {
                parasiticHuntModes[agentId] = static_cast<int>(parasiticRand(gen) * 3);
                parasiticBurstCounters[agentId] = 0;
            }
        }

        float phase;
        int mode, burstCounter;
        {
            std::lock_guard<std::mutex> lock(parasitic_mutex);
            phase = parasiticSpeedPhases[agentId];
            mode = parasiticHuntModes[agentId];
            burstCounter = parasiticBurstCounters[agentId];
        }

        // three hunting strategies, each optimized for different prey situations.
        switch (mode)
        {
        case 0: // stealth mode - very slow, uses sine wave for minimal but organic speed variation.
                // ideal for sneaking into colonies undetected.
        {
            moveSpeed *= 0.3f + std::sin(phase) * 0.2f; // very slow like range: 0.1x to 0.5x
            break;
        }

        case 1: // burst mode - ambush predator pattern.
                // every 180 frames (~3 sec at 60fps), sprint explosively for 30 frames (~0.5 sec).
                // between bursts, move slowly to conserve energy and avoid detection.
        {
            if (burstCounter % 180 < 30) // burst for 0.5 seconds every 3 seconds
            {
                moveSpeed *= 3.5f + parasiteLevel * 1.5f; // explosive speed (up to 5x)
            }
            else
            {
                moveSpeed *= 0.6f; // slow between bursts
            }
            break;
        }

        case 2: // infiltration mode - sin*cos creates complex but bounded speed pattern.
                // unpredictable enough to confuse prey but not so erratic as to draw attention.
        {
            float infiltration = 0.8f + std::cos(phase * 1.5f) * std::sin(phase * 0.8f) * 0.4f;
            moveSpeed *= std::abs(infiltration); // range: 0.4x to 1.2x
            break;
        }
        }

        // clamp parasitic movement bounds
        moveSpeed = std::clamp(moveSpeed, species.moveSpeed * 0.1f, species.moveSpeed * 5.0f);
    }
    // crimson death bringer (species 6) - pure aggression and destruction.
    // rageBuildup accumulates over time and triggers mode switches when it hits 1.0.
    // all three modes are fast; this species never moves slowly.
    else if (speciesIndex == 6 || (species.color.r > 100 && species.color.g < 50 && species.color.b < 50))
    {
        static thread_local std::mt19937 gen(std::random_device{}());

        size_t agentId = reinterpret_cast<size_t>(this);
        float rageLevel = species.behaviorIntensity * 0.25f;

        // rage mechanics: phase advances for oscillation, rageBuildup accumulates until overflow
        // triggers a mode switch. this creates escalating aggression patterns.
        {
            std::lock_guard<std::mutex> lock(death_bringer_mutex);
            forceDeathBringerState(agentId);

            deathSpeedPhases[agentId] += 0.06f + rageLevel * 0.1f;
            deathRageBuildup[agentId] += rageLevel * 0.02f; // constantly building rage

            // when rage overflows, switch to next mode and reset
            if (deathRageBuildup[agentId] > 1.0f)
            {
                deathRageModes[agentId] = (deathRageModes[agentId] + 1) % 3;
                deathRageBuildup[agentId] = 0.0f;
            }
        }

        float phase, rage;
        int mode;
        {
            std::lock_guard<std::mutex> lock(death_bringer_mutex);
            phase = deathSpeedPhases[agentId];
            mode = deathRageModes[agentId];
            rage = std::min(deathRageBuildup[agentId], 1.0f);
        }

        // all modes are aggressive; rage value amplifies speed in each mode.
        switch (mode)
        {
        case 0: // relentless pursuit - constant high speed, no variation.
                // simple but effective: just keeps coming at maximum speed.
        {
            moveSpeed *= 1.8f + rage * 1.2f + rageLevel * 0.8f; // range: 1.8x to 3.8x
            break;
        }

        case 1: // berserker - fast sine oscillation creates violent speed lurching.
                // rage amplifies both base speed and oscillation range.
        {
            float berserkerSpeed = 2.5f + std::sin(phase * 3.0f) * 0.8f + rage * 1.5f;
            moveSpeed *= berserkerSpeed; // range: 1.7x to 4.8x
            break;
        }

        case 2: // charge - speed scales with accumulated rage.
                // starts slower but builds to devastating speed as rage accumulates.
        {
            float chargeMultiplier = 1.5f + rage * 2.5f + std::cos(phase) * 0.3f;
            moveSpeed *= chargeMultiplier; // range: 1.2x to 4.3x
            break;
        }
        }

        // clamp movement bounds (always fast)
        moveSpeed = std::clamp(moveSpeed, species.moveSpeed * 1.2f, species.moveSpeed * 5.0f);
    }
    // white guardian angel (species 7) - protective, graceful, steady movement.
    // dutyLevel represents dedication to protection, gradually increases over time.
    // designed to feel calming and defensive rather than aggressive.
    else if (speciesIndex == 7 || (species.color.r > 200 && species.color.g > 200 && species.color.b > 200))
    {
        static thread_local std::mt19937 gen(std::random_device{}());
        static thread_local std::uniform_real_distribution<float> guardianRand(0.0f, 1.0f);

        size_t agentId = reinterpret_cast<size_t>(this);
        float protectionLevel = species.behaviorIntensity * 0.18f;

        // guardian state: slow phase advance for deliberate movement,
        // dutyLevel gradually increases (capped at 1.0) representing growing protectiveness.
        {
            std::lock_guard<std::mutex> lock(guardian_mutex);
            forceGuardianState(agentId);

            guardianSpeedPhases[agentId] += 0.02f + protectionLevel * 0.04f; // slower, more deliberate
            guardianDutyLevel[agentId] = std::min(guardianDutyLevel[agentId] + protectionLevel * 0.01f, 1.0f);

            // mode switches are rare and random to maintain stability
            if (guardianRand(gen) < protectionLevel * 0.015f)
            {
                guardianProtectionModes[agentId] = static_cast<int>(guardianRand(gen) * 3);
            }
        }

        float phase, duty;
        int mode;
        {
            std::lock_guard<std::mutex> lock(guardian_mutex);
            phase = guardianSpeedPhases[agentId];
            mode = guardianProtectionModes[agentId];
            duty = guardianDutyLevel[agentId];
        }

        switch (mode)
        {
        case 0: // a gentle patrol - steady and predictable movement
        {
            float patrolSpeed = 0.9f + std::sin(phase) * 0.2f + duty * 0.3f;
            moveSpeed *= patrolSpeed; // range: 0.7x to 1.4x
            break;
        }

        case 1: // protective response - speed up when needed
        {
            if (guardianRand(gen) < protectionLevel * 0.1f) // emergency response
            {
                moveSpeed *= 2.0f + duty * 0.8f; // quick response: 2.0x to 2.8x
            }
            else
            {
                moveSpeed *= 1.0f + duty * 0.3f; // normal patrol: 1.0x to 1.3x
            }
            break;
        }

        case 2: // glide - smooth, flowing movement
        {
            float glideSpeed = 1.1f + std::cos(phase * 0.8f) * 0.15f + duty * 0.2f;
            moveSpeed *= glideSpeed; // range: 0.95x to 1.45x
            break;
        }
        }

        // clamp movement bounds (moderate speed)
        moveSpeed = std::clamp(moveSpeed, species.moveSpeed * 0.6f, species.moveSpeed * 3.0f);
    }

    // apply heading inertia/compliance for squishier motion
    applyInertia(settings);

    // using sincosf for better throughput on apple platforms
    float s, c;
    s = std::sinf(angle);
    c = std::cosf(angle);
    position.x += moveSpeed * c;
    position.y += moveSpeed * s;

    wrapPosition(settings.width, settings.height);
}

void Agent::sense(const float *chemoattractant, int width, int height, const SimulationSettings &settings)
{
    if (speciesIndex >= static_cast<int>(settings.speciesSettings.size()))
        return;

    const auto &species = settings.speciesSettings[speciesIndex];

    // for applying genome scaling to sensor angle
    float sensorAngleRad = species.sensorAngleSpacing * (hasGenome ? genome.sensorAngleScale : 1.0f) * M_PI / 180.0f;
    float leftAngle = angle - sensorAngleRad;
    float rightAngle = angle + sensorAngleRad;

    // and for genome scaling to sensor distance
    float sensorDist = species.sensorOffsetDistance * (hasGenome ? genome.sensorDistScale : 1.0f);

    // calculate sensor positions
    sf::Vector2f forwardPos = position + sf::Vector2f(
                                             sensorDist * std::cos(angle),
                                             sensorDist * std::sin(angle));
    sf::Vector2f leftPos = position + sf::Vector2f(
                                          sensorDist * std::cos(leftAngle),
                                          sensorDist * std::sin(leftAngle));
    sf::Vector2f rightPos = position + sf::Vector2f(
                                           sensorDist * std::cos(rightAngle),
                                           sensorDist * std::sin(rightAngle));

    // sample Chemoattractant at sensor positions
    float forward = sampleChemoattractant(chemoattractant,
                                          static_cast<int>(forwardPos.x), static_cast<int>(forwardPos.y), width, height);
    float left = sampleChemoattractant(chemoattractant,
                                       static_cast<int>(leftPos.x), static_cast<int>(leftPos.y), width, height);
    float right = sampleChemoattractant(chemoattractant,
                                        static_cast<int>(rightPos.x), static_cast<int>(rightPos.y), width, height);

    // make movement decision
    float turnSpeedRad = species.turnSpeed * M_PI / 180.0f;

    if (forward > left && forward > right)
    {
        // continue straight
    }
    else if (forward < left && forward < right)
    {
        // random turn when both sides are better than forward
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 1);
        angle += (dis(gen) == 0 ? -1 : 1) * turnSpeedRad;
    }
    else if (left > right)
    {
        angle -= turnSpeedRad;
    }
    else if (right > left)
    {
        angle += turnSpeedRad;
    }
}

// multi-species sensing - uses species interaction parameters
void Agent::senseMultiSpecies(TrailMap &trailMap, const SimulationSettings &settings)
{
    if (speciesIndex >= static_cast<int>(settings.speciesSettings.size()))
        return;

    const auto &species = settings.speciesSettings[speciesIndex];

    float sensorAngleRad = species.sensorAngleSpacing * (hasGenome ? genome.sensorAngleScale : 1.0f) * M_PI / 180.0f;
    float leftAngle = angle - sensorAngleRad;
    float rightAngle = angle + sensorAngleRad;

    float sensorDist = species.sensorOffsetDistance * (hasGenome ? genome.sensorDistScale : 1.0f);

    // calculate sensor positions
    sf::Vector2f forwardPos = position + sf::Vector2f(
                                             sensorDist * std::cos(angle),
                                             sensorDist * std::sin(angle));
    sf::Vector2f leftPos = position + sf::Vector2f(
                                          sensorDist * std::cos(leftAngle),
                                          sensorDist * std::sin(leftAngle));
    sf::Vector2f rightPos = position + sf::Vector2f(
                                           sensorDist * std::cos(rightAngle),
                                           sensorDist * std::sin(rightAngle));


    // yellow species: quantum alien - truly alien, incomprehensible behavior
    if (speciesIndex == 3)
    {
        float frontWeight = senseYellowQuantumAlien(trailMap, forwardPos.x, forwardPos.y, species);
        float leftWeight = senseYellowQuantumAlien(trailMap, leftPos.x, leftPos.y, species);
        float rightWeight = senseYellowQuantumAlien(trailMap, rightPos.x, rightPos.y, species);

        applyAlienQuantumTurning(frontWeight, leftWeight, rightWeight, species);
        return;
    }

    // magenta species: cosmic order enforcer - logical, robotic anti-alien
    if (speciesIndex == 4)
    {
        float frontWeight = senseMagentaOrderEnforcer(trailMap, forwardPos.x, forwardPos.y, species);
        float leftWeight = senseMagentaOrderEnforcer(trailMap, leftPos.x, leftPos.y, species);
        float rightWeight = senseMagentaOrderEnforcer(trailMap, rightPos.x, rightPos.y, species);

        applyOrderEnforcerTurning(frontWeight, leftWeight, rightWeight, species);
        return;
    }

    // black/green species: parasitic invader - advanced parasitic behavior
    if (speciesIndex == 5)
    {
        float frontWeight = senseParasiticInvader(trailMap, forwardPos.x, forwardPos.y, species);
        float leftWeight = senseParasiticInvader(trailMap, leftPos.x, leftPos.y, species);
        float rightWeight = senseParasiticInvader(trailMap, rightPos.x, rightPos.y, species);

        applyParasiticHuntingTurning(frontWeight, leftWeight, rightWeight, species);
        return;
    }

    // crimson species: demonic destroyer - pure malevolent destruction
    if (speciesIndex == 6)
    {
        float frontWeight = senseDemonicDestroyer(trailMap, forwardPos.x, forwardPos.y, species);
        float leftWeight = senseDemonicDestroyer(trailMap, leftPos.x, leftPos.y, species);
        float rightWeight = senseDemonicDestroyer(trailMap, rightPos.x, rightPos.y, species);

        applyDemonicDestructionTurning(frontWeight, leftWeight, rightWeight, species);
        return;
    }

    // white species: absolute devourer - monstrous consumption behavior
    if (speciesIndex == 7)
    {
        float frontWeight = senseAbsoluteDevourer(trailMap, forwardPos.x, forwardPos.y, species);
        float leftWeight = senseAbsoluteDevourer(trailMap, leftPos.x, leftPos.y, species);
        float rightWeight = senseAbsoluteDevourer(trailMap, rightPos.x, rightPos.y, species);

        applyDevourerConsumptionTurning(frontWeight, leftWeight, rightWeight, species);
        return;
    }

    // fallback for any other species - use basic multi-species interaction
    float forward = trailMap.sampleSpeciesInteraction(
        static_cast<int>(forwardPos.x), static_cast<int>(forwardPos.y),
        speciesIndex, species.attractionToSelf, species.attractionToOthers);
    float left = trailMap.sampleSpeciesInteraction(
        static_cast<int>(leftPos.x), static_cast<int>(leftPos.y),
        speciesIndex, species.attractionToSelf, species.attractionToOthers);
    float right = trailMap.sampleSpeciesInteraction(
        static_cast<int>(rightPos.x), static_cast<int>(rightPos.y),
        speciesIndex, species.attractionToSelf, species.attractionToOthers);

    // standard turning logic for fallback
    float maxWeight = std::max({forward, left, right});
    if (maxWeight > 0.001f)
    {
        if (forward >= left && forward >= right)
        {
            // continue forward - no turn needed
        }
        else if (left > right)
        {
            angle -= species.turnSpeed * M_PI / 180.0f;
        }
        else
        {
            angle += species.turnSpeed * M_PI / 180.0f;
        }
    }
    else
    {
        // random exploration when no trails detected
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<float> turnDist(-1.0f, 1.0f);
        angle += turnDist(gen) * species.turnSpeed * M_PI / 180.0f * 0.1f;
    }
}

void Agent::deposit(float *chemoattractant, int width, int height, const SimulationSettings &settings)
{
    int x = static_cast<int>(position.x);
    int y = static_cast<int>(position.y);

    if (x >= 0 && x < width && y >= 0 && y < height)
    {
        chemoattractant[y * width + x] += settings.trailWeight;
    }
}

// a simple direct deposit for benchmark mode - creates THICK, visible trails.
// uses a 7x7 deposit pattern with concentric rings of decreasing strength
// so it can create smooth, visible trails that persist long enough for pathfinding comparison.
void Agent::depositBenchmark(TrailMap &trailMap, float strength)
{
    int x = static_cast<int>(position.x);
    int y = static_cast<int>(position.y);
    int w = trailMap.getWidth();
    int h = trailMap.getHeight();
    
    if (x >= 0 && x < w && y >= 0 && y < h)
    {
        // multiplication by 3 for high visibility in benchmark comparisons
        float baseStrength = strength * 3.0f;
        
        // center 3x3: full strength forms the trail core.
        // inner loop iterates dx,dy from -1 to 1 covering the 9 pixels
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                int nx = x + dx;
                int ny = y + dy;
                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    trailMap.deposit(nx, ny, baseStrength, speciesIndex);
                }
            }
        }
        
        // middle ring (5x5 minus center 3x3): 80% strength.
        // continue check skips pixels already covered by center loop.
        for (int dx = -2; dx <= 2; dx++) {
            for (int dy = -2; dy <= 2; dy++) {
                if (std::abs(dx) <= 1 && std::abs(dy) <= 1) continue; // skipping center
                int nx = x + dx;
                int ny = y + dy;
                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    trailMap.deposit(nx, ny, baseStrength * 0.8f, speciesIndex);
                }
            }
        }
        
        // outer ring (7x7 minus 5x5): 50% strength creates smooth edge falloff.
        for (int dx = -3; dx <= 3; dx++) {
            for (int dy = -3; dy <= 3; dy++) {
                if (std::abs(dx) <= 2 && std::abs(dy) <= 2) continue; // skipping inner
                int nx = x + dx;
                int ny = y + dy;
                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    trailMap.deposit(nx, ny, baseStrength * 0.5f, speciesIndex);
                }
            }
        }
    }
}

void Agent::depositMultiSpecies(TrailMap &trailMap, const SimulationSettings &settings)
{
    int x = static_cast<int>(position.x);
    int y = static_cast<int>(position.y);

    if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
    {
        if (speciesIndex >= static_cast<int>(settings.speciesSettings.size()))
            return;

        const auto &species = settings.speciesSettings[speciesIndex];

        // enhanced trail deposition with patterns like c# version I found.
        float baseTrailStrength = settings.trailWeight;

        // species identification by both color and index.
        // using color allows behavior to persist after species rerolls (when indices change),
        // while index checks handle species with ambiguous colors.
        const sf::Color &color = species.color;
        bool isRedSpecies = (color.r > 200 && color.g < 150 && color.b < 150);
        bool isBlueSpecies = (color.r < 150 && color.g > 100 && color.g < 200 && color.b > 200);
        bool isGreenSpecies = (color.r < 150 && color.g > 200 && color.b < 150);
        bool isYellowSpecies = (color.r > 200 && color.g > 200 && color.b < 150);
        bool isMagentaSpecies = (color.r > 200 && color.g < 150 && color.b > 200);
        bool isBlackParasite = (speciesIndex == 5 || (color.r < 60 && color.g < 60 && color.b < 60));
        bool isCrimsonDeath = (speciesIndex == 6 || (color.r > 100 && color.g < 50 && color.b < 50));
        bool isWhiteGuardian = (speciesIndex == 7 || (color.r > 200 && color.g > 200 && color.b > 200));

        // each species has a unique deposition pattern that reflects its personality:
        // parasitic = infectious spread, death = destruction, guardian = protection,
        // red = thin predator trails, blue = thick food network, green = sparse loner trails,
        // yellow = chaotic alien patterns, magenta = geometric radial patterns.
        if (isBlackParasite)
        {
            depositParasiticPattern(trailMap, x, y, baseTrailStrength, settings);
        }
        else if (isCrimsonDeath)
        {
            depositDestructivePattern(trailMap, x, y, baseTrailStrength * 2.5f);
        }
        else if (isWhiteGuardian)
        {
            depositProtectivePattern(trailMap, x, y, baseTrailStrength * 1.3f);
        }
        else if (isRedSpecies)
        {
            // red species: predators deposit thin trails, relying on hunting others' trails.
            depositThickTrail(trailMap, x, y, baseTrailStrength * 0.7f, 1);
        }
        else if (isBlueSpecies)
        {
            // blue species: farmers create thick network trails to feed the ecosystem.
            depositNetworkPattern(trailMap, x, y, baseTrailStrength * 2.5f);
        }
        else if (isGreenSpecies)
        {
            // green species: loners create thin, scattered patterns for efficiency.
            depositSegmentedPattern(trailMap, x, y, baseTrailStrength * 0.6f);
        }
        else if (isYellowSpecies)
        {
            depositAlienPattern(trailMap, x, y, baseTrailStrength, settings);
        }
        else if (isMagentaSpecies)
        {
            depositRadialPattern(trailMap, x, y, baseTrailStrength * 1.5f);
        }
        else
        {
            // default: simple single point deposition
            trailMap.deposit(x, y, baseTrailStrength, speciesIndex);
        }
    }
}

// helper method for thick arterial trails (red species).
// creates a circular deposit with gaussian falloff - strongest at center, fading smoothly at edges.
// radius controls the size of the deposit footprint.
void Agent::depositThickTrail(TrailMap &trailMap, int centerX, int centerY, float strength, int radius)
{
    // iterate over a square region centered on (centerX, centerY)
    for (int dy = -radius; dy <= radius; ++dy)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            int x = centerX + dx;
            int y = centerY + dy;

            if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
            {
                // only deposit within circular radius (skipping corners of square)
                float distance = std::sqrt(dx * dx + dy * dy);
                if (distance <= radius)
                {
                    // gaussian falloff: e^(-d^2/r^2) gives smooth decay from center.
                    // center gets full strength, edge gets ~37%ish strength.
                    float falloff = std::exp(-distance * distance / (radius * radius));
                    trailMap.deposit(x, y, strength * falloff, speciesIndex);
                }
            }
        }
    }
}

// helper method for fine network patterns (blue species).
// creates a mesh like pattern by depositing along 8 directions (cardinal + diagonal).
// this builds the connected trail network that makes blue species the ecosystem's "farmers".
void Agent::depositNetworkPattern(TrailMap &trailMap, int centerX, int centerY, float strength)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> networkRand(0.0f, 1.0f);

    // main deposition at center
    trailMap.deposit(centerX, centerY, strength, speciesIndex);

    // extend connections in 8 directions: N, NE, E, SE, S, SW, W, NW.
    // each direction creates a short trail segment to build network connectivity.
    for (int i = 0; i < 8; ++i)
    {
        float angle = i * M_PI / 4.0f;
        for (int dist = 1; dist <= 3; ++dist)
        {
            int x = centerX + static_cast<int>(std::cos(angle) * dist);
            int y = centerY + static_cast<int>(std::sin(angle) * dist);

            if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
            {
                float connectionStrength = strength * (0.3f + networkRand(gen) * 0.4f) / dist;
                trailMap.deposit(x, y, connectionStrength, speciesIndex);
            }
        }
    }
}

// helper method for segmented patterns (green species).
// creates gaps in the trail by skipping deposits during certain phase windows.
// this produces a dashed/segmented look that reflects green's sparse, loner personality.
void Agent::depositSegmentedPattern(TrailMap &trailMap, int centerX, int centerY, float strength)
{
    // per agent phase counter stored in static map
    static std::unordered_map<size_t, int> segmentPhases;
    size_t agentId = reinterpret_cast<size_t>(this);

    if (segmentPhases.find(agentId) == segmentPhases.end())
    {
        segmentPhases[agentId] = 0;
    }

    // phase cycles 0-19, wrapping back to 0
    segmentPhases[agentId] = (segmentPhases[agentId] + 1) % 20;

    // only deposit during phases 0-14; phases 15-19 are gaps.
    // this creates a 75% duty cycle trail (deposits 15 frames, skips 5 frames).
    if (segmentPhases[agentId] < 15)
    {
        // main deposit at center, slightly boosted
        trailMap.deposit(centerX, centerY, strength * 1.2f, speciesIndex);

        // thin border around center at 40% strength for subtle thickness
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0)
                    continue; // skip center... already deposited

                int x = centerX + dx;
                int y = centerY + dy;

                if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
                {
                    trailMap.deposit(x, y, strength * 0.4f, speciesIndex);
                }
            }
        }
    }
}

// helper method for radial patterns (magenta species).
// creates concentric rings around the agent that pulse with phase.
// the geometric ordered pattern reflects magenta's anti chaos personality.
void Agent::depositRadialPattern(TrailMap &trailMap, int centerX, int centerY, float strength)
{
    // per agent phase counter for ring pulsing
    static std::unordered_map<size_t, float> radialPhases;
    size_t agentId = reinterpret_cast<size_t>(this);

    if (radialPhases.find(agentId) == radialPhases.end())
    {
        radialPhases[agentId] = 0.0f;
    }

    radialPhases[agentId] += 0.1f;

    // create concentric rings from radius 1 to 4.
    // each ring's strength varies with sin(phase + offset), creating a pulsing effect.
    int maxRadius = 4;
    for (int radius = 1; radius <= maxRadius; ++radius)
    {
        // sin wave creates pulsing; radius offset staggers the pulses between rings
        float ringStrength = strength * std::sin(radialPhases[agentId] + radius * 0.5f) * 0.3f;
        if (ringStrength > 0)
        {
            // more sample points for larger radii to maintain smooth circles
            int points = radius * 8;
            for (int i = 0; i < points; ++i)
            {
                float angle = (2.0f * M_PI * i) / points;
                int x = centerX + static_cast<int>(std::cos(angle) * radius);
                int y = centerY + static_cast<int>(std::sin(angle) * radius);

                if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
                {
                    trailMap.deposit(x, y, ringStrength, speciesIndex);
                }
            }
        }
    }

    // strong center point anchors the pattern
    trailMap.deposit(centerX, centerY, strength * 0.8f, speciesIndex);
}

// helper method for alien patterns (yellow species).
// cycles through 5 bizarre deposit modes that defy normal slime logic:
// quantum tunneling, reality tears, phase shifting, dimensional bleed, and chaos.
void Agent::depositAlienPattern(TrailMap &trailMap, int centerX, int centerY, float strength, const SimulationSettings &settings)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> alienRand(0.0f, 1.0f);
    // per agent alien state: modes select which pattern to use, counters track frames
    static std::unordered_map<size_t, int> alienModes;
    static std::unordered_map<size_t, int> alienCounters;

    size_t agentId = reinterpret_cast<size_t>(this);

    if (alienModes.find(agentId) == alienModes.end())
    {
        alienModes[agentId] = 0;
        alienCounters[agentId] = 0;
    }

    alienCounters[agentId]++;

    // use settings to modulate alien behavior intensity
    float chaosLevel = speciesIndex < (int)settings.speciesSettings.size() ? settings.speciesSettings[speciesIndex].behaviorIntensity * 0.1f : 0.2f;

    // occasionally switch modes based on chaos level
    if (alienRand(gen) < chaosLevel * 0.05f)
    {
        alienModes[agentId] = static_cast<int>(alienRand(gen) * 5);
    }

    switch (alienModes[agentId])
    {
    case 0: // quantum tunneling - skip some depositions
        if (alienRand(gen) > 0.7f)
        {
            trailMap.deposit(centerX, centerY, strength * 2.5f, speciesIndex);
        }
        break;

    case 1: // reality tears - create strange patterns
        for (int i = 0; i < 5; ++i)
        {
            int x = centerX + static_cast<int>(alienRand(gen) * 10 - 5);
            int y = centerY + static_cast<int>(alienRand(gen) * 10 - 5);
            if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
            {
                trailMap.deposit(x, y, strength * alienRand(gen), speciesIndex);
            }
        }
        break;

    case 2: // phase shifting - periodic intense bursts
        if ((alienCounters[agentId] % 10) < 3)
        {
            depositThickTrail(trailMap, centerX, centerY, strength * 3.0f, 3);
        }
        break;

    case 3: // dimensional bleed - creates cross patterns
        for (int i = -2; i <= 2; ++i)
        {
            // horizontal line
            if (centerX + i >= 0 && centerX + i < trailMap.getWidth())
                trailMap.deposit(centerX + i, centerY, strength * 0.8f, speciesIndex);
            // vertical line
            if (centerY + i >= 0 && centerY + i < trailMap.getHeight())
                trailMap.deposit(centerX, centerY + i, strength * 0.8f, speciesIndex);
        }
        break;

    default: // chaos mode - completely random
        depositNetworkPattern(trailMap, centerX, centerY, strength * alienRand(gen) * 2.0f);
        break;
    }
}

// helper method for parasitic patterns (black parasitic nightmare)
void Agent::depositParasiticPattern(TrailMap &trailMap, int centerX, int centerY, float strength, const SimulationSettings &settings)
{
    // parasitic pattern: spreads like infection, corrupts other species trails
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> parasiticRand(0.0f, 1.0f);
    static std::unordered_map<size_t, int> parasiteStates; // per agent parasitic state

    size_t agentId = reinterpret_cast<size_t>(this);

    // initialize parasitic state if not exists
    if (parasiteStates.find(agentId) == parasiteStates.end())
    {
        parasiteStates[agentId] = static_cast<int>(parasiticRand(gen) * 4);
    }

    int state = parasiteStates[agentId];

    switch (state)
    {
    case 0: // stealth mode - minimal deposition, infiltration
    {
        // deposit minimal trail with white outline effect
        trailMap.deposit(centerX, centerY, strength * 0.3f, speciesIndex);

        // add white outline by depositing in adjacent cells
        for (int dy = -1; dy <= 1; dy += 2)
        {
            for (int dx = -1; dx <= 1; dx += 2)
            {
                int x = centerX + dx;
                int y = centerY + dy;
                if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
                {
                    trailMap.deposit(x, y, strength * 0.8f, speciesIndex); // white outline effect
                }
            }
        }
        break;
    }

    case 1: // infiltration mode - spreading tendrils
    {
        // create spreading tentacle-like patterns
        for (int i = 0; i < 6; ++i)
        {
            float angle = (i * M_PI / 3.0f) + parasiticRand(gen) * 0.5f;
            int length = 2 + static_cast<int>(parasiticRand(gen) * 4);

            for (int j = 1; j <= length; ++j)
            {
                int x = centerX + static_cast<int>(std::cos(angle) * j);
                int y = centerY + static_cast<int>(std::sin(angle) * j);

                if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
                {
                    float falloff = 1.0f - (j / static_cast<float>(length));
                    trailMap.deposit(x, y, strength * falloff * 0.6f, speciesIndex);
                }
            }
        }
        break;
    }

    case 2: // burst mode - explosive spread
    {
        // explosive circular burst pattern
        int burstRadius = 4;
        for (int dy = -burstRadius; dy <= burstRadius; ++dy)
        {
            for (int dx = -burstRadius; dx <= burstRadius; ++dx)
            {
                int x = centerX + dx;
                int y = centerY + dy;

                if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
                {
                    float distance = std::sqrt(dx * dx + dy * dy);
                    if (distance <= burstRadius)
                    {
                        float falloff = 1.0f - (distance / burstRadius);
                        trailMap.deposit(x, y, strength * falloff * 1.5f, speciesIndex);
                    }
                }
            }
        }
        break;
    }

    case 3: // consumption mode - absorb other species' trails
    {
        // standard deposition but stronger
        trailMap.deposit(centerX, centerY, strength * 1.2f, speciesIndex);

        // try to "consume" nearby trails from other species
        for (int dy = -2; dy <= 2; ++dy)
        {
            for (int dx = -2; dx <= 2; ++dx)
            {
                int x = centerX + dx;
                int y = centerY + dy;

                if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
                {
                    // sample other species and add to our own trail
                    for (int i = 0; i < trailMap.getNumSpecies(); ++i)
                    {
                        if (i != speciesIndex)
                        {
                            float otherTrail = trailMap.sample(x, y, i);
                            if (otherTrail > 0.1f)
                            {
                                trailMap.deposit(x, y, otherTrail * 0.3f, speciesIndex); // convert their trail to ours
                            }
                        }
                    }
                }
            }
        }
        break;
    }
    }

    // occasionally switch parasitic state
    if (parasiticRand(gen) < 0.02f)
    {
        parasiteStates[agentId] = static_cast<int>(parasiticRand(gen) * 4);
    }
}

// helper method for destructive patterns (crimson death bringer)
void Agent::depositDestructivePattern(TrailMap &trailMap, int centerX, int centerY, float strength)
{
    // destructive pattern: sharp, aggressive, overwrites other species
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> deathRand(0.0f, 1.0f);
    static std::unordered_map<size_t, int> deathModes; // per agent destruction mode

    size_t agentId = reinterpret_cast<size_t>(this);

    // initialize death mode if not exists
    if (deathModes.find(agentId) == deathModes.end())
    {
        deathModes[agentId] = static_cast<int>(deathRand(gen) * 3);
    }

    int mode = deathModes[agentId];

    switch (mode)
    {
    case 0: // annihilation mode - high intensity center with destruction radius
    {
        // extremely strong center point
        trailMap.deposit(centerX, centerY, strength * 3.0f, speciesIndex);

        // destructive cross pattern
        for (int i = 1; i <= 3; ++i)
        {
            // horizontal destruction
            int leftX = centerX - i, rightX = centerX + i;
            int upY = centerY - i, downY = centerY + i;

            if (leftX >= 0)
                trailMap.deposit(leftX, centerY, strength * (1.5f - i * 0.3f), speciesIndex);
            if (rightX < trailMap.getWidth())
                trailMap.deposit(rightX, centerY, strength * (1.5f - i * 0.3f), speciesIndex);
            if (upY >= 0)
                trailMap.deposit(centerX, upY, strength * (1.5f - i * 0.3f), speciesIndex);
            if (downY < trailMap.getHeight())
                trailMap.deposit(centerX, downY, strength * (1.5f - i * 0.3f), speciesIndex);
        }
        break;
    }

    case 1: // berserker mode - chaotic destructive pattern
    {
        // random explosive pattern
        int numPoints = 8 + static_cast<int>(deathRand(gen) * 6);
        for (int i = 0; i < numPoints; ++i)
        {
            float angle = deathRand(gen) * 2.0f * M_PI;
            int distance = 1 + static_cast<int>(deathRand(gen) * 4);

            int x = centerX + static_cast<int>(std::cos(angle) * distance);
            int y = centerY + static_cast<int>(std::sin(angle) * distance);

            if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
            {
                trailMap.deposit(x, y, strength * (1.5f + deathRand(gen) * 1.0f), speciesIndex);
            }
        }
        break;
    }

    case 2: // spiral of death - creates deadly spiral patterns
    {
        // spiral death pattern
        float spiralRadius = 4.0f;
        int numSpokes = 6;

        for (int spoke = 0; spoke < numSpokes; ++spoke)
        {
            float baseAngle = (spoke * 2.0f * M_PI) / numSpokes;

            for (int i = 1; i <= 4; ++i)
            {
                float angle = baseAngle + (i * 0.3f); // spiral twist
                int x = centerX + static_cast<int>(std::cos(angle) * i);
                int y = centerY + static_cast<int>(std::sin(angle) * i);

                if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
                {
                    float intensity = 1.5f - (i * 0.2f);
                    trailMap.deposit(x, y, strength * intensity, speciesIndex);
                }
            }
        }
        break;
    }
    }

    // frequently switch destruction modes for maximum chaos
    if (deathRand(gen) < 0.05f)
    {
        deathModes[agentId] = static_cast<int>(deathRand(gen) * 3);
    }
}

// helper method for protective patterns (white guardian angel)
void Agent::depositProtectivePattern(TrailMap &trailMap, int centerX, int centerY, float strength)
{
    // protective pattern: nurturing, strengthening, creates safe zones
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> guardianRand(0.0f, 1.0f);
    static std::unordered_map<size_t, int> guardianModes; // per agent protection mode

    size_t agentId = reinterpret_cast<size_t>(this);

    // initialize guardian mode if not exists
    if (guardianModes.find(agentId) == guardianModes.end())
    {
        guardianModes[agentId] = static_cast<int>(guardianRand(gen) * 3);
    }

    int mode = guardianModes[agentId];

    switch (mode)
    {
    case 0: // sanctuary mode - creates safe circular zones
    {
        // gentle circular protective area
        int sanctuaryRadius = 3;
        for (int dy = -sanctuaryRadius; dy <= sanctuaryRadius; ++dy)
        {
            for (int dx = -sanctuaryRadius; dx <= sanctuaryRadius; ++dx)
            {
                int x = centerX + dx;
                int y = centerY + dy;

                if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
                {
                    float distance = std::sqrt(dx * dx + dy * dy);
                    if (distance <= sanctuaryRadius)
                    {
                        float protective = 1.0f - (distance / sanctuaryRadius);
                        trailMap.deposit(x, y, strength * protective * 0.8f, speciesIndex);

                        // strengthen any existing beneficial trails from other species
                        for (int i = 0; i < std::min(5, trailMap.getNumSpecies()); ++i) // only strengthen original 5 species
                        {
                            if (i != speciesIndex)
                            {
                                float existingTrail = trailMap.sample(x, y, i);
                                if (existingTrail > 0.1f)
                                {
                                    trailMap.deposit(x, y, existingTrail * 0.2f, i); // boost their trail
                                }
                            }
                        }
                    }
                }
            }
        }
        break;
    }

    case 1: // nurturing mode - flowing, organic patterns
    {
        // create flowing, nurturing patterns like healing energy
        float nurtureRadius = 2.5f;
        int numFlows = 8;

        for (int flow = 0; flow < numFlows; ++flow)
        {
            float angle = (flow * 2.0f * M_PI) / numFlows;

            for (int i = 1; i <= 3; ++i)
            {
                float x = centerX + std::cos(angle) * i * 0.8f;
                float y = centerY + std::sin(angle) * i * 0.8f;

                int pixelX = static_cast<int>(x);
                int pixelY = static_cast<int>(y);

                if (pixelX >= 0 && pixelX < trailMap.getWidth() && pixelY >= 0 && pixelY < trailMap.getHeight())
                {
                    float intensity = 1.0f - (i * 0.25f);
                    trailMap.deposit(pixelX, pixelY, strength * intensity * 0.7f, speciesIndex);
                }
            }
        }
        break;
    }

    case 2: // barrier mode - creates protective walls and barriers
    {
        // create protective barrier patterns
        trailMap.deposit(centerX, centerY, strength * 1.1f, speciesIndex);

        // create protective cross pattern
        for (int i = 1; i <= 2; ++i)
        {
            // horizontal barrier
            int leftX = centerX - i, rightX = centerX + i;
            int upY = centerY - i, downY = centerY + i;

            if (leftX >= 0)
                trailMap.deposit(leftX, centerY, strength * 0.7f, speciesIndex);
            if (rightX < trailMap.getWidth())
                trailMap.deposit(rightX, centerY, strength * 0.7f, speciesIndex);
            if (upY >= 0)
                trailMap.deposit(centerX, upY, strength * 0.7f, speciesIndex);
            if (downY < trailMap.getHeight())
                trailMap.deposit(centerX, downY, strength * 0.7f, speciesIndex);
        }

        // diagonal reinforcement
        for (int dx = -1; dx <= 1; dx += 2)
        {
            for (int dy = -1; dy <= 1; dy += 2)
            {
                int x = centerX + dx;
                int y = centerY + dy;

                if (x >= 0 && x < trailMap.getWidth() && y >= 0 && y < trailMap.getHeight())
                {
                    trailMap.deposit(x, y, strength * 0.5f, speciesIndex);
                }
            }
        }
        break;
    }
    }

    // gradual mode switching for stable protection
    if (guardianRand(gen) < 0.015f)
    {
        guardianModes[agentId] = static_cast<int>(guardianRand(gen) * 3);
    }
}

void Agent::wrapPosition(int width, int height)
{
    if (position.x < 0)
        position.x += width;
    if (position.x >= width)
        position.x -= width;
    if (position.y < 0)
        position.y += height;
    if (position.y >= height)
        position.y -= height;
}


// the path following methods for algorithm "race" mode
void Agent::setPath(const std::vector<GridCell>& path, SimulationSettings::Algos algo) {
    currentPath = path;
    pathIndex = 0;
    hasPath = !path.empty();
    reachedGoal = false;
    assignedAlgo = algo;
}

void Agent::clearPath() {
    currentPath.clear();
    pathIndex = 0;
    hasPath = false;
    reachedGoal = false;
}

bool Agent::followPath(const Pathfinder& pathfinder, float raceMoveSpeed, float goalRadius) {
    if (!hasPath || currentPath.empty() || reachedGoal) {
        return reachedGoal;
    }
    
    // gets current target waypoint
    if (pathIndex >= currentPath.size()) {
        reachedGoal = true;
        return true;
    }
    
    const GridCell& target = currentPath[pathIndex];
    auto [targetX, targetY] = pathfinder.gridToWorld(target);
    
    // calculates direction to target
    float dx = targetX - position.x;
    float dy = targetY - position.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    // and check if we've reached the current waypoint
    float waypointRadius = static_cast<float>(pathfinder.getCellSize()) * 0.5f;
    if (distance < waypointRadius) {
        pathIndex++;
        
        // then check if we've reached the end
        if (pathIndex >= currentPath.size()) {
            reachedGoal = true;
            return true;
        }
        
        // and update target to next waypoint
        const GridCell& nextTarget = currentPath[pathIndex];
        auto [nextX, nextY] = pathfinder.gridToWorld(nextTarget);
        dx = nextX - position.x;
        dy = nextY - position.y;
        distance = std::sqrt(dx * dx + dy * dy);
    }
    
    // move toward target
    if (distance > 0.1f) {
        float dirX = dx / distance;
        float dirY = dy / distance;
        
        // update angle to face movement direction
        angle = std::atan2(dirY, dirX);
        
        // move
        float step = std::min(raceMoveSpeed, distance);
        position.x += dirX * step;
        position.y += dirY * step;
        
        // CLAMP to actual screen bounds (no wrapping in benchmark mode)
        float maxX = static_cast<float>(pathfinder.getWorldWidth() - 5);
        float maxY = static_cast<float>(pathfinder.getWorldHeight() - 5);
        position.x = std::clamp(position.x, 5.0f, maxX);
        position.y = std::clamp(position.y, 5.0f, maxY);
        
        // update for velocity for trail rendering
        velocity.x = dirX * step;
        velocity.y = dirY * step;
    }
    
    // final goal check (last waypoint with goal radius)
    if (pathIndex == currentPath.size() - 1) {
        const GridCell& finalTarget = currentPath.back();
        auto [finalX, finalY] = pathfinder.gridToWorld(finalTarget);
        float finalDist = std::sqrt(
            (position.x - finalX) * (position.x - finalX) +
            (position.y - finalY) * (position.y - finalY)
        );
        if (finalDist < goalRadius) {
            reachedGoal = true;
            return true;
        }
    }
    
    // A FINAL SAFETY NET: absolute hard clamp to world bounds
    float finalMaxX = static_cast<float>(pathfinder.getWorldWidth() - 1);
    float finalMaxY = static_cast<float>(pathfinder.getWorldHeight() - 1);
    position.x = std::clamp(position.x, 1.0f, finalMaxX);
    position.y = std::clamp(position.y, 1.0f, finalMaxY);
    
    return false;
}

float Agent::getPathProgress() const {
    if (!hasPath || currentPath.empty()) return 0.0f;
    return static_cast<float>(pathIndex) / static_cast<float>(currentPath.size());
}


// TRUE BLIND EXPLORATION (ignorance/naive agents) - agents explore without knowing goal location
void Agent::initExploration(const Pathfinder& pathfinder, SimulationSettings::Algos algo) {
    assignedAlgo = algo;
    isExploring = true;
    reachedGoal = false;
    hasPath = false;
    pathIndex = 0;
    
    // initialize current cell from position
    currentCell = pathfinder.worldToGrid(position.x, position.y);
    
    // clearing exploration state
    explorationFrontier.clear();
    visitedCells.clear();
    explorationParents.clear();
    currentPath.clear();
    
    // start with current cell
    visitedCells.insert(currentCell);
    
    // add the neighbors to frontier based on algorithm
    auto neighbors = pathfinder.getNeighbors(currentCell);
    
    if (algo == SimulationSettings::Algos::DFS) {
        // shuffle for that "burning wool" effect 
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(neighbors.begin(), neighbors.end(), g);
        
        for (const auto& next : neighbors) {
            // here dont mark as visited yet only want to mark when we actually arrive at the cell
            // just add to frontier (duplicates will be filtered when popped)
            explorationParents[next] = currentCell;
            explorationFrontier.push_back(next);
        }
    } else if (algo == SimulationSettings::Algos::Dijkstra) {
        // dijkstra: initialize cost map and add neighbors with costs
        explorationCosts.clear();
        explorationCosts[currentCell] = 0.0f;
        
        const float CARDINAL_COST = 1.0f;
        const float DIAGONAL_COST = 1.41421356f;
        
        for (const auto& next : neighbors) {
            int dx = std::abs(next.x - currentCell.x);
            int dy = std::abs(next.y - currentCell.y);
            bool isDiagonal = (dx == 1 && dy == 1);
            float cost = isDiagonal ? DIAGONAL_COST : CARDINAL_COST;
            
            explorationCosts[next] = cost;
            explorationParents[next] = currentCell;
            explorationFrontier.push_back(next);
        }
    }
    
    // no movement target yet
    explorationTargetX = -1;
    explorationTargetY = -1;
}

bool Agent::exploreStep(const Pathfinder& pathfinder, const GridCell& goalCell, float moveSpeed, SharedExplorationState* sharedState) {
    if (!isExploring || reachedGoal) {
        return reachedGoal;
    }
    
    // decision for which exploration state to use
    std::deque<GridCell>* frontierPtr = sharedState ? &sharedState->frontier : nullptr;
    std::unordered_set<GridCell, GridCellHash>* visitedPtr = sharedState ? &sharedState->visited : &visitedCells;
    std::unordered_map<GridCell, GridCell, GridCellHash>* parentsPtr = sharedState ? &sharedState->parents : &explorationParents;
    
    // if we're using shared state and it already found the goal then we're done
    if (sharedState && sharedState->foundGoal) {
        reachedGoal = true;
        isExploring = false;
        return true;
    }
    
    // check if we've discovered the goal
    GridCell posCell = pathfinder.worldToGrid(position.x, position.y);
    if (posCell == goalCell) {
        reachedGoal = true;
        isExploring = false;
        if (sharedState) {
            sharedState->foundGoal = true;
        }
        std::cout << "EXPLORER FOUND GOAL! Agent " << agentId << " (" << SimulationSettings::algoNames(assignedAlgo) << ")" << std::endl;
        return true;
    }
    
    // movement speed for explorers
    float exploreSpeed = moveSpeed * 12.0f;  
    
    // if we dont have a target yet then it just picks one from frontier
    if (explorationTargetX < 0 || explorationTargetY < 0) {
        // use private frontier if no shared state
        if (!sharedState && explorationFrontier.empty()) {
            return false;
        }
        // If shared frontier is empty, DON'T give up (return true to stay alive/waiting)
        // unless the goal is found (handled at top)
        if (sharedState && frontierPtr->empty()) {
            return true; // Wait for work
        }
        
        GridCell nextCell;
        bool foundUnvisited = false;
        
        // picking next unvisited cell from frontier (shared or private)
        auto& frontier = sharedState ? *frontierPtr : explorationFrontier;
        
        if (assignedAlgo == SimulationSettings::Algos::DFS) {
            // DFS: lifo (stack) classic take from back, skip already visited
            while (!frontier.empty() && !foundUnvisited) {
                nextCell = frontier.back();
                frontier.pop_back();
                if (visitedPtr->find(nextCell) == visitedPtr->end()) {
                    foundUnvisited = true;
                }
            }
        } else {
            // default/bfs/bidirectional: the first in first out (ie queue) - take from front
            if (!frontier.empty()) {
                nextCell = frontier.front();
                frontier.pop_front();
                foundUnvisited = true;
            }
        }
        
        if (!foundUnvisited) {
            if (sharedState) return true; // wait for more work
            return false;  // private frontier empty = done
        }
        
        auto [worldX, worldY] = pathfinder.gridToWorld(nextCell);
        // clamping target to visible bounds (grid can extend past visible area)
        float maxX = static_cast<float>(pathfinder.getWorldWidth() - 5);
        float maxY = static_cast<float>(pathfinder.getWorldHeight() - 5);
        explorationTargetX = std::clamp(worldX, 5.0f, maxX);
        explorationTargetY = std::clamp(worldY, 5.0f, maxY);
    }
    
    // move toward current target
    float dx = explorationTargetX - position.x;
    float dy = explorationTargetY - position.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    
    if (dist > 0.5f) {
        float dirX = dx / dist;
        float dirY = dy / dist;
        
        angle = std::atan2(dirY, dirX);
        
        float stepDist = std::min(exploreSpeed, dist);
        float newX = position.x + dirX * stepDist;
        float newY = position.y + dirY * stepDist;
        
        // checks if new position would be inside an obstacle
        GridCell newCell = pathfinder.worldToGrid(newX, newY);
        if (pathfinder.isBlocked(newCell)) {
            // hit a wall! so clear target and pick a new one next frame
            explorationTargetX = -1;
            explorationTargetY = -1;
        } else {
            position.x = newX;
            position.y = newY;
            
            // clamp to actual screen bounds (the world dimensions)
            float maxX = static_cast<float>(pathfinder.getWorldWidth() - 5);
            float maxY = static_cast<float>(pathfinder.getWorldHeight() - 5);
            position.x = std::clamp(position.x, 5.0f, maxX);
            position.y = std::clamp(position.y, 5.0f, maxY);
        }
    }
    
    // check for arrived at target
    float arrivalDist = static_cast<float>(pathfinder.getCellSize()) * 0.4f;
    if (dist <= arrivalDist) {
        // arrived! so then check if its the goal
        GridCell targetCell = pathfinder.worldToGrid(explorationTargetX, explorationTargetY);
        if (targetCell == goalCell) {
            reachedGoal = true;
            isExploring = false;
            if (sharedState) {
                sharedState->foundGoal = true;
            }
            std::cout << "EXPLORER FOUND GOAL! Agent " << agentId << " (" << SimulationSettings::algoNames(assignedAlgo) << ")" << std::endl;
            return true;
        }
        
        // neighbors to frontier
        // Note: for shared state we already marked visited when we pulled from frontier above
        // so we always expand neighbors once we arrive.
        bool shouldExpand = true;
        if (!sharedState) {
            // private state: expanding once per cell
            if (visitedPtr->find(targetCell) == visitedPtr->end()) {
                visitedPtr->insert(targetCell);
            } else {
                shouldExpand = false;
            }
        }

        if (shouldExpand) {
            currentCell = targetCell;

            auto neighbors = pathfinder.getNeighbors(targetCell);
            
            if (assignedAlgo == SimulationSettings::Algos::DFS) {
                std::random_device rd;
                std::mt19937 g(rd());
                std::shuffle(neighbors.begin(), neighbors.end(), g);
            }

            auto& frontier = sharedState ? *frontierPtr : explorationFrontier;

            for (const auto& neighbor : neighbors) {
                if (visitedPtr->find(neighbor) == visitedPtr->end()) {
                    // for DFS: just add to frontier, duplicates filtered when popped
                    // for others: check if already in frontier
                    if (assignedAlgo == SimulationSettings::Algos::DFS || sharedState) {
                        frontier.push_back(neighbor);
                        (*parentsPtr)[neighbor] = targetCell;
                    } else {
                        // private state: check if already in frontier (old behavior)
                        bool inFrontier = false;
                        for (const auto& f : frontier) {
                            if (f == neighbor) { inFrontier = true; break; }
                        }
                        if (!inFrontier) {
                            frontier.push_back(neighbor);
                            (*parentsPtr)[neighbor] = targetCell;
                        }
                    }
                }
            }
        }
        
        // clears target so we pick a new one next frame
        explorationTargetX = -1;
        explorationTargetY = -1;
    }
    
    float finalMaxX = static_cast<float>(pathfinder.getWorldWidth() - 1);
    float finalMaxY = static_cast<float>(pathfinder.getWorldHeight() - 1);
    if (position.x < 0 || position.x > finalMaxX || position.y < 0 || position.y > finalMaxY) {
        std::cout << "BOUNDS FIX: Agent " << agentId << " was at (" << position.x << "," << position.y << ")" << std::endl;
    }
    position.x = std::clamp(position.x, 1.0f, finalMaxX);
    position.y = std::clamp(position.y, 1.0f, finalMaxY);
    
    return false;
}


// classic dijkstra exploration - priority queue by shortest distance from start
// creating an octagonal wavefront because diagonal moves cost sqrt(2) > 1.
// unlike BFS which explores in equal "hops" dijkstra just explores by actual distance traveled.

bool Agent::exploreStepDijkstra(const Pathfinder& pathfinder, const GridCell& goalCell, float moveSpeed) {
    if (!isExploring || reachedGoal) {
        return reachedGoal;
    }
    
    // cost constants: diagonal moves are ~1.41x more expensive than cardinal moves.
    // this creates the characteristic octagonal wavefront of the dijkstra movement on a grid
    const float CARDINAL_COST = 1.0f;
    const float DIAGONAL_COST = 1.41421356f;  // sqrt(2)
    
    // check if we've discovered the goal at current position
    GridCell posCell = pathfinder.worldToGrid(position.x, position.y);
    if (posCell == goalCell) {
        reachedGoal = true;
        isExploring = false;
        std::cout << "DIJKSTRA FOUND GOAL! Agent " << agentId << std::endl;
        return true;
    }
    
    // movement speed for explorers
    float exploreSpeed = moveSpeed * 12.0f;
    
    // if we dont have a target yet, pick at the lowest cost cell from frontier
    // this is the key insight of dijkstra: always expand the node with minimum g cost.
    // unlike a priority queue we're doing linear search here (fine for small frontiers in this context kinda sucks)
    if (explorationTargetX < 0 || explorationTargetY < 0) {
        if (explorationFrontier.empty()) {
            return false;
        }
        
        // linear scan to find the cell with minimum cost from start
        auto minIt = explorationFrontier.begin();
        float minCost = std::numeric_limits<float>::max();
        
        for (auto it = explorationFrontier.begin(); it != explorationFrontier.end(); ++it) {
            float cost = explorationCosts.count(*it) ? explorationCosts[*it] : std::numeric_limits<float>::max();
            if (cost < minCost) {
                minCost = cost;
                minIt = it;
            }
        }
        
        GridCell nextCell = *minIt;
        explorationFrontier.erase(minIt);
        
        // skip if already visited (can happen with deque based frontier)
        if (visitedCells.count(nextCell)) {
            return false;  // and try again next frame
        }
        
        auto [worldX, worldY] = pathfinder.gridToWorld(nextCell);
        float maxX = static_cast<float>(pathfinder.getWorldWidth() - 5);
        float maxY = static_cast<float>(pathfinder.getWorldHeight() - 5);
        explorationTargetX = std::clamp(worldX, 5.0f, maxX);
        explorationTargetY = std::clamp(worldY, 5.0f, maxY);
    }
    
    // moving toward current target
    float dx = explorationTargetX - position.x;
    float dy = explorationTargetY - position.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    
    if (dist > 0.5f) {
        float dirX = dx / dist;
        float dirY = dy / dist;
        
        angle = std::atan2(dirY, dirX);
        
        float stepDist = std::min(exploreSpeed, dist);
        float newX = position.x + dirX * stepDist;
        float newY = position.y + dirY * stepDist;
        
        GridCell newCell = pathfinder.worldToGrid(newX, newY);
        if (pathfinder.isBlocked(newCell)) {
            explorationTargetX = -1;
            explorationTargetY = -1;
        } else {
            position.x = newX;
            position.y = newY;
            
            float maxX = static_cast<float>(pathfinder.getWorldWidth() - 5);
            float maxY = static_cast<float>(pathfinder.getWorldHeight() - 5);
            position.x = std::clamp(position.x, 5.0f, maxX);
            position.y = std::clamp(position.y, 5.0f, maxY);
        }
    }
    
    // check if arrived at target
    float arrivalDist = static_cast<float>(pathfinder.getCellSize()) * 0.4f;
    if (dist <= arrivalDist) {
        GridCell targetCell = pathfinder.worldToGrid(explorationTargetX, explorationTargetY);
        
        if (targetCell == goalCell) {
            reachedGoal = true;
            isExploring = false;
            std::cout << "WOW!!! DIJKSTRA FOUND GOAL! Agent " << agentId << std::endl;
            return true;
        }
        
        // mark cell as visited and expand to neighbors with weighted costs.
        // this is the "relaxation" step of dijkstra: update neighbor costs if we found a shorter path.
        if (visitedCells.find(targetCell) == visitedCells.end()) {
            visitedCells.insert(targetCell);
            currentCell = targetCell;
            
            // get the cost to reach this cell (established when it was added to frontier)
            float currentCost = explorationCosts.count(targetCell) ? explorationCosts[targetCell] : 0.0f;
            
            auto neighbors = pathfinder.getNeighbors(targetCell);
            
            // for each unvisited neighbor calculate cost to reach it through current cell
            for (const auto& neighbor : neighbors) {
                if (visitedCells.find(neighbor) == visitedCells.end()) {
                    // then determine if this is a diagonal move (both x and y change)
                    int ndx = std::abs(neighbor.x - targetCell.x);
                    int ndy = std::abs(neighbor.y - targetCell.y);
                    bool isDiagonal = (ndx == 1 && ndy == 1);
                    // (diagonal moves cost sqrt(2) cardinal moves cost 1)
                    float moveCost = isDiagonal ? DIAGONAL_COST : CARDINAL_COST;
                    float newCost = currentCost + moveCost;
                    
                    // only add if cheaper than existing path
                    if (!explorationCosts.count(neighbor) || newCost < explorationCosts[neighbor]) {
                        explorationCosts[neighbor] = newCost;
                        explorationParents[neighbor] = targetCell;
                        explorationFrontier.push_back(neighbor);
                    }
                }
            }
        }
        
        explorationTargetX = -1;
        explorationTargetY = -1;
    }
    
    float finalMaxX = static_cast<float>(pathfinder.getWorldWidth() - 1);
    float finalMaxY = static_cast<float>(pathfinder.getWorldHeight() - 1);
    position.x = std::clamp(position.x, 1.0f, finalMaxX);
    position.y = std::clamp(position.y, 1.0f, finalMaxY);
    
    return false;
}


// "authentic"-ish physarum-like behavior for benchmark mode comparison.
// the slime navigates using only local chemical sensing (no direct goal coordinates).
// so it follows trails laid by other slimes and a "food" gradient emanating from the goal.
// energy management drives exploration vs exploitation: low energy = more random wandering.

//NOTE: this needs a rework its not at the level it should be (behavior wise)
bool Agent::benchmarkSlimeStep(TrailMap& trailMap, const Pathfinder& pathfinder,
                                const SimulationSettings &settings,
                                float trailDepositStrength, float goalX, float goalY,
                                int goalFieldChannel) {

    const int worldWidth = pathfinder.getWorldWidth();
    const int worldHeight = pathfinder.getWorldHeight();
    const int cellSize = pathfinder.getCellSize();

    // channel indices for trail sampling - goalFieldChannel has "food" scent, slimeChannel is our trail
    const int safeFoodChannel = std::clamp(goalFieldChannel, 0, trailMap.getNumSpecies() - 1);
    const int slimeChannel = std::clamp(speciesIndex, 0, trailMap.getNumSpecies() - 1);

    // sensor parameters: how far and at what angle the 3 sensor array samples the environment
    const auto &species = settings.speciesSettings[
        std::clamp(speciesIndex, 0, static_cast<int>(settings.speciesSettings.size()) - 1)];
    const float stepLength = std::max(species.moveSpeed, 0.5f);
    const float sensorAngleScale = species.sensorAngleSpacing * (hasGenome ? genome.sensorAngleScale : 1.0f);
    const float sensorAngleRad = sensorAngleScale * M_PIf / 180.0f;
    const float sensorDist = species.sensorOffsetDistance * (hasGenome ? genome.sensorDistScale : 1.0f);
    const float worldDiagonal = std::hypot(static_cast<float>(worldWidth), static_cast<float>(worldHeight));
    sf::Vector2f goalVector(goalX - position.x, goalY - position.y);
    float preStepGoalDistance = std::hypot(goalVector.x, goalVector.y);
    if (preStepGoalDistance < 1e-4f) {
        preStepGoalDistance = 1e-4f;
    }

    // behavior tuning constants - these will control the balance between following trails like
    // seeking food, energy management, and wall following behavior.
    // FOOD_WEIGHT/TRAIL_WEIGHT: relative importance of food vs slime trails
    // GRADIENT_THRESHOLDs: minimum signal difference to consider "meaningful"
    // energy constants: how fast energy drains/gains in different situations
    // STICKY constants: penalize getting stuck on dense trails with no food
    // GOAL_PROGRESS: reward/penalize based on whether we moved closer to goal

    // excuse the magic numbers... yes this is bad
    constexpr float FOOD_WEIGHT = 1.0f;
    constexpr float TRAIL_WEIGHT = 0.30f;
    constexpr float GRADIENT_THRESHOLD = 0.0015f;
    constexpr float GOAL_GRADIENT_THRESHOLD = 0.00035f;
    constexpr float BASE_DRAIN = 0.0014f;
    constexpr float FOOD_GAIN = 0.0030f;
    constexpr float TRAIL_GAIN = 0.0006f;
    constexpr float LOW_SIGNAL_PENALTY = 0.0008f;
    constexpr float GOAL_FIELD_BONUS = 0.0030f;
    constexpr float STALE_TRAIL_THRESHOLD = 0.02f;
    constexpr float STALE_TRAIL_PENALTY = 0.0012f;
    constexpr float TRAIL_STICKY_THRESHOLD = 0.18f;
    constexpr float TRAIL_STICKY_PENALTY = 0.0014f;
    constexpr float TRAIL_STICKY_NOISE = 1.9f;
    constexpr float GOAL_PROGRESS_GAIN = 0.0011f;
    constexpr float GOAL_BACKTRACK_PENALTY = 0.00055f;
    constexpr float MIN_GOAL_DRIFT = 0.45f;
    constexpr float MAX_GOAL_DRIFT = 1.05f;
    constexpr float WALL_DRIFT_SUPPRESS = 0.35f;
    constexpr float COLLISION_DRIFT_SUPPRESS = 0.65f;
    constexpr int COLLISION_SUPPRESS_FRAMES = 10;
    constexpr float MAX_ENERGY = 1.8f;

    // collision recovery cooldown decrement
    if (benchmarkRecentCollisionFrames > 0) {
        benchmarkRecentCollisionFrames = std::max(benchmarkRecentCollisionFrames - 1, 0);
    }

    // blockedAt: checks if a world position is blocked by walls or out of bounds.
    // uses pathfinder grid to determine walkability. 5 pixel margin prevents edge clipping
    auto blockedAt = [&](float x, float y) {
        if (x < 5.0f || x > worldWidth - 5.0f ||
            y < 5.0f || y > worldHeight - 5.0f) {
            return true;
        }
        int cellX = static_cast<int>(x) / cellSize;
        int cellY = static_cast<int>(y) / cellSize;
        return pathfinder.isBlocked(cellX, cellY);
    };

    // tryStepFrom: attempts to move stepLength in the given heading direction.
    // returns true and sets outPos if destination is walkable and false otherwise
    auto tryStepFrom = [&](const sf::Vector2f &origin, float heading, sf::Vector2f &outPos) {
        float testX = origin.x + std::cos(heading) * stepLength;
        float testY = origin.y + std::sin(heading) * stepLength;
        if (blockedAt(testX, testY)) {
            return false;
        }
        outPos.x = testX;
        outPos.y = testY;
        return true;
    };

    // findSlideHeading: when blocked searches for an alternative heading to "slide" along the wall.
    // tries offsets from the base angle in order of preference, favoring the previously used side
    // to create consistent wall following behavior (keeps turning the same direction along a wall).
    auto findSlideHeading = [&](const sf::Vector2f &origin, float baseAngle, float &outHeading) {
        // angle offsets to try ordered by preference
        const float ninety = M_PIf * 0.5f;
        const float sixty = M_PIf / 3.0f;
        const float fortyFive = M_PIf * 0.25f;
        const float thirty = M_PIf / 6.0f;
        const float oneTwenty = M_PIf * (2.0f / 3.0f);

        // a preference for the side we were sliding on before (left or right of forward) because it feels more natural
        const int preferredSign = (benchmarkWallSlideSign >= 0) ? 1 : -1;
        const float offsets[] = {
            preferredSign * ninety,     // 90 deg turn preferred side first
            -preferredSign * ninety,    // then opposite side
            preferredSign * sixty,
            -preferredSign * sixty,
            preferredSign * oneTwenty,
            -preferredSign * oneTwenty,
            preferredSign * fortyFive,
            -preferredSign * fortyFive,
            preferredSign * thirty,
            -preferredSign * thirty
        };

        // tries each offset until we find one that isnt blocked
        sf::Vector2f scratch;
        for (float offset : offsets) {
            float heading = baseAngle + offset;
            if (tryStepFrom(origin, heading, scratch)) {
                outHeading = heading;
                benchmarkWallSlideSign = (offset >= 0.0f) ? 1 : -1;
                return true;
            }
        }
        return false;
    };

    // continueWallFollow: if we're currently wall following then keep moving in the slide direction.
    // decrements the follow counter each frame; if blocked then do a mid follow which cancels the follow mode
    auto continueWallFollow = [&](const sf::Vector2f &origin) {
        if (benchmarkWallFollowFrames <= 0) {
            return false;
        }
        sf::Vector2f candidate;
        if (tryStepFrom(origin, benchmarkWallSlideHeading, candidate)) {
            position = candidate;
            angle = benchmarkWallSlideHeading;
            benchmarkWallFollowFrames = std::max(benchmarkWallFollowFrames - 1, 0);
            return true;
        }
        // hit something while wall following... abort
        benchmarkWallFollowFrames = 0;
        return false;
    };

    // startWallFollow: initiates wall following mode by finding a slide heading and committing to it.
    // sets up benchmarkWallFollowFrames to continue sliding for 18 frames.
    auto startWallFollow = [&](const sf::Vector2f &origin, float baseAngle) {
        float heading = 0.0f;
        if (!findSlideHeading(origin, baseAngle, heading)) {
            return false;
        }
        sf::Vector2f candidate;
        if (!tryStepFrom(origin, heading, candidate)) {
            return false;
        }
        benchmarkWallSlideHeading = heading;
        benchmarkWallFollowFrames = 18;
        position = candidate;
        angle = heading;
        return true;
    };

    // SignalSample: holds the food and trail concentrations at a sample point,
    // plus a weighted combination used for decision making.
    struct SignalSample {
        float goal;     // food/goal field concentration
        float trail;    // slime trail concentration
        float combined; // weighted sum for navigation decisions
    };

    // sampleSignals: reads both food and trail channels at a world position.
    // returns combined signal for chemotaxis decision making.
    auto sampleSignals = [&](const sf::Vector2f &pos) {
        int sx = std::clamp(static_cast<int>(pos.x), 0, worldWidth - 1);
        int sy = std::clamp(static_cast<int>(pos.y), 0, worldHeight - 1);
        float goal = trailMap.sample(sx, sy, safeFoodChannel);
        float trail = trailMap.sample(sx, sy, slimeChannel);
        return SignalSample{goal, trail, goal * FOOD_WEIGHT + trail * TRAIL_WEIGHT};
    };

    // sampleGoalField: reads just the food channel for gradient calculation
    auto sampleGoalField = [&](const sf::Vector2f &pos) {
        int sx = std::clamp(static_cast<int>(pos.x), 0, worldWidth - 1);
        int sy = std::clamp(static_cast<int>(pos.y), 0, worldHeight - 1);
        return trailMap.sample(sx, sy, safeFoodChannel);
    };

    sf::Vector2f previousPosition = position;
    SignalSample localSignalsBeforeMove = sampleSignals(position);
    bool usedSlideStep = false;

    if (benchmarkWallFollowFrames > 0) {
        usedSlideStep = continueWallFollow(position);
        if (usedSlideStep) {
            previousPosition = position;
        }
    }

    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> noiseDist(-1.0f, 1.0f);

    if (!usedSlideStep) {
        previousPosition = position;

        // THE 3 SENSOR CHEMOTAXIS: sample environment at forward, left, and right sensor positions.
        // each sensor is sensorDist pixels ahead at angle, angle - sensorAngleRad, angle + sensorAngleRad.
        sf::Vector2f forwardPos = position + sf::Vector2f(std::cos(angle), std::sin(angle)) * sensorDist;
        sf::Vector2f leftPos = position + sf::Vector2f(std::cos(angle - sensorAngleRad), std::sin(angle - sensorAngleRad)) * sensorDist;
        sf::Vector2f rightPos = position + sf::Vector2f(std::cos(angle + sensorAngleRad), std::sin(angle + sensorAngleRad)) * sensorDist;

        SignalSample forward = sampleSignals(forwardPos);
        SignalSample left = sampleSignals(leftPos);
        SignalSample right = sampleSignals(rightPos);

        // stickyFactor: penalizes getting stuck on dense slime trails with no food nearby.
        // and when trail concentration exceeds threshold but food is low, increase random wandering.
        float stickyFactor = 0.0f;
        if (localSignalsBeforeMove.goal < 0.005f) {
            float trailExcess = localSignalsBeforeMove.trail - TRAIL_STICKY_THRESHOLD;
            if (trailExcess > 0.0f) {
                stickyFactor = std::clamp(trailExcess / (TRAIL_STICKY_THRESHOLD * 2.0f), 0.0f, 1.0f);
            }
        }

        // THE GOAL FIELD GRADIENT: sample food concentration at +/- offsets to estimate gradient direction.
        // this provides a sort of "bias" toward the goal even when direct path is blocked.
        const float gradientOffset = std::max(sensorDist * 0.5f, 6.0f);
        sf::Vector2f gradSampleX(gradientOffset, 0.0f);
        sf::Vector2f gradSampleY(0.0f, gradientOffset);
        float goalSamplePosX = sampleGoalField(position + gradSampleX);
        float goalSampleNegX = sampleGoalField(position - gradSampleX);
        float goalSamplePosY = sampleGoalField(position + gradSampleY);
        float goalSampleNegY = sampleGoalField(position - gradSampleY);
        // gradient vector points toward increasing food concentration
        sf::Vector2f goalGradientVec(goalSamplePosX - goalSampleNegX,
                                     goalSamplePosY - goalSampleNegY);
        float goalGradientMag = std::hypot(goalGradientVec.x, goalGradientVec.y);
        sf::Vector2f goalGradientDir = (goalGradientMag > 1e-5f)
            ? goalGradientVec / goalGradientMag
            : sf::Vector2f(0.0f, 0.0f);
        float gradientHeading = (goalGradientMag > 1e-5f)
            ? std::atan2(goalGradientDir.y, goalGradientDir.x)
            : angle;

        // pickDirection: classic chemotaxis - turn toward the sensor with highest "signal"
        auto pickDirection = [&](float forwardValue, float leftValue, float rightValue) {
            if (forwardValue >= leftValue && forwardValue >= rightValue) {
                return angle; // forward is best so keep going!
            }
            if (leftValue > rightValue) {
                return angle - sensorAngleRad; // turn left
            }
            return angle + sensorAngleRad; // turn right
        };

        // check if gradients are "flat" (no meaningful difference between sensors).
        // and if all three sensors read nearly the same value the poor little guy cant tell which way to go
        float maxCombined = std::max({forward.combined, left.combined, right.combined});
        float minCombined = std::min({forward.combined, left.combined, right.combined});
        bool combinedFlat = (maxCombined - minCombined) < GRADIENT_THRESHOLD;

        // same check for goal only signal (food scent without trail influence)
        float maxGoal = std::max({forward.goal, left.goal, right.goal});
        float minGoal = std::min({forward.goal, left.goal, right.goal});
        float goalSensorGradient = maxGoal - minGoal;
        bool goalSensorGradientStrong = goalSensorGradient >= GOAL_GRADIENT_THRESHOLD;

        // trail only gradient (used for sticky detection not the primary navigation)
        float maxTrail = std::max({forward.trail, left.trail, right.trail});
        float minTrail = std::min({forward.trail, left.trail, right.trail});
        float trailGradient = maxTrail - minTrail;

        // direction priority: goal signal > combined signal > random exploration.
        // if we can smell the goal clearly then follow it. otherwise follow trails.
        // if everything is flat, wander randomly (with extra noise if stuck on trails).
        float desiredAngle = angle;
        if (goalSensorGradientStrong) {
            desiredAngle = pickDirection(forward.goal, left.goal, right.goal);
        } else if (!combinedFlat) {
            desiredAngle = pickDirection(forward.combined, left.combined, right.combined);
        } else {
            // no gradient detected: random exploration with sticky factor amplification
            float exploreTurn = sensorAngleRad * noiseDist(rng);
            float exploreScale = 1.5f + stickyFactor * TRAIL_STICKY_NOISE;
            desiredAngle = angle + exploreTurn * exploreScale;
        }

        // goal drift: blend the chemotaxis direction toward the global goal gradient.
        // this gives a subtle "homing" bias even when local sensors dont detect food.
        float desiredGoalAngle = gradientHeading;

        // distNorm: how far we actually are from goal as fraction of world diagonal (0=at goal, 1=far).
        // farther from goal = stronger drift toward it.
        float distNorm = (worldDiagonal > 0.0f)
            ? std::clamp(preStepGoalDistance / worldDiagonal, 0.0f, 1.0f)
            : 0.0f;
        float driftSpan = MAX_GOAL_DRIFT - MIN_GOAL_DRIFT;
        float baseDrift = MIN_GOAL_DRIFT + driftSpan * distNorm;
        // scale drift by how strong the goal gradient actually is
        float goalWeight = std::clamp(goalGradientMag / (GOAL_GRADIENT_THRESHOLD * 3.0f), 0.0f, 1.0f);
        float goalDriftStrength = baseDrift * goalWeight;
        bool fieldGradientStrong = goalGradientMag >= GOAL_GRADIENT_THRESHOLD;
        // reduce drift when following trails (let chemotaxis dominate)
        if (!fieldGradientStrong && !combinedFlat) {
            goalDriftStrength *= 0.5f;
        }
        // suppress drift during wall following or collision recovery to avoid fighting the slide
        if (benchmarkWallFollowFrames > 0) {
            goalDriftStrength *= WALL_DRIFT_SUPPRESS;
        } else if (benchmarkRecentCollisionFrames > 0) {
            goalDriftStrength *= COLLISION_DRIFT_SUPPRESS;
        }
        // apply the drift: rotate desiredAngle toward desiredGoalAngle by goalDriftStrength fraction
        if (goalDriftStrength > 0.01f) {
            float headingDelta = std::atan2(std::sin(desiredGoalAngle - desiredAngle), std::cos(desiredGoalAngle - desiredAngle));
            desiredAngle += headingDelta * goalDriftStrength;
        }

        // jitter: small random noise to prevent getting stuck in a local minima
        // low energy = more desperate = more jitter. stuck on trails = more jitter
        float energyFactor = benchmarkEnergy < 0.25f ? 1.8f : 1.0f;
        float stickyNoise = 1.0f + stickyFactor * (TRAIL_STICKY_NOISE * 0.5f);
        float jitter = noiseDist(rng) * 0.2f * energyFactor * stickyNoise;
        desiredAngle += jitter;

        // apply turn rate limit and move forward
        float delta = std::clamp(desiredAngle - angle, -maxTurnPerStep, maxTurnPerStep);
        angle += delta;

        position.x += std::cos(angle) * stepLength;
        position.y += std::sin(angle) * stepLength;
    }

    // collision check: if we walked into a wall revert position and try to recover
    bool blocked = blockedAt(position.x, position.y);
    if (blocked) {
        benchmarkRecentCollisionFrames = COLLISION_SUPPRESS_FRAMES;
        position = previousPosition;
    }

    // collision recovery: try wall following first then fallback angles
    if (blocked) {
        sf::Vector2f slideOrigin = previousPosition;
        // first try: continue existing wall follow if active
        bool moved = continueWallFollow(slideOrigin);
        // second try: start a new wall follow along the obstacle
        if (!moved) {
            moved = startWallFollow(slideOrigin, angle);
        }

        // fallback: try increasingly desperate angle offsets to escape
        if (!moved) {
            const float fallbackOffsets[] = {0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -1.5f, M_PIf * 0.5f, -M_PIf * 0.5f};

            for (float offset : fallbackOffsets) {
                float testAngle = angle + offset;
                sf::Vector2f candidate;
                if (tryStepFrom(slideOrigin, testAngle, candidate)) {
                    angle = testAngle;
                    position = candidate;
                    moved = true;
                    break;
                }
            }

            // last resort: just rotate and stay put (will try again next frame)
            if (!moved) {
                angle += 0.5f;
                position = slideOrigin;
            }
        }
    } else {
        // no collision: clear wall follow state so we can resume normal chemotaxis
        benchmarkWallFollowFrames = 0;
    }

    // final position clamp to stay within world bounds
    position.x = std::clamp(position.x, 5.0f, static_cast<float>(worldWidth - 5));
    position.y = std::clamp(position.y, 5.0f, static_cast<float>(worldHeight - 5));

    // sample signals at new position for energy calculations
    int px = std::clamp(static_cast<int>(position.x), 0, worldWidth - 1);
    int py = std::clamp(static_cast<int>(position.y), 0, worldHeight - 1);
    float postStepGoalDistance = std::hypot(goalX - position.x, goalY - position.y);
    SignalSample localSignals = sampleSignals(position);
    float localFood = localSignals.goal;
    float localTrail = localSignals.trail;
    float combinedSignal = localSignals.combined;

    // ENERGY SYSTEM: simulates metabolic cost of exploration.
    // energy decays over time gained from food/trails lost from bad situations
    // when energy hits 0 the agent "dies" (returns false).
    
    // exponential moving average of signal strength (for detecting stagnation)
    benchmarkSignalMemory = 0.9f * benchmarkSignalMemory + 0.1f * combinedSignal;
    
    // base metabolism: always drains energy just for existing
    benchmarkEnergy -= BASE_DRAIN;
    // food gives energy (goal field = "food scent")
    benchmarkEnergy += localFood * (FOOD_GAIN + GOAL_FIELD_BONUS);
    // if no food detected trails give a small energy boost (following others = less wasted effort)
    if (localFood <= 0.0005f) {
        benchmarkEnergy += localTrail * TRAIL_GAIN;
    }

    // penalty for being in "dead" areas (no trail, no food = wasted exploration)
    if (localTrail < STALE_TRAIL_THRESHOLD && localFood < 0.01f) {
        benchmarkEnergy -= STALE_TRAIL_PENALTY;
    }

    // low signal penalty: if we cant sense anything useful drain energy faster
    if (localFood < 0.01f) {
        benchmarkLowSignalFrames = std::min(benchmarkLowSignalFrames + 1, 600);
        benchmarkEnergy -= LOW_SIGNAL_PENALTY;
    } else if (combinedSignal < 0.02f) {
        benchmarkLowSignalFrames = std::min(benchmarkLowSignalFrames + 1, 600);
        benchmarkEnergy -= LOW_SIGNAL_PENALTY;
    } else {
        // good signal: recover from low signal state
        benchmarkLowSignalFrames = std::max(benchmarkLowSignalFrames - 2, 0);
    }

    // sticky penalty: punish getting stuck on dense trails with no food nearby
    // (following old trails that dont lead anywhere)
    if (localFood < 0.005f && localTrail > TRAIL_STICKY_THRESHOLD) {
        float stickyFactorPost = std::clamp((localTrail - TRAIL_STICKY_THRESHOLD) / (TRAIL_STICKY_THRESHOLD * 2.0f), 0.0f, 1.0f);
        benchmarkEnergy -= TRAIL_STICKY_PENALTY * (0.5f + stickyFactorPost * 1.5f);
    }

    // goal progress reward/penalty: moving toward goal = good and backtracking = bad.
    // this creates selective pressure for agents that make net progress.
    if (benchmarkPrevGoalDistance < 0.0f) {
        // first frame: just record distance no reward/penalty yet
        benchmarkPrevGoalDistance = postStepGoalDistance;
    } else {
        // positive delta = got closer to goal, negative = moved away
        float distanceDelta = benchmarkPrevGoalDistance - postStepGoalDistance;
        if (distanceDelta > 0.0f) {
            benchmarkEnergy += distanceDelta * GOAL_PROGRESS_GAIN;
        } else if (distanceDelta < 0.0f) {
            // backtracking penalty is weaker than progress reward (allow some exploration)
            benchmarkEnergy += distanceDelta * GOAL_BACKTRACK_PENALTY;
        }
        benchmarkPrevGoalDistance = postStepGoalDistance;
    }

    // clamp energy and check for death
    benchmarkEnergy = std::clamp(benchmarkEnergy, 0.0f, MAX_ENERGY);
    if (benchmarkEnergy <= 0.0f) {
        return false;  // agent dies from exhaustion
    }

    // record position in path memory (for trail reinforcement when goal is found)
    pushPathMemory(static_cast<int>(position.x), static_cast<int>(position.y));

    // TRAIL DEPOSITION: leave pheromone trail for other agents to follow.
    // higher energy = stronger trail (successful agents leave clearer paths).
    const float energyScale = std::clamp(0.4f + benchmarkEnergy, 0.2f, 2.5f);
    float baseStrength = trailDepositStrength * 10.0f * energyScale;
    int w = trailMap.getWidth();
    int h = trailMap.getHeight();

    // deposit in a 5x5 area with a manhattan distance falloff (center strongest)
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dy = -2; dy <= 2; ++dy) {
            int nx = px + dx;
            int ny = py + dy;
            if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                float falloff = 1.0f - (std::abs(dx) + std::abs(dy)) * 0.15f;
                trailMap.deposit(nx, ny, baseStrength * falloff, slimeChannel);
            }
        }
    }

    return true;  // agent survives to next frame
}

// Reinforce the recent path with bonus trail deposits when goal is found
// creates stigmergic feedback - other slimes will follow the proven path
void Agent::reinforceRecentPath(TrailMap& trailMap, float baseStrength) {
    if (pathMemoryCount == 0) return;
    
    int worldWidth = trailMap.getWidth();
    int worldHeight = trailMap.getHeight();
    
    // iterating through the ring buffer from oldest to newest
    // iterate through path memory from oldest to newest position.
    // newer positions (closer to goal) get stronger reinforcement because
    // they represent the "good" part of the path that led to success.
    for (size_t i = 0; i < pathMemoryCount; ++i) {
        // ring buffer index calculation:
        // the buffer wraps around so we need to map logical index i to physical index.
        // when buffer isnt full yet its a simple 1:1 mapping.
        // when buffer is full pathMemoryIndex points to the oldest entry (and next write position)
        size_t bufferIdx;
        if (pathMemoryCount < PATH_MEMORY_SIZE) {
            bufferIdx = i;
        } else {
            // wrap around: start at oldest entry and advance by i
            bufferIdx = (pathMemoryIndex + i) % PATH_MEMORY_SIZE;
        }
        
        sf::Vector2i pos = recentPositions[bufferIdx];
        
        // progress based decay: positions closer to the goal get stronger reinforcement.
        // i=0 is the oldest/farthest position -> 20% strength (weakest)
        // i=pathMemoryCount-1 is newest/closest to goal -> 100% strength (strongest)
        // this creates a "ramp" of trail intensity that guides other agents toward the goal.
        float progress = static_cast<float>(i) / static_cast<float>(pathMemoryCount);
        float decayMultiplier = 0.2f + 0.8f * progress;  // range: 0.2 to 1.0
        
        // 10x base strength makes the highway clearly visible
        float depositStrength = baseStrength * decayMultiplier * 10.0f;
        
        // deposit at the path position itself
        if (pos.x >= 0 && pos.x < worldWidth && pos.y >= 0 && pos.y < worldHeight) {
            trailMap.deposit(pos.x, pos.y, depositStrength, 2);  // channel 2 = slime (bright green)
            
            // also reinforce a 5x5 area around each position to create a wide "highway".
            // so the successful path is broad enough for other agents to follow.
            for (int dx = -2; dx <= 2; ++dx) {
                for (int dy = -2; dy <= 2; ++dy) {
                    if (dx == 0 && dy == 0) continue; // skip center (already deposited)
                    int nx = pos.x + dx;
                    int ny = pos.y + dy;
                    if (nx >= 0 && nx < worldWidth && ny >= 0 && ny < worldHeight) {
                        // falloff by manhattan distance: farther cells get weaker deposit
                        float falloff = 1.0f - (std::abs(dx) + std::abs(dy)) * 0.2f;
                        trailMap.deposit(nx, ny, depositStrength * falloff, 2);
                    }
                }
            }
        }
    }
    
    // clear the path memory after reinforcement (for next journey)
    pathMemoryIndex = 0;
    pathMemoryCount = 0;
}

float Agent::sampleChemoattractant(const float *grid, int x, int y, int width, int height) const
{
    if (x < 0 || x >= width || y < 0 || y >= height)
    {
        return 0.0f;
    }
    return grid[y * width + x];
}

std::vector<Agent> AgentFactory::createAgents(const SimulationSettings &settings)
{
    // default behavior - no species reroll mapping
    return createAgents(settings, std::vector<int>());
}

std::vector<Agent> AgentFactory::createAgents(const SimulationSettings &settings, const std::vector<int> &activeSpeciesIndices)
{
    std::vector<Agent> agents;
    agents.reserve(settings.numAgents);

    static std::random_device rd;
    static std::mt19937 gen(rd());

    sf::Vector2f center(settings.width / 2.0f, settings.height / 2.0f);
    int numSpecies = settings.speciesSettings.size();

    // multi colony spawning: each species gets 1-3 separate "colonies" (clusters).
    // this ensures species dont all start in one blob, and territorial/loner
    // species have neighbors of their own kind to interact with at startup.
    struct SpawnCluster {
        sf::Vector2f center;
        int speciesIndex;
    };
    std::vector<SpawnCluster> allClusters;
    
    // each species gets a random number of colonies (1-3)
    std::uniform_int_distribution<int> clusterCountDist(1, 3);
    // clusters spawn in the middle 80% of the world (10%-90% on each axis)
    std::uniform_real_distribution<float> posDistX(settings.width * 0.1f, settings.width * 0.9f);
    std::uniform_real_distribution<float> posDistY(settings.height * 0.1f, settings.height * 0.9f);
    
    if (numSpecies > 1)
    {
        // multi species mode: create multiple clusters per species
        for (int s = 0; s < numSpecies; ++s)
        {
            int numClusters = clusterCountDist(gen);
            for (int c = 0; c < numClusters; ++c)
            {
                // random position for each cluster
                sf::Vector2f clusterCenter(posDistX(gen), posDistY(gen));
                allClusters.push_back({clusterCenter, s});
            }
        }
        std::cout << "Created " << allClusters.size() << " spawn clusters across " << numSpecies << " species" << std::endl;
    }
    else
    {
        // single species mode: using center
        allClusters.push_back({center, 0});
    }

    // distribute agents evenly among clusters of their own species
    // first count how many clusters each species has then divide agents accordingly
    std::vector<int> agentsPerCluster(allClusters.size(), 0);
    int agentsPerSpecies = settings.numAgents / numSpecies;
    
    // and for each species find its clusters and assign equal agent counts
    for (int s = 0; s < numSpecies; ++s)
    {
        // and collect indices of all clusters belonging to this species
        std::vector<size_t> speciesClusters;
        for (size_t i = 0; i < allClusters.size(); ++i)
        {
            if (allClusters[i].speciesIndex == s)
                speciesClusters.push_back(i);
        }
        
        // divide this species agent quota among its clusters
        if (!speciesClusters.empty())
        {
            int agentsEach = agentsPerSpecies / speciesClusters.size();
            for (size_t idx : speciesClusters)
            {
                agentsPerCluster[idx] = agentsEach;
            }
        }
    }

    // spawn agents at their cluster centers with tight grouping (+/-30 pixels)
    std::uniform_real_distribution<float> clusterSpread(-30.0f, 30.0f);
    
    // iterate through each cluster and spawn its assigned agents
    for (size_t clusterIdx = 0; clusterIdx < allClusters.size(); ++clusterIdx)
    {
        const auto& cluster = allClusters[clusterIdx];
        int agentCount = agentsPerCluster[clusterIdx];
        
        // get lifespan for this species to randomize starting age
        float lifespan = 60.0f;  // default
        if (cluster.speciesIndex < static_cast<int>(settings.speciesSettings.size())) {
            lifespan = settings.speciesSettings[cluster.speciesIndex].lifespanSeconds;
        }
        // stagger starting ages across 0-90% of lifespan to prevent mass extinction waves.
        // if all agents started at age 0 they'd all die at the same time! not good...
        std::uniform_real_distribution<float> ageDist(0.0f, lifespan * 0.9f);
        
        // spawn each agent near the cluster center with random angle
        for (int i = 0; i < agentCount; ++i)
        {
            // spawn close to cluster center
            sf::Vector2f spawnPos(
                std::clamp(cluster.center.x + clusterSpread(gen), 0.0f, (float)settings.width),
                std::clamp(cluster.center.y + clusterSpread(gen), 0.0f, (float)settings.height)
            );
            
            // (random angle)
            std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * M_PI);
            float spawnAngle = angleDist(gen);

            agents.emplace_back(spawnPos.x, spawnPos.y, spawnAngle, cluster.speciesIndex);
            
            // randomize starting age so deaths are staggered
            agents.back().ageSeconds = ageDist(gen);

            // and set correct species mask
            if (!activeSpeciesIndices.empty() && cluster.speciesIndex < (int)activeSpeciesIndices.size())
            {
                int originalSpeciesIndex = activeSpeciesIndices[cluster.speciesIndex];
                agents.back().setSpeciesMaskFromOriginalIndex(originalSpeciesIndex);
            }
            else
            {
                agents.back().setDefaultSpeciesMask(cluster.speciesIndex);
            }
        }
    }

    std::cout << "Created " << agents.size() << " agents across " << numSpecies
              << " species with CLUSTERED spawning (" << allClusters.size() << " colonies)" << std::endl;

    return agents;
}

sf::Vector2f AgentFactory::getSpawnPosition(SimulationSettings::SpawnMode mode,
                                            const sf::Vector2f &center,
                                            int width, int height)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    switch (mode)
    {
    case SimulationSettings::SpawnMode::Point:
        return center;

    case SimulationSettings::SpawnMode::Random:
        return sf::Vector2f(dis(gen) * width, dis(gen) * height);

    case SimulationSettings::SpawnMode::InwardCircle:
    {
        float radius = std::min(width, height) * 0.4f;
        float angle = dis(gen) * 2.0f * M_PI;
        float r = dis(gen) * radius;
        return center + sf::Vector2f(r * std::cos(angle), r * std::sin(angle));
    }

    case SimulationSettings::SpawnMode::RandomCircle:
    {
        float radius = std::min(width, height) * 0.15f;
        float angle = dis(gen) * 2.0f * M_PI;
        float r = dis(gen) * radius;
        return center + sf::Vector2f(r * std::cos(angle), r * std::sin(angle));
    }

    case SimulationSettings::SpawnMode::Clusters:
    default:
    {
        static std::uniform_int_distribution<int> clusterDis(3, 7);
        static int numClusters = clusterDis(gen);
        static int currentCluster = 0;
        static sf::Vector2f clusterCenter = sf::Vector2f(dis(gen) * width, dis(gen) * height);
        static int agentsInCurrentCluster = 0;
        static int maxAgentsPerCluster = 1000; // will be updated

        if (agentsInCurrentCluster >= maxAgentsPerCluster)
        {
            currentCluster++;
            if (currentCluster >= numClusters)
            {
                currentCluster = 0;
                numClusters = clusterDis(gen);
            }
            clusterCenter = sf::Vector2f(dis(gen) * width, dis(gen) * height);
            agentsInCurrentCluster = 0;
        }

        float clusterRadius = 50.0f;
        float angle = dis(gen) * 2.0f * M_PI;
        float r = dis(gen) * clusterRadius;
        agentsInCurrentCluster++;

        return clusterCenter + sf::Vector2f(r * std::cos(angle), r * std::sin(angle));
    }
    }
}

float AgentFactory::getSpawnAngle(SimulationSettings::SpawnMode mode,
                                  const sf::Vector2f &position,
                                  const sf::Vector2f &center)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    switch (mode)
    {
    case SimulationSettings::SpawnMode::InwardCircle:
    {
        sf::Vector2f direction = center - position;
        float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
        if (length > 0)
        {
            direction /= length;
            return std::atan2(direction.y, direction.x);
        }
        break;
    }
    default:
        break;
    }

    // default: random angle
    return dis(gen) * 2.0f * M_PI;
}

// custom species specific sensing behaviors  

// red species: territorial bully - advanced territorial control and isolation tactics
float Agent::senseRedTerritorialBully(TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species)
{
    int ix = static_cast<int>(x), iy = static_cast<int>(y);

    // RED is a PREDATOR - it HUNTS other species!
    // when it smells prey it goes for the kill

    float ownTrail = trailMap.sample(ix, iy, speciesIndex);
    float totalOtherTrails = 0.0f;

    // sample of other species - these are prey to hunt!
    for (int otherSpecies = 0; otherSpecies < trailMap.getNumSpecies(); otherSpecies++)
    {
        if (otherSpecies != speciesIndex)
        {
            totalOtherTrails += trailMap.sample(ix, iy, otherSpecies);
        }
    }

    // base attraction to own trail
    float ownAttraction = ownTrail * species.attractionToSelf;

    // HUNTING INSTINCT: strong attraction to other species trails!
    // this is what makes red chase prey
    float huntingDrive = totalOtherTrails * 2.0f;  // strong chase!

    // total: own trail + hunting. when prey is near, hunt it
    return ownAttraction + huntingDrive;
}

// blue species: altruistic helper - sophisticated cooperation and assistance
float Agent::senseBlueAltruisticHelper(TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species)
{
    int ix = static_cast<int>(x), iy = static_cast<int>(y);

    // BLUE is ALTRUISTIC - so it seeks other species to help them
    // "oh look theres another slime! let me go say hi!"

    float ownTrail = trailMap.sample(ix, iy, speciesIndex);
    float totalOtherTrails = 0.0f;

    // sample other species - "friends" to find
    for (int otherSpecies = 0; otherSpecies < trailMap.getNumSpecies(); otherSpecies++)
    {
        if (otherSpecies != speciesIndex)
        {
            totalOtherTrails += trailMap.sample(ix, iy, otherSpecies);
        }
    }

    float ownAttraction = ownTrail * species.attractionToSelf;

    // HELPING INSTINCT: strong attraction to other species trails!
    float helpingDrive = totalOtherTrails * 2.5f;  // very eager to find others

    // total: own trail + helping. when others are near go to them
    return ownAttraction + helpingDrive;
}

// green species: nomadic loner - advanced avoidance and isolation seeking
float Agent::senseGreenNomadicLoner(TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species)
{
    int ix = static_cast<int>(x), iy = static_cast<int>(y);

    // GREEN is a loner/nomad - it flees from everything
    // "I need a safe space."

    float ownTrail = trailMap.sample(ix, iy, speciesIndex);
    float totalOtherTrails = 0.0f;

    // slimes to flee from
    for (int otherSpecies = 0; otherSpecies < trailMap.getNumSpecies(); otherSpecies++)
    {
        if (otherSpecies != speciesIndex)
        {
            totalOtherTrails += trailMap.sample(ix, iy, otherSpecies);
        }
    }

    // weaker attraction to own trail (doesnt even like own kind much)
    float ownAttraction = ownTrail * species.attractionToSelf * 0.5f;

    // FLEE INSTINCT: strong repulsion from other species trails
    // more other trail = lower weight = turn away
    float fleeDrive = -totalOtherTrails * 3.0f;  // strong flee

    // base value so empty space is attractive
    float baseValue = 1.0f;

    return baseValue + ownAttraction + fleeDrive;
}

// yellow species: alien - weird behavior
float Agent::senseYellowQuantumAlien(TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species)
{
    int ix = static_cast<int>(x), iy = static_cast<int>(y);

    // yellow is "alien" - it defies the normal slime mold logic

    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> alienRand(0.0f, 1.0f);

    // per agent alien state
    static std::unordered_map<size_t, float> alienPhases;
    static std::unordered_map<size_t, int> alienRealities;
    static std::unordered_map<size_t, float> alienChaos;

    size_t agentId = reinterpret_cast<size_t>(this);

    // initialize alien consciousness

    if (alienPhases.find(agentId) == alienPhases.end())
    {
        alienPhases[agentId] = alienRand(gen) * 6.28f;
        alienRealities[agentId] = static_cast<int>(alienRand(gen) * 5);
        alienChaos[agentId] = alienRand(gen);
    }

    float phase = alienPhases[agentId] += 0.1f + species.behaviorIntensity * 0.05f;
    int reality = alienRealities[agentId];
    float chaos = alienChaos[agentId];

    // sample trails in alien dimensional space
    float ownTrail = trailMap.sample(ix, iy, speciesIndex);
    float totalOthers = 0.0f;
    for (int i = 0; i < trailMap.getNumSpecies(); i++)
    {
        if (i != speciesIndex)
            totalOthers += trailMap.sample(ix, iy, i);
    }

    float attraction = 0.0f;

    switch (reality)
    {
    case 0:
        attraction = std::sin(phase) * ownTrail + std::cos(phase * 1.7f) * totalOthers;
        break;
    case 1:
        attraction = -ownTrail * chaos + totalOthers * (1.0f - chaos);
        break;
    case 2: 
        attraction = (ownTrail > totalOthers) ? -totalOthers : ownTrail * 2.0f;
        break;
    case 3: 
        attraction = std::sin(phase * totalOthers + ownTrail) * (1.0f + chaos);
        break;
    case 4: // those alien mathematics yo
        attraction = std::pow(ownTrail + 0.1f, chaos) - std::sqrt(totalOthers + 0.1f);
        break;
    }

    // reality phase shifts
    if (alienRand(gen) < 0.005f * species.behaviorIntensity)
    {
        alienRealities[agentId] = (alienRealities[agentId] + 1) % 5;
        alienChaos[agentId] = alienRand(gen);
    }

    return attraction * (1.0f + species.behaviorIntensity * 0.3f);
}

// magenta species: orderly-logical, robotic
float Agent::senseMagentaOrderEnforcer(TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species)
{
    int ix = static_cast<int>(x), iy = static_cast<int>(y);


    float ownTrail = trailMap.sample(ix, iy, speciesIndex);
    float alienTrail = (trailMap.getNumSpecies() > 3) ? trailMap.sample(ix, iy, 3) : 0.0f;
    float totalOthers = 0.0f;

    // calculate order/chaos metrics
    float localOrder = 0.0f;
    for (int dx = -1; dx <= 1; dx++)
    {
        for (int dy = -1; dy <= 1; dy++)
        {
            localOrder += trailMap.sample(ix + dx, iy + dy, speciesIndex);
            for (int i = 0; i < trailMap.getNumSpecies(); i++)
            {
                if (i != speciesIndex && i != 3)
                { // exclude aliens
                    totalOthers += trailMap.sample(ix + dx, iy + dy, i);
                }
            }
        }
    }

    // logical attraction: build ordered geometric like structures
    float orderAttraction = ownTrail * (1.5f + species.behaviorIntensity * 0.4f);

    float antiAlien = -alienTrail * (3.0f + species.behaviorIntensity * 0.8f);

    float stabilization = totalOthers * (0.6f + species.behaviorIntensity * 0.2f);

    // order bonus: extra attraction for creating perfect patterns
    float orderBonus = (localOrder > 0.5f && alienTrail < 0.01f) ? 0.8f : 0.0f;

    return orderAttraction + antiAlien + stabilization + orderBonus;
}

// black/green species: parasitic invader - advanced parasitic behavior
float Agent::senseParasiticInvader(TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species)
{
    int ix = static_cast<int>(x), iy = static_cast<int>(y);

    // parasitic is an invader like slime
    // 1. hunt for dense colonies of other species to infect
    // 2. infiltrate and grow inside other species networks
    // 3. explode outward after sufficient infection
    // 4. completely despicable and relentless

    float ownTrail = trailMap.sample(ix, iy, speciesIndex);
    float preyDensity = 0.0f;
    float preyQuality = 0.0f;

    // hunt for the prey in infection radius
    for (int dx = -2; dx <= 2; dx++)
    {
        for (int dy = -2; dy <= 2; dy++)
        {
            for (int prey = 0; prey < trailMap.getNumSpecies(); prey++)
            {
                if (prey == speciesIndex)
                    continue;

                float preyTrail = trailMap.sample(ix + dx, iy + dy, prey);
                preyDensity += preyTrail;

                // blue cooperative species are especially tasty...
                if (prey == 1)
                    preyQuality += preyTrail * 2.0f;
                // order enforcers are crunchy and satisfying. naturally...
                if (prey == 4)
                    preyQuality += preyTrail * 1.5f;
            }
        }
    }

    //  massive attraction to dense prey colonies
    float huntingUrge = preyDensity * (4.0f + species.behaviorIntensity * 1.0f);

    // avoid own kind until ready to spore burst
    float avoidSelf = -ownTrail * (0.5f + species.behaviorIntensity * 0.2f);

    // parasitic bonus: extra attraction to high quality prey
    float parasitismBonus = preyQuality * (1.5f + species.behaviorIntensity * 0.5f);

    // explosive phase: when infected area is dense enough, seek other areas
    if (ownTrail > 0.8f && preyDensity > 1.0f)
    {
        huntingUrge *= 0.3f; // time to spread to new areas
        avoidSelf = -2.0f;   // explode outward
    }

    return huntingUrge + avoidSelf + parasitismBonus;
}

// crimson species: destroyer - pure malevolence 
float Agent::senseDemonicDestroyer(TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species)
{
    int ix = static_cast<int>(x), iy = static_cast<int>(y);
 
    // pure evil incarnate:
    // 1. seeks to annihilate all other species with extreme prejudice
    // 2. creates destructive fire patterns that consume everything
    // 3. coordinates with other bad slimes for maximum effect
    // 4. absolutely no mercy or cooperation with anyone else

    float ownTrail = trailMap.sample(ix, iy, speciesIndex);
    float victimTrails = 0.0f;
    float destructionPotential = 0.0f;

    // scan for victims to destroy in wide radius
    for (int dx = -3; dx <= 3; dx++)
    {
        for (int dy = -3; dy <= 3; dy++)
        {
            for (int victim = 0; victim < trailMap.getNumSpecies(); victim++)
            {
                if (victim == speciesIndex)
                    continue;

                float victimTrail = trailMap.sample(ix + dx, iy + dy, victim);
                victimTrails += victimTrail;

                // calculate maximum possible destruction
                if (victimTrail > 0.1f)
                {
                    destructionPotential += victimTrail * (std::abs(dx) + std::abs(dy) + 1);
                }
            }
        }
    }

    // overwhelming attraction to areas with slimes
    float destructionLust = victimTrails * (5.0f + species.behaviorIntensity * 1.5f);

    // pstrong self attraction for coordinated attacks
    float demonicUnity = ownTrail * (2.0f + species.behaviorIntensity * 0.6f);

    // annihilation bonus: extra malice for dense victim areas
    float maliceBonus = (destructionPotential > 2.0f) ? (2.0f + species.behaviorIntensity) : 0.0f;

    // hatred amplifier: pure evil seeks maximum suffering
    float hatredAmplifier = 1.0f + victimTrails * 0.5f;

    return (destructionLust + demonicUnity + maliceBonus) * hatredAmplifier;
}

// white species: consumption behavior
float Agent::senseAbsoluteDevourer(TrailMap &trailMap, float x, float y, const SimulationSettings::SpeciesSettings &species)
{
    int ix = static_cast<int>(x), iy = static_cast<int>(y);

    // 1. consumes everything in sight - claims it's "protection"
    // 2. grows larger and more powerful with each consumption
    // 3. creates expanding zones of complete consumption
    // 4. appetite that cannot be satisfied

    float ownTrail = trailMap.sample(ix, iy, speciesIndex);
    float totalFood = 0.0f;
    float feedingPotential = 0.0f;

    // scan for everything to devour in consumption radius
    for (int dx = -2; dx <= 2; dx++)
    {
        for (int dy = -2; dy <= 2; dy++)
        {
            for (int food = 0; food < trailMap.getNumSpecies(); food++)
            {
                if (food == speciesIndex)
                    continue;

                float foodTrail = trailMap.sample(ix + dx, iy + dy, food);
                totalFood += foodTrail;

                // closer food is more appetizing
                float distance = std::sqrt(dx * dx + dy * dy) + 0.1f;
                feedingPotential += foodTrail / distance;
            }
        }
    }

    // overwhelming drive to consume everything
    float appetite = totalFood * (6.0f + species.behaviorIntensity * 2.0f);

    // self attraction for building feeding zones
    float feedingZone = ownTrail * (1.8f + species.behaviorIntensity * 0.4f);

    //  feeding potential amplifies hunger
    float monstrousBonus = feedingPotential * (2.0f + species.behaviorIntensity * 0.8f);

    float protectionDelusion = (totalFood > 0.5f) ? 1.5f : 0.0f;

    // the more it eats the hungrier it gets
    float unstoppableHunger = 1.0f + ownTrail * 0.5f;

    return (appetite + feedingZone + monstrousBonus + protectionDelusion) * unstoppableHunger;
}

// custom species specific turning behaviors

void Agent::applyRedBullyTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> bullRand(0.0f, 1.0f);

    float turnSpeed = species.turnSpeed * M_PI / 180.0f * (1.0f + species.behaviorIntensity * 0.3f);

    // aggressive territory expansion behavior
    if (front > left && front > right)
    {
        // charge forward aggressively
        return;
    }
    else if (left > right)
    {
        // sharp territorial turns
        angle -= turnSpeed * (1.2f + bullRand(gen) * 0.5f);
    }
    else
    {
        angle += turnSpeed * (1.2f + bullRand(gen) * 0.5f);
    }

    // random aggressive positioning
    if (bullRand(gen) < 0.1f * species.behaviorIntensity)
    {
        angle += turnSpeed * (bullRand(gen) - 0.5f) * 0.8f;
    }
}

// blue species: cooperative helper turning patterns
void Agent::applyBlueCooperativeTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species)
{
    float turnSpeed = species.turnSpeed * M_PI / 180.0f * (0.8f + species.behaviorIntensity * 0.2f);

    // gentle thoughtful-like movement
    if (front > left && front > right)
    {
        // continue helping
        return;
    }
    else if (left > right)
    {
        // gradual cooperative turns
        angle -= turnSpeed * 0.9f;
    }
    else
    {
        angle += turnSpeed * 0.9f;
    }

    // small course corrections for precision help
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> helpRand(0.0f, 1.0f);

    if (helpRand(gen) < 0.05f)
    {
        angle += turnSpeed * (helpRand(gen) - 0.5f) * 0.3f; // gentle adjustments
    }
}

// green species: nomadic loner type turning patterns
void Agent::applyGreenAvoidanceTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species)
{
    // Green turns toward highest value (empty space has high value now)
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> wanderRand(0.0f, 1.0f);

    float turnSpeed = species.turnSpeed * M_PI / 180.0f;

    // Turn toward highest value (which is empty space)
    if (front >= left && front >= right)
    {
        // forward is emptiest so continue
        return;
    }
    else if (left > right)
    {
        // left is emptier, turn left
        angle -= turnSpeed * (1.0f + wanderRand(gen) * 0.5f);
    }
    else
    {
        // right is emptier, turn right
        angle += turnSpeed * (1.0f + wanderRand(gen) * 0.5f);
    }

    // extra random wandering for nomadic behavior
    if (wanderRand(gen) < 0.2f)
    {
        angle += turnSpeed * (wanderRand(gen) - 0.5f) * 2.0f;
    }
}

// yellow species: turning patterns
void Agent::applyAlienQuantumTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> alienRand(0.0f, 1.0f);

    // per agent quantum state
    static std::unordered_map<size_t, int> quantumStates;
    size_t agentId = reinterpret_cast<size_t>(this);

    if (quantumStates.find(agentId) == quantumStates.end())
    {
        quantumStates[agentId] = static_cast<int>(alienRand(gen) * 7);
    }

    int state = quantumStates[agentId];
    float turnSpeed = species.turnSpeed * M_PI / 180.0f;

    switch (state)
    {
    case 0: // quantum superposition
        if (front > left && front > right)
        {
            angle += turnSpeed * (alienRand(gen) - 0.5f) * 4.0f;
        }
        break;
    case 1: // phase variance turning
        angle += std::sin(front * 10.0f) * turnSpeed * 2.0f;
        break;
    case 2: 
        if (left > 0.1f)
            angle += turnSpeed * right;
        else if (right > 0.1f)
            angle -= turnSpeed * left;
        break;
    case 3:
        angle += turnSpeed * std::cos(front + left + right) * 3.0f;
        break;
    default: 
        angle += turnSpeed * std::pow(alienRand(gen), front + 0.1f) * 2.0f;
    }

    // quantum state transitions
    if (alienRand(gen) < 0.02f * species.behaviorIntensity)
    {
        quantumStates[agentId] = static_cast<int>(alienRand(gen) * 7);
    }
}

// magenta species: turning patterns
void Agent::applyOrderEnforcerTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species)
{
    // magenta uses precise
    float turnSpeed = species.turnSpeed * M_PI / 180.0f * (0.9f + species.behaviorIntensity * 0.2f);

    float maxValue = std::max({front, left, right});

    if (std::abs(front - maxValue) < 0.001f)
    {
        // optimal path forward
        return;
    }
    else if (std::abs(left - maxValue) < 0.001f)
    {
        // precise left turn
        angle -= turnSpeed;
    }
    else
    {
        // precise right turn
        angle += turnSpeed;
    }

    // geometric pattern correction
    static int stepCounter = 0;
    stepCounter++;
    if (stepCounter % 50 == 0)
    {                              // regular pattern adjustments
        angle += turnSpeed * 0.1f; // small geometric corrections
    }
}

// parasitic species: hunting-like turning patterns
void Agent::applyParasiticHuntingTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> huntRand(0.0f, 1.0f);

    float turnSpeed = species.turnSpeed * M_PI / 180.0f * (1.3f + species.behaviorIntensity * 0.5f);

    // aggressive hunting turns
    if (front > left && front > right)
    {
        // charge toward
        return;
    }
    else if (left > right)
    {
        // quick predatory like turn
        angle -= turnSpeed * (1.4f + huntRand(gen) * 0.6f);
    }
    else
    {
        angle += turnSpeed * (1.4f + huntRand(gen) * 0.6f);
    }

    if (huntRand(gen) < 0.2f * species.behaviorIntensity)
    {
        angle += turnSpeed * (huntRand(gen) - 0.5f) * 1.2f;
    }
}

// crimson species: estructive turning patterns
void Agent::applyDemonicDestructionTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> rageRand(0.0f, 1.0f);

    float turnSpeed = species.turnSpeed * M_PI / 180.0f * (1.5f + species.behaviorIntensity * 0.8f);

    // violent destruction seeking
    if (front > left && front > right)
    {
        // charge toward destruction
        return;
    }
    else if (left > right)
    {
        // violent thrashing turn
        angle -= turnSpeed * (1.6f + rageRand(gen) * 0.8f);
    }
    else
    {
        angle += turnSpeed * (1.6f + rageRand(gen) * 0.8f);
    }

    // demonic rage spasms
    if (rageRand(gen) < 0.25f * species.behaviorIntensity)
    {
        angle += turnSpeed * (rageRand(gen) - 0.5f) * 2.0f;
    }
}

// white species turning
void Agent::applyDevourerConsumptionTurning(float front, float left, float right, const SimulationSettings::SpeciesSettings &species)
{
    // engulfing turning strategies
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> hungerRand(0.0f, 1.0f);

    float turnSpeed = species.turnSpeed * M_PI / 180.0f * (1.4f + species.behaviorIntensity * 0.6f);

    // ravenous consumption seeking
    if (front > left && front > right)
    {
        // devour everything ahead
        return;
    }
    else if (left > right)
    {
        // consuming sweep left
        angle -= turnSpeed * (1.5f + hungerRand(gen) * 0.7f);
    }
    else
    {
        // consuming sweep right
        angle += turnSpeed * (1.5f + hungerRand(gen) * 0.7f);
    }

    // monstrous hunger spasms
    if (hungerRand(gen) < 0.3f * species.behaviorIntensity)
    {
        angle += turnSpeed * (hungerRand(gen) - 0.5f) * 1.8f;
    }
}

// optimized methods for high performance systems

void Agent::senseWithSpatialGrid(const SpatialGrid &spatialGrid, const std::vector<Agent> &allAgents,
                                 OptimizedTrailMap &trailMap, const SimulationSettings &settings)
{
    if (settings.speciesSettings.empty())
        return;
    const auto &species = settings.speciesSettings[std::min(speciesIndex, static_cast<int>(settings.speciesSettings.size()) - 1)];

    // cache frequently used values for performance with genome scaling
    const float sensorDist = species.sensorOffsetDistance * (hasGenome ? genome.sensorDistScale : 1.0f);
    const float sensorAngleRad = species.sensorAngleSpacing * (hasGenome ? genome.sensorAngleScale : 1.0f) * M_PIf / 180.0f;
    const float leftAngle = angle - sensorAngleRad;
    const float rightAngle = angle + sensorAngleRad;

    // calculate sensor positions using simd-friendly math
    float cosAngle = std::cos(angle);
    float sinAngle = std::sin(angle);
    float cosLeft = std::cos(leftAngle);
    float sinLeft = std::sin(leftAngle);
    float cosRight = std::cos(rightAngle);
    float sinRight = std::sin(rightAngle);

    sf::Vector2f frontPos(position.x + cosAngle * sensorDist, position.y + sinAngle * sensorDist);
    sf::Vector2f leftPos(position.x + cosLeft * sensorDist, position.y + sinLeft * sensorDist);
    sf::Vector2f rightPos(position.x + cosRight * sensorDist, position.y + sinRight * sensorDist);

    // effective per agent parameters with genome scaling
    const float alignW = species.alignmentWeight * (hasGenome ? genome.alignWScale : 1.0f);
    const float cohW = species.cohesionWeight * (hasGenome ? genome.cohWScale : 1.0f);
    const float sepW = species.separationWeight * (hasGenome ? genome.sepWScale : 1.0f);
    const float sepRad = species.separationRadius * (hasGenome ? genome.sensorDistScale : 1.0f);
    const float oscStr = species.oscillatorStrength * (hasGenome ? genome.oscStrengthScale : 1.0f);
    const float oscHz = species.oscillatorFrequency * (hasGenome ? genome.oscFreqScale : 1.0f);

    // sample trail concentrations using optimized trail map
    float front = trailMap.sampleOptimized(frontPos.x, frontPos.y, speciesIndex, species.attractionToSelf, species.attractionToOthers);
    float left = trailMap.sampleOptimized(leftPos.x, leftPos.y, speciesIndex, species.attractionToSelf, species.attractionToOthers);
    float right = trailMap.sampleOptimized(rightPos.x, rightPos.y, speciesIndex, species.attractionToSelf, species.attractionToOthers);

    // use spatial grid for ultra-fast neighbor detection
    // spatial grid divides world into cells so we only check nearby agents, not all agents
    std::vector<size_t> nearbyAgents = spatialGrid.getNearbyAgents(position.x, position.y, sensorDist * 2.0f);

    // aggregates for boids-like emergent behavior (Reynolds 1987):
    // alignSum: running total of neighbor velocity directions for flocking alignment
    // centerSum: running total of neighbor positions for cohesion (moving toward group center)
    // separationSum: running total of "push away" vectors to avoid crowding
    sf::Vector2f alignSum(0.f, 0.f);
    sf::Vector2f centerSum(0.f, 0.f);
    sf::Vector2f separationSum(0.f, 0.f);
    int neighborCount = 0;
    int sameSpeciesCount = 0;

    // accumulate boids terms for each nearby agent
    // this loop computes interaction strength and adds to alignment/cohesion/separation sums
    for (size_t agentIdx : nearbyAgents)
    {
        if (agentIdx >= allAgents.size())
            continue;
        const Agent &other = allAgents[agentIdx];
        if (&other == this)
            continue; // skip self

        float dx = other.position.x - position.x;
        float dy = other.position.y - position.y;
        float distance = std::sqrt(dx * dx + dy * dy);

        if (distance < sensorDist * 2.0f && distance > 1e-3f)
        {
            // base interaction mapped into sensors
            float interaction = calculateAgentInteraction(other, distance, species);

            float angleToOther = std::atan2(dy, dx);
            float angleDiff = angleToOther - angle;
            // normalize angle difference to [-π, π]
            while (angleDiff > M_PIf)
                angleDiff -= 2.0f * M_PIf;
            while (angleDiff < -M_PIf)
                angleDiff += 2.0f * M_PIf;

            if (std::abs(angleDiff) < sensorAngleRad * 0.5f)
                front += interaction;
            else if (angleDiff < 0)
                left += interaction;
            else
                right += interaction;

            // accumulate boids terms
            neighborCount++;
            bool isSameSpecies = (other.speciesIndex == speciesIndex);
            if (isSameSpecies)
                sameSpeciesCount++;

            // alignment: sum normalized neighbor velocities (prefer same species)
            sf::Vector2f ov = other.velocity;
            float ovLen = std::sqrt(ov.x * ov.x + ov.y * ov.y);
            if (ovLen > 1e-3f)
            {
                sf::Vector2f ovn = sf::Vector2f(ov.x / ovLen, ov.y / ovLen);
                float speciesWeight = isSameSpecies ? 1.0f : 0.5f;
                alignSum += ovn * speciesWeight;
            }

            // cohesion: sum neighbor positions (with same-species boost for territorial types)
            float speciesWeight = isSameSpecies ? (1.0f * species.sameSpeciesCohesionBoost) : 0.6f;
            centerSum += sf::Vector2f(other.position.x * speciesWeight, other.position.y * speciesWeight);

            // separation: repel strongly inside separation radius
            // for territorial/loner species, only separate from OTHER species
            float sepR = sepRad;
            bool shouldSeparate = (distance < sepR) && (species.separateFromSameSpecies || !isSameSpecies);
            if (shouldSeparate)
            {
                float inv = 1.0f / distance;
                float strength = (sepR - distance) / sepR; // 0..1
                separationSum += sf::Vector2f(-dx * inv * strength, -dy * inv * strength);
            }
        }
    }

    // convert accumulated boids sums into sensor biases that affect turning
    if (neighborCount > 0)
    {
        // alignment direction: normalize the sum of neighbor velocities.
        // this gives the average heading of the flock - we'll want to match it.
        sf::Vector2f alignDir(0.f, 0.f);
        float aLen = std::sqrt(alignSum.x * alignSum.x + alignSum.y * alignSum.y);
        if (aLen > 1e-3f)
            alignDir = sf::Vector2f(alignSum.x / aLen, alignSum.y / aLen);

        // cohesion direction: compute center of mass of neighbors, then direction toward it.
        // dividing sum by count gives average position (center of mass).
        sf::Vector2f toCenter(0.f, 0.f);
        float invCount = 1.0f / static_cast<float>(neighborCount);
        toCenter.x = (centerSum.x * invCount) - position.x;
        toCenter.y = (centerSum.y * invCount) - position.y;
        float cLen = std::sqrt(toCenter.x * toCenter.x + toCenter.y * toCenter.y);
        sf::Vector2f cohDir(0.f, 0.f);
        if (cLen > 1e-3f)
            cohDir = sf::Vector2f(toCenter.x / cLen, toCenter.y / cLen);

        // separation direction: normalize the accumulated "push away" vectors
        sf::Vector2f sepDir(0.f, 0.f);
        float sLen = std::sqrt(separationSum.x * separationSum.x + separationSum.y * separationSum.y);
        if (sLen > 1e-3f)
            sepDir = sf::Vector2f(separationSum.x / sLen, separationSum.y / sLen);

        // combine all three boids rules into a single desired heading.
        // each direction is scaled by its respective weight (alignW, cohW, sepW).
        // the result is a vector pointing in the "ideal" direction to flock properly.
        sf::Vector2f desired = alignDir * alignW +
                               cohDir * cohW +
                               sepDir * sepW;

        float dLen = std::sqrt(desired.x * desired.x + desired.y * desired.y);
        if (dLen > 1e-3f)
        {
            // quorum factor: flocking influence scales with neighbor count up to threshold.
            // more neighbors = stronger social influence on direction choice.
            float quorumFactor = std::min(1.5f, static_cast<float>(neighborCount) / std::max(1.0f, species.quorumThreshold));
            float desiredAngle = std::atan2(desired.y, desired.x);
            float ang = desiredAngle - angle;
            while (ang > M_PIf)
                ang -= 2.0f * M_PIf;
            while (ang < -M_PIf)
                ang += 2.0f * M_PIf;

            // project the desired boids heading onto the 3-sensor array.
            // cosv > 0 means desired is ahead -> boost front sensor.
            // sinv < 0 means desired is to the left -> boost left sensor.
            // sinv > 0 means desired is to the right -> boost right sensor.
            // this translates the abstract "desired direction" into sensor weights
            // that the existing chemotaxis turning logic can use.
            float magnitude = std::min(1.0f, dLen) * quorumFactor;
            float cosv = std::cos(ang);
            float sinv = std::sin(ang);
            front += std::max(0.0f, cosv) * magnitude;
            if (sinv < 0)
                left += -sinv * magnitude;
            else
                right += sinv * magnitude;
        }

        // quorum straight-ahead stabilization
        if (static_cast<float>(neighborCount) >= species.quorumThreshold)
        {
            front += species.quorumTurnBias;
            // slightly damp lateral bias when in quorum
            left *= 0.98f;
            right *= 0.98f;
        }

        // energy gain from being in a crowd (capped at quorum)
        float crowd = std::min(static_cast<float>(neighborCount), species.quorumThreshold);
        energy += crowd * species.energyGainPerNeighbor;
    }

    // mark used to silence compiler warning in some configurations
    (void)sameSpeciesCount;

    // internal oscillator to induce waves/spirals
    if (oscStr > 0.0f)
    {
        float oscPhase = stateTimer * oscHz * 2.0f * M_PIf;
        float osc = std::sin(oscPhase) * oscStr;
        left += -osc;
        right += osc;
    }

    // apply species-specific turning logic with optimized branching
    switch (speciesIndex % 8) // support up to 8 species efficiently
    {
    case 0:
        applyRedBullyTurning(front, left, right, species);
        break;
    case 1:
        applyBlueCooperativeTurning(front, left, right, species);
        break;
    case 2:
        applyGreenAvoidanceTurning(front, left, right, species);
        break;
    case 3:
        applyAlienQuantumTurning(front, left, right, species);
        break;
    case 4:
        applyOrderEnforcerTurning(front, left, right, species);
        break;
    case 5:
        applyParasiticHuntingTurning(front, left, right, species);
        break;
    case 6:
        applyDemonicDestructionTurning(front, left, right, species);
        break;
    case 7:
        applyDevourerConsumptionTurning(front, left, right, species);
        break;
    default:
        applyRedBullyTurning(front, left, right, species);
        break;
    }
}

void Agent::depositOptimized(OptimizedTrailMap &trailMap, const SimulationSettings &settings)
{
    if (settings.speciesSettings.empty())
        return;
    const auto &species = settings.speciesSettings[std::min(speciesIndex, static_cast<int>(settings.speciesSettings.size()) - 1)];

    int width = settings.width;
    int height = settings.height;
    int centerX = static_cast<int>(position.x);
    int centerY = static_cast<int>(position.y);

    auto wrap = [](int v, int max)
    {
        while (v < 0)
            v += max;
        while (v >= max)
            v -= max;
        return v;
    };

    // helper: deposit at a point using anisotropic splat if enabled
    auto depositAt = [&](int px, int py, float amount)
    {
        if (amount <= 0.0f)
            return;
        if (settings.anisotropicSplatsEnabled)
        {
            float sigmaPar = settings.splatSigmaParallel * (1.0f + 0.15f * (species.behaviorIntensity - 2));
            float sigmaPerp = settings.splatSigmaPerp * (1.0f + 0.10f * (species.behaviorIntensity - 2));
            trailMap.depositAnisotropic(px, py, angle, sigmaPar, sigmaPerp, amount * settings.splatIntensityScale, speciesIndex, width, height);
        }
        else
        {
            trailMap.depositOptimized(px, py, amount, speciesIndex);
        }
    };

    // if anisotropic splats (directional deposits) are enabled with multiple species,
    // use lightweight single-point deposition to avoid O(n * neighbors) overhead.
    // anisotropic splats already spread out naturally along the heading direction.
    bool multiSpecies = settings.speciesSettings.size() > 1;
    if (settings.anisotropicSplatsEnabled && multiSpecies)
    {
        depositAt(centerX, centerY, settings.trailWeight * species.behaviorIntensity);
    }
    else
    {
        // use species-specific deposition patterns.
        // each species leaves a distinctive "fingerprint" in the trail map:
        // - red (0): thick territorial trails
        // - blue (1): cross-pattern networks
        // - green (2): sparse trails (loner)
        // - yellow (3): quantum tunneling patterns
        // - magenta (4): radial ordered patterns
        // - black (5): parasitic consumption (erases others)
        // - crimson (6): destructive fire patterns
        // - white (7): protective enhancement aura
        switch (speciesIndex % 8)
        {
        case 0: // red: territorial thick trails
            depositAt(centerX, centerY, settings.trailWeight * species.behaviorIntensity);
            if (species.behaviorIntensity >= 3)
            {
                // thick arterial patterns for high intensity
                for (int dx = -1; dx <= 1; dx++)
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        int wx = wrap(centerX + dx, width);
                        int wy = wrap(centerY + dy, height);
                        depositAt(wx, wy, settings.trailWeight * 0.7f);
                    }
            }
            break;

        case 1: // blue: network cooperation patterns
            depositAt(centerX, centerY, settings.trailWeight * species.behaviorIntensity);
            if (species.behaviorIntensity >= 2)
            {
                // cross pattern for network building
                depositAt(wrap(centerX + 1, width), centerY, settings.trailWeight * 0.5f);
                depositAt(wrap(centerX - 1, width), centerY, settings.trailWeight * 0.5f);
                depositAt(centerX, wrap(centerY + 1, height), settings.trailWeight * 0.5f);
                depositAt(centerX, wrap(centerY - 1, height), settings.trailWeight * 0.5f);
            }
            break;

        case 2:                    // green: sparse avoidant trails
            // nomadic loners deposit only occasionally to leave minimal evidence.
            // stateTimer accumulates each frame; deposit only when > 0.5 seconds.
            if (stateTimer > 0.5f)
            {
                depositAt(centerX, centerY, settings.trailWeight * species.behaviorIntensity * 0.6f);
                stateTimer = 0.0f; // reset timer after deposit
            }
            break;

        case 3: // yellow: quantum alien patterns
        {
            // alien deposits oscillate in intensity based on a sinusoidal phase.
            // creates pulsing trails that wax and wane mysteriously.
            float quantumPhase = stateTimer * 2.0f * M_PIf;
            float intensity = settings.trailWeight * species.behaviorIntensity * (0.5f + 0.5f * std::sin(quantumPhase));
            depositAt(centerX, centerY, intensity);

            // quantum tunneling: every 50 timer ticks, deposit at a distant location.
            // simulates the alien "teleporting" its trail across space.
            if (static_cast<int>(stateTimer * 100.0f) % 50 == 0)
            {
                int quantumX = wrap(centerX + static_cast<int>(20.0f * std::cos(quantumPhase)), width);
                int quantumY = wrap(centerY + static_cast<int>(20.0f * std::sin(quantumPhase)), height);
                depositAt(quantumX, quantumY, intensity * 0.3f);
            }
        }
        break;

        case 4: // magenta: radial order patterns
        {
            // order enforcer creates a rotating "arm" of trail deposits.
            // orderAngle advances over time, creating spiral/radial geometric patterns.
            depositAt(centerX, centerY, settings.trailWeight * species.behaviorIntensity);
            float orderAngle = angle + stateTimer;
            // number of radial deposits scales with behavior intensity (1-5 typically)
            for (int r = 1; r <= species.behaviorIntensity; r++)
            {
                int x = wrap(centerX + static_cast<int>(r * std::cos(orderAngle)), width);
                int y = wrap(centerY + static_cast<int>(r * std::sin(orderAngle)), height);
                depositAt(x, y, settings.trailWeight * 0.4f);
            }
        }
        break;

        case 5: // black: parasititic consumption (reduces other trails)
            // parasites deposit their own trail normally...
            depositAt(centerX, centerY, settings.trailWeight * species.behaviorIntensity);
            // ...but also ERASE other species' trails at this location.
            // this creates "holes" in competing trail networks.
            for (int otherSpecies = 0; otherSpecies < static_cast<int>(settings.speciesSettings.size()); otherSpecies++)
            {
                if (otherSpecies != speciesIndex)
                {
                    trailMap.eraseOptimized(centerX, centerY, settings.trailWeight * 0.3f, otherSpecies);
                }
            }
            break;

        case 6: // crimson: destructive trails
            // death-bringer deposits extra-strong own trail (1.5x)...
            depositAt(centerX, centerY, settings.trailWeight * species.behaviorIntensity * 1.5f);
            // ...and erases neighboring cells of the "next" species in a 3x3 pattern.
            // this creates a destructive wake that damages other species' networks.
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++)
                    if (dx != 0 || dy != 0)
                    {
                        int wx = wrap(centerX + dx, width);
                        int wy = wrap(centerY + dy, height);
                        trailMap.eraseOptimized(wx, wy, settings.trailWeight * 0.2f, (speciesIndex + 1) % static_cast<int>(settings.speciesSettings.size()));
                    }
            break;

        case 7: // white: protective enhancement
            // guardians deposit their own trail normally...
            depositAt(centerX, centerY, settings.trailWeight * species.behaviorIntensity);
            // ...and also ENHANCE nearby friendly trails in a 5x5 area.
            // this creates a protective "aura" that strengthens allied territory.
            for (int dx = -2; dx <= 2; dx++)
                for (int dy = -2; dy <= 2; dy++)
                    if (dx != 0 || dy != 0)
                    {
                        int wx = wrap(centerX + dx, width);
                        int wy = wrap(centerY + dy, height);
                        trailMap.enhanceOptimized(wx, wy, settings.trailWeight * 0.1f, speciesIndex);
                    }
            break;

        default:
            depositAt(centerX, centerY, settings.trailWeight);
            break;
        }
    }

    // update state timer for time-based behaviors
    stateTimer += 0.016f; // assume ~60 fps
    if (stateTimer > 2.0f * M_PIf)
        stateTimer = 0.0f; // reset every cycle
}

float Agent::calculateAgentInteraction(const Agent &other, float distance, const SimulationSettings::SpeciesSettings &species) const
{
    // simple interaction model based on distance and species compatibility
    if (distance > species.sensorOffsetDistance * 2.0f)
        return 0.0f;

    // stronger interaction for closer agents
    float proximityFactor = 1.0f - (distance / (species.sensorOffsetDistance * 2.0f));

    // species-specific interaction strength
    float baseInteraction = 0.1f;
    if (this->speciesIndex == other.speciesIndex)
    {
        // same species - positive interaction
        baseInteraction *= 1.5f;
    }
    else
    {
        // different species - weaker interaction
        baseInteraction *= 0.5f;
    }

    return baseInteraction * proximityFactor;
}

// food pellet goal-seeking system
// this creates organic pathfinding behavior where agents actively seek or avoid pellets.
// each species has different "personality" reactions to attractive/repulsive pellets.

sf::Vector2f Agent::calculatePelletForce(const std::vector<FoodPellet> &foodPellets, const SimulationSettings &settings) const
{
    sf::Vector2f totalForce(0.0f, 0.0f);

    if (foodPellets.empty() || speciesIndex >= static_cast<int>(settings.speciesSettings.size()))
        return totalForce;

    const auto &species = settings.speciesSettings[speciesIndex];

    // accumulate force from all pellets (no distance limit - global influence)
    for (const auto &pellet : foodPellets)
    {
        sf::Vector2f toPellet = pellet.position - position;
        float distance = std::sqrt(toPellet.x * toPellet.x + toPellet.y * toPellet.y);

        // normalize direction vector (unit vector toward pellet)
        sf::Vector2f direction = toPellet / distance;

        // inverse-distance force: closer pellets have stronger pull.
        // the +10 prevents division by zero and softens extreme close-range forces.
        float forceStrength = std::abs(pellet.strength) / (distance * 0.1f + 10.0f);

        // species-specific reactions: each species has a "personality".
        // isAttracted = true for food pellets, false for repulsion pellets.
        float speciesMultiplier = 3.0f;
        bool isAttracted = pellet.strength > 0;

        // species-specific multipliers for universal pellets (type 0).
        // each species reacts differently to food vs danger:
        // - red (territorial): cautious, stronger flee response
        // - blue (cooperative): eager for food, ignores danger
        // - green (loner): mostly indifferent, slight caution
        // - yellow (alien): truly random reactions each frame
        // - magenta (order): consistent balanced response
        // - black (parasitic): very hungry, ignores threats
        // - crimson (death): moderate hunger, strong fear
        // - white (guardian): perfectly balanced
        if (pellet.pelletType == 0)
        {
            if (speciesIndex == 0)
                speciesMultiplier = isAttracted ? 0.8f : 1.5f;
            else if (speciesIndex == 1)
                speciesMultiplier = isAttracted ? 1.5f : 0.5f;
            else if (speciesIndex == 2)
                speciesMultiplier = isAttracted ? 0.6f : 1.0f;
            else if (speciesIndex == 3)
            {
                // alien: roll a random multiplier each time for unpredictable behavior
                static thread_local std::mt19937 gen(std::random_device{}());
                static thread_local std::uniform_real_distribution<float> chaos(0.2f, 2.0f);
                speciesMultiplier = chaos(gen);
            }
            else if (speciesIndex == 4)
                speciesMultiplier = isAttracted ? 1.2f : 1.2f;
            else if (speciesIndex == 5)
                speciesMultiplier = isAttracted ? 2.0f : 0.3f;
            else if (speciesIndex == 6)
                speciesMultiplier = isAttracted ? 1.0f : 2.5f;
            else if (speciesIndex == 7)
                speciesMultiplier = 1.0f;
        }
        else if (pellet.pelletType == speciesIndex + 1) // species-specific pellet
        {
            speciesMultiplier = 3.0f; // very strong reaction to species specific pellets
        }
        else if (pellet.pelletType > 0 && pellet.pelletType != speciesIndex + 1) // other species pellet
        {
            speciesMultiplier = 0.3f; // weak reaction to other species' pellets
        }

        // apply attraction or repulsion
        float finalForce = forceStrength * speciesMultiplier;
        if (pellet.strength < 0) // repulsive pellet
            direction = -direction;

        totalForce += direction * finalForce;
    }

    // clamp the total force to prevent insane speeds
    float maxForce = species.moveSpeed * 2.0f;
    float forceMagnitude = std::sqrt(totalForce.x * totalForce.x + totalForce.y * totalForce.y);
    if (forceMagnitude > maxForce)
    {
        totalForce = (totalForce / forceMagnitude) * maxForce;
    }

    return totalForce;
}

bool Agent::shouldSeekPellets(const SimulationSettings &settings) const
{
    // always seek pellets - this creates the strong goal-oriented behavior
    return true;
}

void Agent::moveWithPelletSeeking(const SimulationSettings &settings, const std::vector<FoodPellet> &foodPellets)
{
    if (speciesIndex >= static_cast<int>(settings.speciesSettings.size()))
        return;

    const auto &species = settings.speciesSettings[speciesIndex];

    // calculate gentle pellet influence
    sf::Vector2f pelletForce = calculatePelletForce(foodPellets, settings);
    float pelletInfluence = std::sqrt(pelletForce.x * pelletForce.x + pelletForce.y * pelletForce.y);

    // do normal movement first
    move(settings);

    // if there's pellet influence, gently adjust direction
    if (pelletInfluence > 0.01f)
    {
        // calculate desired direction toward/away from pellets
        float pelletAngle = std::atan2(pelletForce.y, pelletForce.x);

        // gentle turning toward pellets - blend with current angle
        float angleDiff = pelletAngle - angle;

        // normalize angle difference to [-π, π]
        while (angleDiff > M_PIf)
            angleDiff -= 2.0f * M_PIf;
        while (angleDiff < -M_PIf)
            angleDiff += 2.0f * M_PIf;

        // very gentle turning influence (only 15% toward pellets)
        float maxTurnRate = species.turnSpeed * M_PIf / 180.0f;
        float pelletTurnInfluence = std::min(pelletInfluence / (species.moveSpeed * 5.0f), 0.3f); // much weaker influence
        float turnAmount = angleDiff * pelletTurnInfluence * 0.5f;                                //  50% influence
        // float turnamount = anglediff * pelletturninfluence * 0.15f;                               // only 15% influence

        turnAmount = std::clamp(turnAmount, -maxTurnRate, maxTurnRate); // normal turn rate limits

        angle += turnAmount;

        // apply heading inertia/compliance after pellet-induced turning as well
        applyInertia(settings);

        // very slight speed boost when moving toward attractive pellets (only 10% max)
        if (pelletInfluence > species.moveSpeed * 0.5f && pelletForce.x * std::cos(angle) + pelletForce.y * std::sin(angle) > 0)
        {
            sf::Vector2f gentleMovement(std::cos(angle) * species.moveSpeed * 0.3f, std::sin(angle) * species.moveSpeed * 0.3f);
            // sf::vector2f gentlemovement(std::cos(angle) * species.movespeed * 0.1f, std::sin(angle) * species.movespeed * 0.1f);

            position += gentleMovement;
        }
    }

    // agent stays in bounds
    wrapPosition(settings.width, settings.height);
}
