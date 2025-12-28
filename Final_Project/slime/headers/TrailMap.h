#pragma once
#include <SFML/Graphics.hpp>
#include <memory>

class TrailMap
{
public:
    TrailMap(int width, int height, int numSpecies = 1);
    ~TrailMap();

    void deposit(int species, int x, int y, float amount);

    // core operations
    void clear();
    float sample(int x, int y, int species = 0) const;
    
    // food economy: eat trail and return amount consumed
    float eat(int x, int y, int species, float maxBite);
    float eatAnySpecies(int x, int y, int excludeSpecies, float maxBite);  // For predators

    // multi species operations
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    void deposit(int x, int y, float amount, int speciesIndex);
    float sampleSpeciesInteraction(int x, int y, int currentSpecies,
                                   float selfAttraction, float otherAttraction) const;

    // processing
    void diffuse(float diffuseRate);
    void decay(float decayRate);
    void applyBlur();

    // display
    void updateTexture(sf::Image &image, float displayThreshold, const sf::Color &baseColor) const;
    void updateMultiSpeciesTexture(sf::Image &image, float displayThreshold,
                                   const std::vector<sf::Color> &speciesColors) const;

    // accessors
    int getNumSpecies() const { return numSpecies_; }
    float *getData(int species = 0) { return species < numSpecies_ ? speciesData_[species].get() : nullptr; }
    const float *getData(int species = 0) const { return species < numSpecies_ ? speciesData_[species].get() : nullptr; }

    // GPU data transfer methods
    std::vector<float> getAllData() const;
    void setData(const std::vector<float> &data, int width, int height);
    size_t getDataSize() const { return width_ * height_ * numSpecies_; }

private:
    int width_, height_, numSpecies_;
    std::vector<std::unique_ptr<float[]>> speciesData_;     // one trail map per species
    std::vector<std::unique_ptr<float[]>> tempSpeciesData_; // for diffusion calculations

    // helper methods
    bool isValidCoordinate(int x, int y) const;
    int getIndex(int x, int y) const;
    void swapBuffers();

    // box blur functions for improved diffusion (based on a very helpful go reference)
    // https://github.com/fogleman/physarum
    void boxBlurHorizontal(float *src, float *dst, int w, int h, int r, float scale);
    void boxBlurVertical(float *src, float *dst, int w, int h, int r, float scale);
};
