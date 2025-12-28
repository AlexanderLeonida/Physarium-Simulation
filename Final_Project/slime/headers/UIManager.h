#pragma once
#include <SFML/Graphics.hpp>
#include "SimulationSettings.h"
#include <functional>

class UIManager
{
public:
    UIManager(sf::Font &font);

    // event handling
    void handleInput(const sf::Event::KeyPressed* keyEvent, SimulationSettings &settings);

    // rendering
    void drawHUD(sf::RenderWindow &window, const SimulationSettings &settings,
                 float updateTime, int agentCount);
    void drawParameterEditor(sf::RenderWindow &window, SimulationSettings &settings);

    // ui state
    bool isParameterEditorOpen() const { return showParameterEditor_; }
    void toggleParameterEditor() { showParameterEditor_ = !showParameterEditor_; }

    // callbacks for simulation control
    std::function<void()> onReset;
    std::function<void(int)> onAgentCountChange;
    std::function<void()> onSaveSettings;
    std::function<void()> onLoadSettings;
    std::function<void(int)> onMultiSpeciesToggle;
    std::function<void()> onSpeciesReroll; 

private:
    sf::Font &font_;
    bool showParameterEditor_ = false;
    bool showHelp_ = false;

    // ui styling
    sf::Color hudBackgroundColor_ = sf::Color(0, 0, 0, 150);
    sf::Color hudTextColor_ = sf::Color::White;
    sf::Color hudAccentColor_ = sf::Color::Yellow;

    // parameter adjustment
    void adjustParameter(float &param, float delta, float min, float max);
    void adjustParameter(int &param, int delta, int min, int max);

    // ui drawing helpers
    void drawBackground(sf::RenderWindow &window, const sf::FloatRect &bounds);
    void drawParameterSlider(sf::RenderWindow &window, const std::string &name,
                             float value, float min, float max, sf::Vector2f position);
    void drawHelpText(sf::RenderWindow &window);

    // input handling helpers
    void handleKeyboardInput(sf::Keyboard::Key key, SimulationSettings &settings);
    void handleMouseInput(const sf::Event::MouseButtonPressed* mouseEvent,
                          SimulationSettings &settings);

    // multi species setup helper
    void setupMultiSpecies(SimulationSettings &settings, int numSpecies);

    // species mode tracking
    int currentSpeciesMode_ = 1;
};
