#undef setPixel
#include "TrailMap.h"
#include <SFML/Graphics/Image.hpp>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <random>
#include <iostream>

TrailMap::TrailMap(int width, int height, int numSpecies)
    : width_(width), height_(height), numSpecies_(numSpecies)
{
    size_t size = width_ * height_;

    // create one trail map per species
    for (int i = 0; i < numSpecies_; ++i)
    {
        speciesData_.push_back(std::make_unique<float[]>(size));
        tempSpeciesData_.push_back(std::make_unique<float[]>(size));
    }

    clear();
}

TrailMap::~TrailMap() = default;

// overload for compatibility with different parameter orders
void TrailMap::deposit(int species, int x, int y, float amount)
{
    deposit(x, y, amount, species);
}
void TrailMap::clear()
{
    for (int species = 0; species < numSpecies_; ++species)
    {
        std::memset(speciesData_[species].get(), 0, width_ * height_ * sizeof(float));
    }
}

// optimized: inline bounds check assumes valid input in hot path
void TrailMap::deposit(int x, int y, float amount, int species)
{
    // defensive bounds check to prevent crash
    if (species < 0 || species >= numSpecies_ || x < 0 || x >= width_ || y < 0 || y >= height_)
        return;
    speciesData_[species][y * width_ + x] += amount;
}

// optimized: inline bounds check, avoid function call overhead, assume valid input in hot path
float TrailMap::sample(int x, int y, int species) const
{
    if (species < 0 || species >= numSpecies_ || x < 0 || x >= width_ || y < 0 || y >= height_)
        return 0.0f;
    return speciesData_[species][y * width_ + x];
}

// eat trail at position return amount consumed (removes from trail)
float TrailMap::eat(int x, int y, int species, float maxBite)
{
    if (species < 0 || species >= numSpecies_ || x < 0 || x >= width_ || y < 0 || y >= height_)
        return 0.0f;
    
    int idx = y * width_ + x;
    float available = speciesData_[species][idx];
    float eaten = std::min(available, maxBite);
    speciesData_[species][idx] -= eaten;  // CONSUME the trail!
    return eaten;
}

// food economy: eat from other species trails (for predators/bullies)
float TrailMap::eatAnySpecies(int x, int y, int excludeSpecies, float maxBite)
{
    if (x < 0 || x >= width_ || y < 0 || y >= height_)
        return 0.0f;
    
    float totalEaten = 0.0f;
    int idx = y * width_ + x;
    
    for (int s = 0; s < numSpecies_; ++s)
    {
        if (s == excludeSpecies) continue;  // dont eat own trail
        
        float available = speciesData_[s][idx];
        float bite = std::min(available, maxBite - totalEaten);
        if (bite > 0.0f)
        {
            speciesData_[s][idx] -= bite;
            totalEaten += bite;
        }
        if (totalEaten >= maxBite) break;
    }
    return totalEaten;
}

// multi species interaction sampling attempt to enhance complex emergent behaviors
float TrailMap::sampleSpeciesInteraction(int x, int y, int species,
                                         float attractionToSelf,
                                         float attractionToOthers) const
{
    if (!isValidCoordinate(x, y) || species < 0 || species >= numSpecies_)
        return 0.0f;

    float totalAttraction = 0.0f;
    float ownTrail = speciesData_[species][getIndex(x, y)];

    // sample own species trail with self attraction
    totalAttraction += ownTrail * attractionToSelf;

    // enhanced multi species interactions with emergent behaviors
    for (int otherSpecies = 0; otherSpecies < numSpecies_; ++otherSpecies)
    {
        if (otherSpecies != species)
        {
            float otherTrail = speciesData_[otherSpecies][getIndex(x, y)];

            // basic attraction/repulsion to other species
            float basicInteraction = otherTrail * attractionToOthers;

            // enhanced interactions based on trail density combinations
            if (otherTrail > 0.1f && ownTrail > 0.1f)
            {
                // when trails overlap, create competition or cooperation effects
                if (attractionToOthers > 0.5f)
                {
                    // cooperative species: synergy effect when trails meet
                    basicInteraction *= 1.3f;
                }
                else if (attractionToOthers < -0.2f)
                {
                    // avoidant species: stronger repulsion when trails are dense
                    basicInteraction *= 1.5f;
                }
                else if (attractionToSelf > 1.2f && attractionToOthers < 0.2f)
                {
                    // aggressive species: compete for territory by weakening others
                    basicInteraction -= otherTrail * 0.3f;
                }
            }

            // territory competition: stronger trails suppress weaker ones
            if (ownTrail > otherTrail * 2.0f && attractionToSelf > 1.0f)
            {
                // strong own presence suppresses others for territorial species
                basicInteraction *= 0.7f;
            }
            else if (otherTrail > ownTrail * 2.0f && attractionToOthers < 0.0f)
            {
                // being overwhelmed by others should increase avoidance
                basicInteraction *= 1.4f;
            }

            totalAttraction += basicInteraction;
        }
    }

    // adds small amount of noise to prevent perfectly regular patterns
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_real_distribution<float> noise(-0.01f, 0.01f);
    totalAttraction += noise(gen);

    // dont clamp to 0 - negative values are meaningful for avoidance
    //sSpecies with negative attractionToOthers need negative weights to flee
    return totalAttraction;
}

void TrailMap::diffuse(float diffuseRate)
{
    // 3x3 gaussian like kernel for diffusion
    constexpr float kernel[3][3] = {
        {0.0625f, 0.125f, 0.0625f},
        {0.125f, 0.25f, 0.125f},
        {0.0625f, 0.125f, 0.0625f}};

    // apply diffusion to each species channel
    for (int species = 0; species < numSpecies_; ++species)
    {
        float *data = speciesData_[species].get();
        float *tempData = tempSpeciesData_[species].get();

        // apply diffusion (skip border pixels to avoid bounds checking)
        for (int y = 1; y < height_ - 1; ++y)
        {
            for (int x = 1; x < width_ - 1; ++x)
            {
                float sum = 0.0f;

                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        int nx = x + dx;
                        int ny = y + dy;
                        sum += data[getIndex(nx, ny)] * kernel[dy + 1][dx + 1];
                    }
                }

                tempData[getIndex(x, y)] = sum;
            }
        }

        // blend original with the diffused result
        for (int i = 0; i < width_ * height_; ++i)
        {
            data[i] = data[i] * (1.0f - diffuseRate) + tempData[i] * diffuseRate;
        }
    }
}

void TrailMap::decay(float decayRate)
{
    float decayFactor = 1.0f - decayRate;
    for (int species = 0; species < numSpecies_; ++species)
    {
        float *data = speciesData_[species].get();
        for (int i = 0; i < width_ * height_; ++i)
        {
            data[i] *= decayFactor;
        }
    }
}

void TrailMap::applyBlur()
{
    // more noticeable blur for smoother trails
    const float blurStrength = 0.4f; // stronger effect so the change is visible

    for (int species = 0; species < numSpecies_; ++species)
    {
        float *data = speciesData_[species].get();
        float *tempData = tempSpeciesData_[species].get();

        // copy original data first
        std::memcpy(tempData, data, width_ * height_ * sizeof(float));

        for (int y = 1; y < height_ - 1; ++y)
        {
            for (int x = 1; x < width_ - 1; ++x)
            {
                int idx = getIndex(x, y);
                float original = data[idx];

                // applies the blur to any visible trail (lower threshold so more pixels are affected)
                if (original > 0.01f)
                {
                    // and weighted 3x3 blur for smoother effect
                    float sum = 0.0f;
                    float weightSum = 0.0f;

                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            float weight = (dx == 0 && dy == 0) ? 4.0f : 1.0f; // center weight
                            sum += data[getIndex(x + dx, y + dy)] * weight;
                            weightSum += weight;
                        }
                    }
                    float averaged = sum / weightSum;

                    // blend original with blurred for visible smoothing
                    tempData[idx] = original * (1.0f - blurStrength) + averaged * blurStrength;
                }
            }
        }

        // swap buffers for this species
        std::swap(speciesData_[species], tempSpeciesData_[species]);
    }
}

void TrailMap::updateTexture(sf::Image &image, float displayThreshold, const sf::Color &baseColor) const
#undef setPixel
{
    if (numSpecies_ == 0)
        return;

    // for single species it just uses the first channel
    float *data = speciesData_[0].get();

    // find maximum value for normalization
    float maxVal = 0.0f;
    for (int i = 0; i < width_ * height_; ++i)
    {
        maxVal = std::max(maxVal, data[i]);
    }

    if (maxVal <= 0.0f)
        return;

    // update image pixels with crisp, high contrast rendering
    for (int y = 0; y < height_; ++y)
    {
        for (int x = 0; x < width_; ++x)
        {
            float rawValue = data[getIndex(x, y)];
            float normalizedValue = rawValue / maxVal;

            sf::Color color;
            if (normalizedValue < displayThreshold)
            {
                color = sf::Color::Black;
            }
            else
            {
                // high contrast mapping for sharp, detailed trails
                float intensity = std::pow(normalizedValue, 0.8f); // enhance contrast

                // preserve accurate species colors
                float r = (baseColor.r / 255.0f) * intensity;
                float g = (baseColor.g / 255.0f) * intensity;
                float b = (baseColor.b / 255.0f) * intensity;

                // only brighten the core areas (very high intensity)
                if (intensity > 0.85f)
                {
                    float coreBright = (intensity - 0.85f) * 2.0f; // only brighten top 15%
                    r = std::min(1.0f, r + coreBright * 0.3f);
                    g = std::min(1.0f, g + coreBright * 0.3f);
                    b = std::min(1.0f, b + coreBright * 0.3f);
                }

                color.r = static_cast<unsigned char>(r * 255.0f);
                color.g = static_cast<unsigned char>(g * 255.0f);
                color.b = static_cast<unsigned char>(b * 255.0f);
                color.a = 255;
            }

            image.setPixel(sf::Vector2u(static_cast<unsigned int>(x), static_cast<unsigned int>(y)), color);
        }
    }
}

// multi species texture update 
void TrailMap::updateMultiSpeciesTexture(sf::Image &image, float displayThreshold,
                                         const std::vector<sf::Color> &speciesColors) const
{
    // Find max value PER species for independent normalization (so each species is equally visible)
    std::vector<float> maxValPerSpecies(numSpecies_, 0.0f);
    for (int species = 0; species < numSpecies_; ++species)
    {
        const float *data = speciesData_[species].get();
        for (int i = 0; i < width_ * height_; ++i)
        {
            maxValPerSpecies[species] = std::max(maxValPerSpecies[species], data[i]);
        }
    }
    
    // debug: for printing max values per species every 120 frames
    static int debugCounter = 0;
    if (++debugCounter % 120 == 0) {
        std::cout << "[TRAIL DEBUG] Max per channel: ";
        for (int s = 0; s < numSpecies_ && s < 8; ++s) {
            std::cout << "ch" << s << "=" << maxValPerSpecies[s] << " ";
        }
        std::cout << std::endl;
    }

    // renders each pixel with proper species color blending
    for (int y = 0; y < height_; ++y)
    {
        for (int x = 0; x < width_; ++x)
        {
            int idx = getIndex(x, y);
            float totalR = 0.0f, totalG = 0.0f, totalB = 0.0f;
            bool hasTrail = false;

            // calculate contribution from each species
            for (int species = 0; species < numSpecies_; ++species)
            {
                if (species >= static_cast<int>(speciesColors.size()))
                    continue;

                // skipping if this species has no trails at all
                if (maxValPerSpecies[species] <= 0.0f)
                    continue;

                float rawValue = speciesData_[species][idx];
                float normalizedVal = rawValue / maxValPerSpecies[species];  // normalize per species
                
                // use display threshold like single species mode
                if (normalizedVal > displayThreshold)
                {
                    hasTrail = true;
                    const sf::Color &speciesColor = speciesColors[species];

                    // use same intensity curve as single species for consistency
                    float intensity = std::pow(normalizedVal, 0.8f);

                    // accurate species color reproduction
                    float r = (speciesColor.r / 255.0f) * intensity;
                    float g = (speciesColor.g / 255.0f) * intensity;
                    float b = (speciesColor.b / 255.0f) * intensity;

                    // additive blending for species interactions
                    totalR += r;
                    totalG += g;
                    totalB += b;
                }
            }

            sf::Color finalColor = sf::Color::Black;

            if (hasTrail)
            {
                // only compress if colors are severely oversaturated
                float maxComponent = std::max({totalR, totalG, totalB});
                if (maxComponent > 1.2f)
                {
                    float scale = 1.2f / maxComponent;
                    totalR *= scale;
                    totalG *= scale;
                    totalB *= scale;
                }

                finalColor.r = static_cast<unsigned char>(std::min(255.0f, totalR * 255.0f));
                finalColor.g = static_cast<unsigned char>(std::min(255.0f, totalG * 255.0f));
                finalColor.b = static_cast<unsigned char>(std::min(255.0f, totalB * 255.0f));
            }

            finalColor.a = 255;
            image.setPixel(sf::Vector2u(static_cast<unsigned int>(x), static_cast<unsigned int>(y)), finalColor);
        }
    }
}

// ============================== GPU data transfer ==============================

std::vector<float> TrailMap::getAllData() const
{
    std::vector<float> allData;
    allData.reserve(width_ * height_ * numSpecies_);

    // flatten all species data into a single vector
    // format: [species0_data..., species1_data..., speciesn_data...]
    for (int species = 0; species < numSpecies_; ++species)
    {
        const float *speciesPtr = speciesData_[species].get();
        for (int i = 0; i < width_ * height_; ++i)
        {
            allData.push_back(speciesPtr[i]);
        }
    }

    return allData;
}

void TrailMap::setData(const std::vector<float> &data, int width, int height)
{
    if (width != width_ || height != height_)
    {
        // return if resize not supported during GPU sync
        return;
    }

    if (data.size() != static_cast<size_t>(width_ * height_ * numSpecies_))
    {
        // return if data size mismatch
        return;
    }

    // unflatten the data back into species arrays
    // format: [species0_data..., species1_data..., speciesn_data...]
    size_t offset = 0;
    for (int species = 0; species < numSpecies_; ++species)
    {
        float *speciesPtr = speciesData_[species].get();
        for (int i = 0; i < width_ * height_; ++i)
        {
            speciesPtr[i] = data[offset++];
        }
    }
}
bool TrailMap::isValidCoordinate(int x, int y) const
{
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}

int TrailMap::getIndex(int x, int y) const
{
    return y * width_ + x;
}
