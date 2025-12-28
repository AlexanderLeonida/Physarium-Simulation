#include "SimulationSettings.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>

bool SimulationSettings::saveToFile(const std::string &filename) const
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open file for writing: " << filename << std::endl;
        return false;
    }

    file << "# Physarum Simulation Settings\n";
    file << "stepsPerFrame=" << stepsPerFrame << "\n";
    file << "width=" << width << "\n";
    file << "height=" << height << "\n";
    file << "numAgents=" << numAgents << "\n";
    file << "spawnMode=" << static_cast<int>(spawnMode) << "\n";
    file << "trailWeight=" << trailWeight << "\n";
    file << "decayRate=" << decayRate << "\n";
    file << "diffuseRate=" << diffuseRate << "\n";
    file << "displayThreshold=" << displayThreshold << "\n";
    file << "blurEnabled=" << (blurEnabled ? 1 : 0) << "\n";
    file << "slimeShadingEnabled=" << (slimeShadingEnabled ? 1 : 0) << "\n";
    file << "motionInertia=" << motionInertia << "\n";
    file << "anisotropicSplatsEnabled=" << (anisotropicSplatsEnabled ? 1 : 0) << "\n";
    file << "splatSigmaParallel=" << splatSigmaParallel << "\n";
    file << "splatSigmaPerp=" << splatSigmaPerp << "\n";
    file << "splatIntensityScale=" << splatIntensityScale << "\n";
    file << "complianceStrength=" << complianceStrength << "\n";
    file << "complianceDamping=" << complianceDamping << "\n";

    // save species settings
    file << "speciesCount=" << speciesSettings.size() << "\n";
    for (size_t i = 0; i < speciesSettings.size(); ++i)
    {
        const auto &species = speciesSettings[i];
        file << "species" << i << "_moveSpeed=" << species.moveSpeed << "\n";
        file << "species" << i << "_turnSpeed=" << species.turnSpeed << "\n";
        file << "species" << i << "_sensorAngleSpacing=" << species.sensorAngleSpacing << "\n";
        file << "species" << i << "_sensorOffsetDistance=" << species.sensorOffsetDistance << "\n";
        file << "species" << i << "_sensorSize=" << species.sensorSize << "\n";
        file << "species" << i << "_attractionToSelf=" << species.attractionToSelf << "\n";
        file << "species" << i << "_attractionToOthers=" << species.attractionToOthers << "\n";
        file << "species" << i << "_repulsionFromOthers=" << species.repulsionFromOthers << "\n";
        file << "species" << i << "_behaviorIntensity=" << species.behaviorIntensity << "\n";
        file << "species" << i << "_color=" << static_cast<int>(species.color.r) << ","
             << static_cast<int>(species.color.g) << "," << static_cast<int>(species.color.b) << "\n";
    }

    return true;
}

bool SimulationSettings::loadFromFile(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open file for reading: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        size_t equalPos = line.find('=');
        if (equalPos == std::string::npos)
            continue;

        std::string key = line.substr(0, equalPos);
        std::string value = line.substr(equalPos + 1);

        // parse basic settings
        if (key == "stepsPerFrame")
            stepsPerFrame = std::stoi(value);
        else if (key == "width")
            width = std::stoi(value);
        else if (key == "height")
            height = std::stoi(value);
        else if (key == "numAgents")
            numAgents = std::stoi(value);
        else if (key == "spawnMode")
            spawnMode = static_cast<SpawnMode>(std::stoi(value));
        else if (key == "trailWeight")
            trailWeight = std::stof(value);
        else if (key == "decayRate")
            decayRate = std::stof(value);
        else if (key == "diffuseRate")
            diffuseRate = std::stof(value);
        else if (key == "displayThreshold")
            displayThreshold = std::stof(value);
        else if (key == "blurEnabled")
            blurEnabled = (std::stoi(value) != 0);
        else if (key == "slimeShadingEnabled")
            slimeShadingEnabled = (std::stoi(value) != 0);
        else if (key == "motionInertia")
            motionInertia = std::stof(value);
        else if (key == "anisotropicSplatsEnabled")
            anisotropicSplatsEnabled = (std::stoi(value) != 0);
        else if (key == "splatSigmaParallel")
            splatSigmaParallel = std::stof(value);
        else if (key == "splatSigmaPerp")
            splatSigmaPerp = std::stof(value);
        else if (key == "splatIntensityScale")
            splatIntensityScale = std::stof(value);
        else if (key == "complianceStrength")
            complianceStrength = std::stof(value);
        else if (key == "complianceDamping")
            complianceDamping = std::stof(value);
        // parse species settings
        else if (key.find("species") == 0)
        {
            size_t underscorePos = key.find('_');
            if (underscorePos != std::string::npos)
            {
                std::string indexStr = key.substr(7, underscorePos - 7); // after "species"
                int index = std::stoi(indexStr);
                std::string property = key.substr(underscorePos + 1);

                if (index >= 0 && index < static_cast<int>(speciesSettings.size()))
                {
                    auto &species = speciesSettings[index];
                    if (property == "moveSpeed")
                        species.moveSpeed = std::stof(value);
                    else if (property == "turnSpeed")
                        species.turnSpeed = std::stof(value);
                    else if (property == "sensorAngleSpacing")
                        species.sensorAngleSpacing = std::stof(value);
                    else if (property == "sensorOffsetDistance")
                        species.sensorOffsetDistance = std::stof(value);
                    else if (property == "sensorSize")
                        species.sensorSize = std::stoi(value);
                    else if (property == "colorR")
                        species.color.r = std::stoi(value);
                    else if (property == "colorG")
                        species.color.g = std::stoi(value);
                    else if (property == "colorB")
                        species.color.b = std::stoi(value);
                    else if (property == "colorA")
                        species.color.a = std::stoi(value);
                }
            }
        }
    }

    validateAndClamp();
    return true;
}

void SimulationSettings::validateAndClamp()
{
    stepsPerFrame = std::max(1, stepsPerFrame);
    width = std::clamp(width, 100, 4096);
    height = std::clamp(height, 100, 4096);
    numAgents = std::clamp(numAgents, 100, 1000000);
    trailWeight = std::clamp(trailWeight, 0.1f, 100.0f);
    decayRate = std::clamp(decayRate, 0.001f, 1.0f);
    diffuseRate = std::clamp(diffuseRate, 0.0f, 1.0f);
    displayThreshold = std::clamp(displayThreshold, 0.01f, 10.0f);
    motionInertia = std::clamp(motionInertia, 0.0f, 0.95f);
    splatSigmaParallel = std::clamp(splatSigmaParallel, 0.1f, 10.0f);
    splatSigmaPerp = std::clamp(splatSigmaPerp, 0.1f, 10.0f);
    splatIntensityScale = std::clamp(splatIntensityScale, 0.1f, 5.0f);
    complianceStrength = std::clamp(complianceStrength, 0.0f, 2.0f);
    complianceDamping = std::clamp(complianceDamping, 0.0f, 2.0f);

    // validate species settings
    for (auto &species : speciesSettings)
    {
        species.moveSpeed = std::max(0.1f, species.moveSpeed);
        species.turnSpeed = std::clamp(species.turnSpeed, 0.0f, 180.0f);
        species.sensorAngleSpacing = std::clamp(species.sensorAngleSpacing, 0.0f, 180.0f);
        species.sensorOffsetDistance = std::max(1.0f, species.sensorOffsetDistance);
        species.sensorSize = std::max(1, species.sensorSize);
    }

    // at least one species exists
    if (speciesSettings.empty())
    {
        speciesSettings.push_back(SpeciesSettings());
    }
}
