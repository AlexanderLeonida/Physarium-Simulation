#pragma once
#include <SFML/Graphics.hpp>
#include <memory>
#include <vector>

// platform specific simd includes
#ifdef __x86_64__
#include <immintrin.h> // for x86 simd (avx2)
#define HAVE_AVX2 1
#elif defined(__aarch64__) || defined(__arm64__)
#include <arm_neon.h> // for arm neon simd
#define HAVE_NEON 1
#else
#define HAVE_SIMD 0 // no simd support
#endif

// structure of arrays (soa) trail map for optimal vectorization
class OptimizedTrailMap
{
public:
    OptimizedTrailMap(int width, int height, int numSpecies = 1);
    ~OptimizedTrailMap();

    // core operations
    void clear();
    float sample(int x, int y, int species = 0) const;
    void deposit(int x, int y, float amount, int speciesIndex);
    void deposit(int species, int x, int y, float amount);

    // multi species operations
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getNumSpecies() const { return numSpecies_; }

    float sampleSpeciesInteraction(int x, int y, int currentSpecies,
                                   float selfAttraction, float otherAttraction) const;

    // high performance processing with simd
    void diffuseSIMD(float diffuseRate);
    void decaySIMD(float decayRate);
    void applyBlurSIMD();

    float sampleOptimized(float x, float y, int species, float selfAttraction, float otherAttraction) const;
    void depositOptimized(int x, int y, float amount, int species);
    // oriented elliptical gaussian deposit aligned by angle (radians)
    void depositAnisotropic(int centerX, int centerY, float angle, float sigmaParallel,
                            float sigmaPerp, float amount, int species, int widthLimit, int heightLimit);
    void eraseOptimized(int x, int y, float amount, int species);
    void enhanceOptimized(int x, int y, float amount, int species);
    void diffuseOptimized(float diffuseRate) { diffuseSIMD(diffuseRate); }
    void decayOptimized(float decayRate) { decaySIMD(decayRate); }
    void applyBlurOptimized() { applyBlurSIMD(); }
    void syncToLegacyTrailMap(class TrailMap &legacyMap) const;

    // older methods for compatibility
    void diffuse(float diffuseRate) { diffuseSIMD(diffuseRate); }
    void decay(float decayRate) { decaySIMD(decayRate); }
    void applyBlur() { applyBlurSIMD(); }

    // display
    void updateTexture(sf::Image &image, float displayThreshold, const sf::Color &baseColor) const;
    void updateMultiSpeciesTexture(sf::Image &image, float displayThreshold,
                                   const std::vector<sf::Color> &speciesColors) const;

    // data access for compatibility
    float *getData(int species = 0)
    {
        return species < numSpecies_ ? speciesData_[species].get() : nullptr;
    }
    const float *getData(int species = 0) const
    {
        return species < numSpecies_ ? speciesData_[species].get() : nullptr;
    }

    // parallel processing support
    void parallelDiffuse(float diffuseRate, int numThreads = 0);
    void parallelDecay(float decayRate, int numThreads = 0);

    // memory statistics
    size_t getMemoryUsage() const;
    void printMemoryLayout() const;

private:
    int width_, height_, numSpecies_;
    size_t totalSize_;

    // soa layout - separate arrays for each species for better cache coherency
    std::vector<std::unique_ptr<float[], std::function<void(float *)>>> speciesData_;
    std::vector<std::unique_ptr<float[], std::function<void(float *)>>> tempSpeciesData_;

    // simd aligned memory allocation
    float *allocateAlignedFloat(size_t count);
    void freeAlignedFloat(float *ptr);

    // simd processing helpers
    void diffuseSpeciesSIMD(int species, float diffuseRate);
    void decaySpeciesSIMD(int species, float decayRate);
    void blurSpeciesSIMD(int species);

    // parallel processing helpers
    void processSpeciesRange(int speciesStart, int speciesEnd,
                             std::function<void(int)> processor);

    // helper methods
    bool isValidCoordinate(int x, int y) const;
    int getIndex(int x, int y) const;
    void swapBuffers();

    // simd constants( platform specific )
#ifdef HAVE_AVX2
    static constexpr size_t SIMD_ALIGNMENT = 32; // avx2 alignment
    static constexpr size_t FLOATS_PER_SIMD = 8; // 8 floats per avx2 register
#elif defined(HAVE_NEON)
    static constexpr size_t SIMD_ALIGNMENT = 16; // neon alignment
    static constexpr size_t FLOATS_PER_SIMD = 4; // 4 floats per neon register
#else
    static constexpr size_t SIMD_ALIGNMENT = 16; // default alignment
    static constexpr size_t FLOATS_PER_SIMD = 4; // default vectorization
#endif

    // the diffusion kernel for simd operations
    alignas(32) static constexpr float DIFFUSION_KERNEL[9] = {
        0.0625f, 0.125f, 0.0625f,
        0.125f, 0.25f, 0.125f,
        0.0625f, 0.125f, 0.0625f};
};
