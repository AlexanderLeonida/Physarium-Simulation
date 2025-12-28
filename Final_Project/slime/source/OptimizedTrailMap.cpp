#include "OptimizedTrailMap.h"
#include "TrailMap.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <random>
#include <thread>
#include <future>
#include <iostream>
#include <cstdlib> // for posix_memalign

// initialize simd-aligned diffusion kernel
alignas(32) constexpr float OptimizedTrailMap::DIFFUSION_KERNEL[9];

OptimizedTrailMap::OptimizedTrailMap(int width, int height, int numSpecies)
    : width_(width), height_(height), numSpecies_(numSpecies)
{
    totalSize_ = width_ * height_;

    // allocate simd aligned memory for each species
    speciesData_.reserve(numSpecies_);
    tempSpeciesData_.reserve(numSpecies_);

    for (int i = 0; i < numSpecies_; ++i)
    {
        float *data = allocateAlignedFloat(totalSize_);
        float *tempData = allocateAlignedFloat(totalSize_);

        // defensive: if allocation fails, throw immediately
        if (!data || !tempData)
        {
            std::cerr << "FATAL: Failed to allocate aligned memory for OptimizedTrailMap (species " << i << ")" << std::endl;
            std::terminate();
        }

        // use a custom deleter that only frees if ptr is non null and never double frees
        speciesData_.emplace_back(data, [](float *ptr)
                                  {
            if (ptr) free(ptr); });
        tempSpeciesData_.emplace_back(tempData, [](float *ptr)
                                      {
            if (ptr) free(ptr); });
    }

    clear();

    std::cout << "OptimizedTrailMap initialized: "
              << width_ << "x" << height_ << " with " << numSpecies_ << " species" << std::endl;
    std::cout << "Memory usage: " << getMemoryUsage() / (1024 * 1024) << " MB" << std::endl;
}

OptimizedTrailMap::~OptimizedTrailMap() = default;

float *OptimizedTrailMap::allocateAlignedFloat(size_t count)
{
    // allocate extra space to for simd alignment
    size_t alignedCount = ((count + FLOATS_PER_SIMD - 1) / FLOATS_PER_SIMD) * FLOATS_PER_SIMD;

#ifdef __x86_64__
    return static_cast<float *>(_mm_malloc(alignedCount * sizeof(float), SIMD_ALIGNMENT));
#else
    // cross-platform aligned allocation
    void *ptr = nullptr;
    if (posix_memalign(&ptr, SIMD_ALIGNMENT, alignedCount * sizeof(float)) == 0)
    {
        return static_cast<float *>(ptr);
    }
    return nullptr;
#endif
}

void OptimizedTrailMap::freeAlignedFloat(float *ptr)
{
    if (ptr)
    {
#ifdef __x86_64__
        _mm_free(ptr);
#else
        free(ptr);
#endif
    }
}

void OptimizedTrailMap::clear()
{
    for (int species = 0; species < numSpecies_; ++species)
    {
        std::memset(speciesData_[species].get(), 0, totalSize_ * sizeof(float));
    }
}

float OptimizedTrailMap::sample(int x, int y, int species) const
{
    if (isValidCoordinate(x, y) && species >= 0 && species < numSpecies_)
    {
        return speciesData_[species][getIndex(x, y)];
    }
    return 0.0f;
}

void OptimizedTrailMap::deposit(int x, int y, float amount, int species)
{
    if (isValidCoordinate(x, y) && species >= 0 && species < numSpecies_)
    {
        speciesData_[species][getIndex(x, y)] += amount;
    }
}

void OptimizedTrailMap::deposit(int species, int x, int y, float amount)
{
    deposit(x, y, amount, species);
}

float OptimizedTrailMap::sampleSpeciesInteraction(int x, int y, int species,
                                                  float attractionToSelf,
                                                  float attractionToOthers) const
{
    if (!isValidCoordinate(x, y) || species < 0 || species >= numSpecies_)
        return 0.0f;

    float totalAttraction = 0.0f;
    int index = getIndex(x, y);

    // own species trail with self-attraction
    float ownTrail = speciesData_[species][index];
    totalAttraction += ownTrail * attractionToSelf;

    // other species trails
    for (int otherSpecies = 0; otherSpecies < numSpecies_; ++otherSpecies)
    {
        if (otherSpecies != species)
        {
            float otherTrail = speciesData_[otherSpecies][index];
            totalAttraction += otherTrail * attractionToOthers;
        }
    }

    return std::max(0.0f, totalAttraction);
}

void OptimizedTrailMap::diffuseSIMD(float diffuseRate)
{
    for (int species = 0; species < numSpecies_; ++species)
    {
        diffuseSpeciesSIMD(species, diffuseRate);
    }
    swapBuffers();
}

void OptimizedTrailMap::diffuseSpeciesSIMD(int species, float diffuseRate)
{
    float *source = speciesData_[species].get();
    float *dest = tempSpeciesData_[species].get();

#ifdef HAVE_AVX2
    // avx2 optimized version for x86
    const __m256 rate = _mm256_set1_ps(diffuseRate);
    const __m256 invRate = _mm256_set1_ps(1.0f - diffuseRate);

    // process interior pixels with simd (excluding borders for simplicity)
    for (int y = 1; y < height_ - 1; ++y)
    {
        for (int x = 1; x < width_ - FLOATS_PER_SIMD; x += FLOATS_PER_SIMD)
        {
            int idx = getIndex(x, y);
            __m256 center = _mm256_load_ps(&source[idx]);
            __m256 neighbors = _mm256_setzero_ps(); // simplified for now
            __m256 diffused = _mm256_fmadd_ps(center, invRate, _mm256_mul_ps(neighbors, rate));
            _mm256_store_ps(&dest[idx], diffused);
        }
    }

#elif defined(HAVE_NEON)
    // arm neon optimized version
    const float32x4_t rate = vdupq_n_f32(diffuseRate);
    const float32x4_t invRate = vdupq_n_f32(1.0f - diffuseRate);

    for (int y = 1; y < height_ - 1; ++y)
    {
        for (int x = 1; x < width_ - FLOATS_PER_SIMD; x += FLOATS_PER_SIMD)
        {
            int idx = getIndex(x, y);
            float32x4_t center = vld1q_f32(&source[idx]);
            float32x4_t neighbors = vdupq_n_f32(0.0f); // simplified for now
            float32x4_t diffused = vmlaq_f32(vmulq_f32(center, invRate), neighbors, rate);
            vst1q_f32(&dest[idx], diffused);
        }
    }

#else
    // scalar fallback - simple diffusion
    for (int y = 1; y < height_ - 1; ++y)
    {
        for (int x = 1; x < width_ - 1; ++x)
        {
            int idx = getIndex(x, y);
            float center = source[idx];
            float neighbors = (source[getIndex(x - 1, y)] + source[getIndex(x + 1, y)] +
                               source[getIndex(x, y - 1)] + source[getIndex(x, y + 1)]) *
                              0.25f;
            dest[idx] = center * (1.0f - diffuseRate) + neighbors * diffuseRate;
        }
    }
#endif

    // handle borders with scalar code
    for (int x = 0; x < width_; ++x)
    {
        dest[getIndex(x, 0)] = source[getIndex(x, 0)];
        dest[getIndex(x, height_ - 1)] = source[getIndex(x, height_ - 1)];
    }
    for (int y = 0; y < height_; ++y)
    {
        dest[getIndex(0, y)] = source[getIndex(0, y)];
        dest[getIndex(width_ - 1, y)] = source[getIndex(width_ - 1, y)];
    }
}

void OptimizedTrailMap::decaySIMD(float decayRate)
{
    const float decayFactor = 1.0f - decayRate;

    for (int species = 0; species < numSpecies_; ++species)
    {
        float *data = speciesData_[species].get();

#ifdef HAVE_AVX2
        // avx2 optimized version
        const __m256 decayVec = _mm256_set1_ps(decayFactor);
        size_t simdChunks = totalSize_ / FLOATS_PER_SIMD;
        for (size_t i = 0; i < simdChunks; ++i)
        {
            size_t idx = i * FLOATS_PER_SIMD;
            __m256 values = _mm256_load_ps(&data[idx]);
            __m256 decayed = _mm256_mul_ps(values, decayVec);
            _mm256_store_ps(&data[idx], decayed);
        }

        // handle remaining elements
        for (size_t i = simdChunks * FLOATS_PER_SIMD; i < totalSize_; ++i)
        {
            data[i] *= decayFactor;
        }

#elif defined(HAVE_NEON)
        // arm neon optimized version
        const float32x4_t decayVec = vdupq_n_f32(decayFactor);
        size_t simdChunks = totalSize_ / FLOATS_PER_SIMD;
        for (size_t i = 0; i < simdChunks; ++i)
        {
            size_t idx = i * FLOATS_PER_SIMD;
            float32x4_t values = vld1q_f32(&data[idx]);
            float32x4_t decayed = vmulq_f32(values, decayVec);
            vst1q_f32(&data[idx], decayed);
        }

        // handle remaining elements
        for (size_t i = simdChunks * FLOATS_PER_SIMD; i < totalSize_; ++i)
        {
            data[i] *= decayFactor;
        }

#else
        // scalar fallback
        for (size_t i = 0; i < totalSize_; ++i)
        {
            data[i] *= decayFactor;
        }
#endif
    }
}

void OptimizedTrailMap::parallelDiffuse(float diffuseRate, int numThreads)
{
    if (numThreads <= 0)
    {
        numThreads = std::thread::hardware_concurrency();
    }

    if (numThreads <= 1 || numSpecies_ <= 1)
    {
        diffuseSIMD(diffuseRate);
        return;
    }

    std::vector<std::future<void>> futures;
    int speciesPerThread = (numSpecies_ + numThreads - 1) / numThreads;

    for (int t = 0; t < numThreads; ++t)
    {
        int startSpecies = t * speciesPerThread;
        int endSpecies = std::min(startSpecies + speciesPerThread, numSpecies_);

        if (startSpecies >= numSpecies_)
            break;

        futures.emplace_back(std::async(std::launch::async, [=, this]()
                                        {
            for (int species = startSpecies; species < endSpecies; ++species) {
                diffuseSpeciesSIMD(species, diffuseRate);
            } }));
    }

    for (auto &future : futures)
    {
        future.wait();
    }

    swapBuffers();
}

void OptimizedTrailMap::parallelDecay(float decayRate, int numThreads)
{
    if (numThreads <= 0)
    {
        numThreads = std::thread::hardware_concurrency();
    }

    if (numThreads <= 1)
    {
        decaySIMD(decayRate);
        return;
    }

    const float decayFactor = 1.0f - decayRate;
    std::vector<std::future<void>> futures;

    size_t totalElements = totalSize_ * numSpecies_;
    size_t elementsPerThread = (totalElements + numThreads - 1) / numThreads;

    for (int t = 0; t < numThreads; ++t)
    {
        size_t startElement = t * elementsPerThread;
        size_t endElement = std::min(startElement + elementsPerThread, totalElements);

        if (startElement >= totalElements)
            break;

        futures.emplace_back(std::async(std::launch::async, [=, this]()
                                        {
            for (size_t globalIdx = startElement; globalIdx < endElement; globalIdx += FLOATS_PER_SIMD) {
                int species = globalIdx / totalSize_;
                size_t localIdx = globalIdx % totalSize_;
                
                if (species >= numSpecies_) break;
                
                float* data = speciesData_[species].get();
                size_t remaining = std::min(FLOATS_PER_SIMD, endElement - globalIdx);
                
                // use scalar code for cross platform compatibility*
                for (size_t i = 0; i < remaining && localIdx + i < totalSize_; ++i) {
                    data[localIdx + i] *= decayFactor;
                }
            } }));
    }

    for (auto &future : futures)
    {
        future.wait();
    }

    for (auto &future : futures)
    {
        future.wait();
    }
}

void OptimizedTrailMap::updateTexture(sf::Image &image, float displayThreshold, const sf::Color &baseColor) const
{
    // implementation similar to original trailmap but optimized for SoA layout
    if (numSpecies_ == 0)
        return;

    float *data = speciesData_[0].get();

    for (int y = 0; y < height_; ++y)
    {
        for (int x = 0; x < width_; ++x)
        {
            int idx = getIndex(x, y);
            float intensity = std::min(1.0f, data[idx] / displayThreshold);

            sf::Color pixelColor(
                static_cast<uint8_t>(baseColor.r * intensity),
                static_cast<uint8_t>(baseColor.g * intensity),
                static_cast<uint8_t>(baseColor.b * intensity),
                255);

            image.setPixel(sf::Vector2u(static_cast<unsigned int>(x), static_cast<unsigned int>(y)), pixelColor);
        }
    }
}

void OptimizedTrailMap::updateMultiSpeciesTexture(sf::Image &image, float displayThreshold,
                                                  const std::vector<sf::Color> &speciesColors) const
{
    for (int y = 0; y < height_; ++y)
    {
        for (int x = 0; x < width_; ++x)
        {
            sf::Color finalColor(0, 0, 0, 255);
            int idx = getIndex(x, y);

            for (int species = 0; species < numSpecies_ && species < static_cast<int>(speciesColors.size()); ++species)
            {
                float intensity = std::min(1.0f, speciesData_[species][idx] / displayThreshold);

                if (intensity > 0.01f)
                {
                    const sf::Color &speciesColor = speciesColors[species];
                    finalColor.r = std::min(255, static_cast<int>(finalColor.r + speciesColor.r * intensity));
                    finalColor.g = std::min(255, static_cast<int>(finalColor.g + speciesColor.g * intensity));
                    finalColor.b = std::min(255, static_cast<int>(finalColor.b + speciesColor.b * intensity));
                }
            }

            image.setPixel(sf::Vector2u(static_cast<unsigned int>(x), static_cast<unsigned int>(y)), finalColor);
        }
    }
}

size_t OptimizedTrailMap::getMemoryUsage() const
{
    size_t memoryPerSpecies = totalSize_ * sizeof(float) * 2;    // data + temp buffer
    size_t alignmentOverhead = SIMD_ALIGNMENT * numSpecies_ * 2; // alignment padding
    return memoryPerSpecies * numSpecies_ + alignmentOverhead;
}

void OptimizedTrailMap::printMemoryLayout() const
{
    std::cout << "=== OptimizedTrailMap Memory Layout ===" << std::endl;
    std::cout << "Dimensions: " << width_ << "x" << height_ << std::endl;
    std::cout << "Species: " << numSpecies_ << std::endl;
    std::cout << "Total elements per species: " << totalSize_ << std::endl;
    std::cout << "SIMD alignment: " << SIMD_ALIGNMENT << " bytes" << std::endl;
    std::cout << "Floats per SIMD register: " << FLOATS_PER_SIMD << std::endl;
    std::cout << "Total memory usage: " << getMemoryUsage() / (1024 * 1024) << " MB" << std::endl;
}

bool OptimizedTrailMap::isValidCoordinate(int x, int y) const
{
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}

int OptimizedTrailMap::getIndex(int x, int y) const
{
    return y * width_ + x;
}

void OptimizedTrailMap::swapBuffers()
{
    for (int species = 0; species < numSpecies_; ++species)
    {
        speciesData_[species].swap(tempSpeciesData_[species]);
    }
}

// optimized methods for agent integration

float OptimizedTrailMap::sampleOptimized(float x, float y, int species, float selfAttraction, float otherAttraction) const
{
    int ix = static_cast<int>(x);
    int iy = static_cast<int>(y);

    if (!isValidCoordinate(ix, iy) || species < 0 || species >= numSpecies_)
        return 0.0f;

    int index = getIndex(ix, iy);
    float totalAttraction = 0.0f;

    // sample own species trail with self-attraction
    float ownTrail = speciesData_[species][index];
    totalAttraction += ownTrail * selfAttraction;

    // sample other species trails with inter-species attraction
    for (int otherSpecies = 0; otherSpecies < numSpecies_; ++otherSpecies)
    {
        if (otherSpecies != species)
        {
            float otherTrail = speciesData_[otherSpecies][index];
            totalAttraction += otherTrail * otherAttraction;
        }
    }

    return std::max(0.0f, totalAttraction);
}

void OptimizedTrailMap::depositOptimized(int x, int y, float amount, int species)
{
    if (isValidCoordinate(x, y) && species >= 0 && species < numSpecies_ && amount > 0.0f)
    {
        int index = getIndex(x, y);
        speciesData_[species][index] += amount;

        // clamp to prevent overflow
        if (speciesData_[species][index] > 1000.0f)
            speciesData_[species][index] = 1000.0f;
    }
}

void OptimizedTrailMap::depositAnisotropic(int centerX, int centerY, float angle, float sigmaParallel,
                                           float sigmaPerp, float amount, int species, int widthLimit, int heightLimit)
{
    if (species < 0 || species >= numSpecies_ || amount <= 0.0f)
        return;

    // sigma clamping
    sigmaParallel = std::max(0.1f, sigmaParallel);
    sigmaPerp = std::max(0.1f, sigmaPerp);

    // compute rotation matrix terms
    float ca = std::cos(angle);
    float sa = std::sin(angle);

    // determine bounding box roughly within 3 sigma extents
    int radiusX = static_cast<int>(std::ceil(3.0f * std::max(sigmaParallel * std::abs(ca), sigmaPerp * std::abs(sa))));
    int radiusY = static_cast<int>(std::ceil(3.0f * std::max(sigmaParallel * std::abs(sa), sigmaPerp * std::abs(ca))));

    int minX = std::max(0, centerX - radiusX);
    int maxX = std::min(width_ - 1, centerX + radiusX);
    int minY = std::max(0, centerY - radiusY);
    int maxY = std::min(height_ - 1, centerY + radiusY);

    // normalization constant for 2d gaussian
    float norm = 1.0f / (2.0f * static_cast<float>(M_PI) * sigmaParallel * sigmaPerp);

    float *dst = speciesData_[species].get();

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            float dx = static_cast<float>(x - centerX);
            float dy = static_cast<float>(y - centerY);
            // rotate coordinates into agent frame: u along angle, v perpendicular
            float u = ca * dx + sa * dy;  // parallel to motion
            float v = -sa * dx + ca * dy; // perpendicular

            float exponent = -0.5f * ((u * u) / (sigmaParallel * sigmaParallel) + (v * v) / (sigmaPerp * sigmaPerp));
            if (exponent < -10.0f) // negligible
                continue;

            float w = std::exp(exponent) * norm;
            float contrib = amount * w;
            int idx = getIndex(x, y);
            dst[idx] = std::min(1000.0f, dst[idx] + contrib);
        }
    }
}

void OptimizedTrailMap::eraseOptimized(int x, int y, float amount, int species)
{
    if (isValidCoordinate(x, y) && species >= 0 && species < numSpecies_ && amount > 0.0f)
    {
        int index = getIndex(x, y);
        speciesData_[species][index] -= amount;

        // clamping here to prevent underflow
        if (speciesData_[species][index] < 0.0f)
            speciesData_[species][index] = 0.0f;
    }
}

void OptimizedTrailMap::enhanceOptimized(int x, int y, float amount, int species)
{
    if (isValidCoordinate(x, y) && species >= 0 && species < numSpecies_ && amount > 0.0f)
    {
        int index = getIndex(x, y);
        speciesData_[species][index] *= (1.0f + amount);

        if (speciesData_[species][index] > 1000.0f)
            speciesData_[species][index] = 1000.0f;
    }
}

void OptimizedTrailMap::syncToLegacyTrailMap(TrailMap &legacyMap) const
{
    // copy data from optimized SoA layout to legacy AoS layout for display
    for (int species = 0; species < std::min(numSpecies_, legacyMap.getNumSpecies()); ++species)
    {
        float *legacyData = legacyMap.getData(species);
        const float *optimizedData = getData(species);

        if (legacyData && optimizedData)
        {
            std::memcpy(legacyData, optimizedData, totalSize_ * sizeof(float));
        }
    }
}

void OptimizedTrailMap::applyBlurSIMD()
{
    // apply blur to all species using simd
    for (int species = 0; species < numSpecies_; ++species)
    {
        blurSpeciesSIMD(species);
    }
}

void OptimizedTrailMap::blurSpeciesSIMD(int species)
{
    if (species >= numSpecies_ || !speciesData_[species])
        return;

    const float *src = speciesData_[species].get();
    float *dst = tempSpeciesData_[species].get();

    // simple blur kernel application with simd
    for (int y = 1; y < height_ - 1; ++y)
    {
        for (int x = 1; x < width_ - 1; ++x)
        {
            int index = y * width_ + x;

            // 3x3 gaussian blur
            float sum = 0.0f;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int sampleIdx = (y + dy) * width_ + (x + dx);
                    sum += src[sampleIdx] * DIFFUSION_KERNEL[(dy + 1) * 3 + (dx + 1)];
                }
            }
            dst[index] = sum;
        }
    }

    // copy blurred data back
    std::memcpy(speciesData_[species].get(), tempSpeciesData_[species].get(), totalSize_ * sizeof(float));
}
