#include "UIManager.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <cstdlib>
#include <algorithm>

UIManager::UIManager(sf::Font &font) : font_(font) {}

void UIManager::handleInput(const sf::Event::KeyPressed* keyEvent, SimulationSettings &settings)
{
    if (keyEvent)
    {
        handleKeyboardInput(keyEvent->code, settings);
    }
}

void UIManager::handleMouseInput(const sf::Event::MouseButtonPressed* mouseEvent,
                                 SimulationSettings &settings)
{
    // mouse input handling for future interactive controls
    (void)mouseEvent; // suppress unused parameter warning
    (void)settings;
}

void UIManager::drawHUD(sf::RenderWindow &window, const SimulationSettings &settings,
                        float updateTime, int agentCount)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);

    // basic info
    oss << "=== Physarum Simulation ===" << "\n";
    oss << "Agents: " << agentCount << " | Update: " << updateTime << "ms" << "\n";
    oss << "Resolution: " << settings.width << "x" << settings.height << "\n\n";

    // trail settings
    oss << "=== Trail Settings ===" << "\n";
    oss << "Trail Weight [5/6]: " << settings.trailWeight << "\n";
    oss << "Decay Rate [3/4]: " << settings.decayRate << "\n";
    oss << "Diffuse Rate [1/2]: " << settings.diffuseRate << "\n";
    oss << "Display Threshold [T/Y]: " << settings.displayThreshold << "\n";
    oss << "Blur [B]: " << (settings.blurEnabled ? "ON" : "OFF") << "\n\n";

    // movement settings (first species)
    if (!settings.speciesSettings.empty())
    {
        const auto &species = settings.speciesSettings[0];
        oss << "=== Movement Settings ===" << "\n";
        oss << "Move Speed [E/R]: " << species.moveSpeed << "\n";
        oss << "Turn Speed [Q/W]: " << species.turnSpeed << "°" << "\n";
        oss << "Sensor Angle [7/8]: " << species.sensorAngleSpacing << "°" << "\n";
        oss << "Sensor Distance [9/0]: " << species.sensorOffsetDistance << "\n\n";
    }

    // emergence & reproduction (first species shown)
    if (!settings.speciesSettings.empty())
    {
        const auto &s = settings.speciesSettings[0];
        oss << "=== Emergence & Reproduction ===" << "\n";
        oss << "Mating [X]: " << (s.matingEnabled ? "ON" : "OFF")
            << "  |  Splitting [Z]: " << (s.splittingEnabled ? "ON" : "OFF")
            << "  |  Cross-species [C]: " << (s.crossSpeciesMating ? "ON" : "OFF") << "\n";
        oss << "Radius [U/I]: " << s.matingRadius
            << "  |  Mutation [K/J]: " << s.hybridMutationRate << "\n";
        oss << "SplitThreshold: " << s.splitEnergyThreshold
            << "  |  Rebirth: " << (s.rebirthEnabled ? "ON" : "OFF")
            << "  |  Lifespan(s): " << s.lifespanSeconds << "\n\n";
    }

    // controls
    oss << "=== Controls ===" << "\n";
    oss << "[Space] Reset | [↑/↓] Agent Count" << "\n";
    oss << "[P] Parameter Editor | [H] Help" << "\n";
    oss << "[S] Save | [L] Load Settings" << "\n";
    oss << "[M] Multi-Species (" << settings.speciesSettings.size() << " species)" << "\n";

    sf::Text text(font_, oss.str(), 14);
    text.setFillColor(hudTextColor_);
    text.setOutlineColor(sf::Color::Black);
    text.setOutlineThickness(1.5f);
    text.setPosition({10.0f, 10.0f});

    // draw background
    sf::FloatRect textBounds = text.getLocalBounds();
    drawBackground(window, sf::FloatRect({5, 5}, {textBounds.size.x + 10, textBounds.size.y + 10}));

    window.draw(text);
}

void UIManager::drawParameterEditor(sf::RenderWindow &window, SimulationSettings &settings)
{
    if (!showParameterEditor_)
        return;

    sf::Vector2f windowSize = static_cast<sf::Vector2f>(window.getSize());
    sf::FloatRect editorBounds({windowSize.x - 320, 10}, {300, windowSize.y - 20});

    drawBackground(window, editorBounds);

    sf::Vector2f pos(editorBounds.position.x + 10, editorBounds.position.y + 10);
    float lineHeight = 25.0f;

    sf::Text title(font_, "Parameter Editor", 16);
    title.setFillColor(hudAccentColor_);
    title.setPosition(pos);
    window.draw(title);
    pos.y += lineHeight * 1.5f;

    // trail parameters
    drawParameterSlider(window, "Trail Weight", settings.trailWeight, 0.0f, 50.0f, pos);
    pos.y += lineHeight;
    drawParameterSlider(window, "Decay Rate", settings.decayRate, 0.0f, 0.1f, pos);
    pos.y += lineHeight;
    drawParameterSlider(window, "Diffuse Rate", settings.diffuseRate, 0.0f, 1.0f, pos);
    pos.y += lineHeight * 1.5f;

    // species parameters (first species)
    if (!settings.speciesSettings.empty())
    {
        auto &species = settings.speciesSettings[0];
        drawParameterSlider(window, "Move Speed", species.moveSpeed, 0.1f, 20.0f, pos);
        pos.y += lineHeight;
        drawParameterSlider(window, "Turn Speed", species.turnSpeed, 0.0f, 90.0f, pos);
        pos.y += lineHeight;
        drawParameterSlider(window, "Sensor Angle", species.sensorAngleSpacing, 0.0f, 90.0f, pos);
        pos.y += lineHeight;
        drawParameterSlider(window, "Sensor Distance", species.sensorOffsetDistance, 1.0f, 50.0f, pos);
        pos.y += lineHeight * 1.5f;
        // reproduction / genetics quick sliders
        drawParameterSlider(window, "Mating Radius", species.matingRadius, 2.0f, 80.0f, pos);
        pos.y += lineHeight;
        drawParameterSlider(window, "Hybrid Mutation", species.hybridMutationRate, 0.0f, 0.5f, pos);
        pos.y += lineHeight;
        drawParameterSlider(window, "Split Threshold", species.splitEnergyThreshold, 1.0f, 3.0f, pos);
    }
}

void UIManager::handleKeyboardInput(sf::Keyboard::Key key, SimulationSettings &settings)
{
    const float smallStep = 0.01f;
    const float mediumStep = 0.1f;
    const float largeStep = 1.0f;

    switch (key)
    {
    // diffuse rate
    case sf::Keyboard::Key::Num1:
        adjustParameter(settings.diffuseRate, smallStep, 0.0f, 1.0f);
        break;
    case sf::Keyboard::Key::Num2:
        adjustParameter(settings.diffuseRate, -smallStep, 0.0f, 1.0f);
        break;

    // decay rate
    case sf::Keyboard::Key::Num3:
        adjustParameter(settings.decayRate, smallStep * 0.1f, 0.0f, 0.1f);
        break;
    case sf::Keyboard::Key::Num4:
        adjustParameter(settings.decayRate, -smallStep * 0.1f, 0.0f, 0.1f);
        break;

    // trail weight
    case sf::Keyboard::Key::Num5:
        adjustParameter(settings.trailWeight, mediumStep * 5.0f, 0.0f, 100.0f);
        break;
    case sf::Keyboard::Key::Num6:
        adjustParameter(settings.trailWeight, -mediumStep * 5.0f, 0.0f, 100.0f);
        break;

    // species parameters
    case sf::Keyboard::Key::Num7:
        if (!settings.speciesSettings.empty())
        {
            adjustParameter(settings.speciesSettings[0].sensorAngleSpacing, largeStep, 0.0f, 90.0f);
        }
        break;
    case sf::Keyboard::Key::Num8:
        if (!settings.speciesSettings.empty())
        {
            adjustParameter(settings.speciesSettings[0].sensorAngleSpacing, -largeStep, 0.0f, 90.0f);
        }
        break;
    case sf::Keyboard::Key::Num9:
        if (!settings.speciesSettings.empty())
        {
            adjustParameter(settings.speciesSettings[0].sensorOffsetDistance, largeStep, 1.0f, 50.0f);
        }
        break;
    case sf::Keyboard::Key::Num0:
        if (!settings.speciesSettings.empty())
        {
            adjustParameter(settings.speciesSettings[0].sensorOffsetDistance, -largeStep, 1.0f, 50.0f);
        }
        break;

    // movement parameters
    case sf::Keyboard::Key::Q:
        if (!settings.speciesSettings.empty())
        {
            adjustParameter(settings.speciesSettings[0].turnSpeed, largeStep, 0.0f, 90.0f);
        }
        break;
    case sf::Keyboard::Key::W:
        if (!settings.speciesSettings.empty())
        {
            adjustParameter(settings.speciesSettings[0].turnSpeed, -largeStep, 0.0f, 90.0f);
        }
        break;
    case sf::Keyboard::Key::E:
        if (!settings.speciesSettings.empty())
        {
            adjustParameter(settings.speciesSettings[0].moveSpeed, mediumStep, 0.1f, 20.0f);
        }
        break;
    case sf::Keyboard::Key::R:
        if (!settings.speciesSettings.empty())
        {
            adjustParameter(settings.speciesSettings[0].moveSpeed, -mediumStep, 0.1f, 20.0f);
        }
        break;

    // display parameters
    case sf::Keyboard::Key::T:
        adjustParameter(settings.displayThreshold, smallStep, 0.0f, 1.0f);
        break;
    case sf::Keyboard::Key::Y:
        adjustParameter(settings.displayThreshold, -smallStep, 0.0f, 1.0f);
        break;

    // toggles
    case sf::Keyboard::Key::B:
        settings.blurEnabled = !settings.blurEnabled;
        break;
    case sf::Keyboard::Key::P:
        toggleParameterEditor();
        break;
    case sf::Keyboard::Key::X: // toggle mating across all species
        for (auto &sp : settings.speciesSettings)
            sp.matingEnabled = !sp.matingEnabled;
        break;
    case sf::Keyboard::Key::C: // toggle cross species mating across all species
        for (auto &sp : settings.speciesSettings)
            sp.crossSpeciesMating = !sp.crossSpeciesMating;
        break;
    case sf::Keyboard::Key::U: // increase mating radius
        if (!settings.speciesSettings.empty())
        {
            adjustParameter(settings.speciesSettings[0].matingRadius, 1.0f, 2.0f, 120.0f);
            float v = settings.speciesSettings[0].matingRadius;
            for (size_t i = 1; i < settings.speciesSettings.size(); ++i)
                settings.speciesSettings[i].matingRadius = v;
        }
        break;
    case sf::Keyboard::Key::I: // decrease mating radius
        if (!settings.speciesSettings.empty())
        {
            adjustParameter(settings.speciesSettings[0].matingRadius, -1.0f, 2.0f, 120.0f);
            float v = settings.speciesSettings[0].matingRadius;
            for (size_t i = 1; i < settings.speciesSettings.size(); ++i)
                settings.speciesSettings[i].matingRadius = v;
        }
        break;
    case sf::Keyboard::Key::K: // increase hybrid mutation
        if (!settings.speciesSettings.empty())
        {
            adjustParameter(settings.speciesSettings[0].hybridMutationRate, 0.01f, 0.0f, 0.8f);
            float v = settings.speciesSettings[0].hybridMutationRate;
            for (size_t i = 1; i < settings.speciesSettings.size(); ++i)
                settings.speciesSettings[i].hybridMutationRate = v;
        }
        break;
    case sf::Keyboard::Key::J: // decrease hybrid mutation
        if (!settings.speciesSettings.empty())
        {
            adjustParameter(settings.speciesSettings[0].hybridMutationRate, -0.01f, 0.0f, 0.8f);
            float v = settings.speciesSettings[0].hybridMutationRate;
            for (size_t i = 1; i < settings.speciesSettings.size(); ++i)
                settings.speciesSettings[i].hybridMutationRate = v;
        }
        break;
    // NOTE: H key is handled by main.cpp for species intensity controls for now

    // actions
    case sf::Keyboard::Key::Space:
        if (onReset)
            onReset();
        break;
    case sf::Keyboard::Key::Up:
        if (onAgentCountChange)
            onAgentCountChange(settings.numAgents * 2);
        break;
    case sf::Keyboard::Key::Down:
        if (onAgentCountChange)
            onAgentCountChange(std::max(1000, settings.numAgents / 2));
        break;
    case sf::Keyboard::Key::S:
        if (onSaveSettings)
            onSaveSettings();
        break;
    case sf::Keyboard::Key::L:
        if (onLoadSettings)
            onLoadSettings();
        break;

    // multi species mode toggle
    case sf::Keyboard::Key::M:
        // this is now handled by main.cpp directly
        if (onMultiSpeciesToggle)
            onMultiSpeciesToggle(1); // placeholder value
        break;

        // NOTE: G key (species reroll) is handled by main.cpp directly

    default:
        break;
    }
}

void UIManager::adjustParameter(float &param, float delta, float min, float max)
{
    param = std::clamp(param + delta, min, max);
}

void UIManager::adjustParameter(int &param, int delta, int min, int max)
{
    param = std::clamp(param + delta, min, max);
}

void UIManager::drawBackground(sf::RenderWindow &window, const sf::FloatRect &bounds)
{
    sf::RectangleShape background(sf::Vector2f(bounds.size.x, bounds.size.y));
    background.setPosition({bounds.position.x, bounds.position.y});
    background.setFillColor(hudBackgroundColor_);
    background.setOutlineThickness(1.0f);
    background.setOutlineColor(sf::Color(100, 100, 100));
    window.draw(background);
}

void UIManager::drawParameterSlider(sf::RenderWindow &window, const std::string &name,
                                    float value, float min, float max, sf::Vector2f position)
{
    // simple text based parameter display (could be enhanced with actual sliders)
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << name << ": " << value;

    sf::Text text(font_, oss.str(), 12);
    text.setFillColor(hudTextColor_);
    text.setPosition(position);
    window.draw(text);

    // simple progress bar
    float progress = (value - min) / (max - min);
    sf::RectangleShape bar(sf::Vector2f(200.0f * progress, 3.0f));
    bar.setPosition({position.x, position.y + 15.0f});
    bar.setFillColor(hudAccentColor_);
    window.draw(bar);

    sf::RectangleShape barBg(sf::Vector2f(200.0f, 3.0f));
    barBg.setPosition({position.x, position.y + 15.0f});
    barBg.setFillColor(sf::Color(50, 50, 50));
    barBg.setOutlineThickness(1.0f);
    barBg.setOutlineColor(sf::Color(100, 100, 100));
    window.draw(barBg);
    window.draw(bar);
}

void UIManager::setupMultiSpecies(SimulationSettings &settings, int numSpecies)
{
    settings.speciesSettings.clear();

    if (numSpecies >= 1)
    {
        SimulationSettings::SpeciesSettings species1;
        species1.color = sf::Color(255, 80, 80); // bright red
        species1.moveSpeed = 1.5f;
        species1.turnSpeed = 25.0f;
        species1.sensorAngleSpacing = 22.5f;
        species1.sensorOffsetDistance = 9.0f;
        species1.attractionToSelf = 1.2f;
        species1.attractionToOthers = 0.1f;
        species1.repulsionFromOthers = 0.0f;
        settings.speciesSettings.push_back(species1);
    }

    if (numSpecies >= 2)
    {
        SimulationSettings::SpeciesSettings species2;
        species2.color = sf::Color(80, 150, 255); // bright blue
        species2.moveSpeed = 1.2f;
        species2.turnSpeed = 30.0f;
        species2.sensorAngleSpacing = 22.5f;
        species2.sensorOffsetDistance = 9.0f;
        species2.attractionToSelf = 1.0f;
        species2.attractionToOthers = 0.8f;
        species2.repulsionFromOthers = 0.0f;
        settings.speciesSettings.push_back(species2);
    }

    if (numSpecies >= 3)
    {
        SimulationSettings::SpeciesSettings species3;
        species3.color = sf::Color(80, 255, 80); // bright green
        species3.moveSpeed = 1.8f;
        species3.turnSpeed = 35.0f;
        species3.sensorAngleSpacing = 25.0f;
        species3.sensorOffsetDistance = 10.0f;
        species3.attractionToSelf = 1.1f;
        species3.attractionToOthers = -0.3f; // repelled by others
        species3.repulsionFromOthers = 0.5f;
        settings.speciesSettings.push_back(species3);
    }

    if (numSpecies >= 4)
    {
        SimulationSettings::SpeciesSettings species4;
        species4.color = sf::Color(255, 255, 80); // bright yellow 
        species4.moveSpeed = 1.8f;
        species4.turnSpeed = 90.0f;
        species4.sensorAngleSpacing = 60.0f;
        species4.sensorOffsetDistance = 10.0f;
        species4.attractionToSelf = 0.5f;
        species4.attractionToOthers = 0.0f;
        species4.repulsionFromOthers = 0.0f;
        settings.speciesSettings.push_back(species4);
    }

    if (numSpecies >= 5)
    {
        SimulationSettings::SpeciesSettings species5;
        species5.color = sf::Color(255, 80, 255); // bright magenta 
        species5.moveSpeed = 1.5f;
        species5.turnSpeed = 30.0f;
        species5.sensorAngleSpacing = 25.0f;
        species5.sensorOffsetDistance = 10.0f;
        species5.attractionToSelf = 1.5f;
        species5.attractionToOthers = 0.5f;
        species5.repulsionFromOthers = 0.0f;
        settings.speciesSettings.push_back(species5);
    }

    std::cout << " Multi-species setup complete! " << numSpecies << " species" << std::endl;
}
