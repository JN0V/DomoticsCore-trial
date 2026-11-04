#pragma once

/**
 * @file LED.h
 * @brief Declares the DomoticsCore LED component for single-color and RGB LED control.
 * 
 * @example DomoticsCore-LED/examples/BasicLED/src/main.cpp
 * @example DomoticsCore-LED/examples/LEDWithWebUI/src/main.cpp
 */

#include <DomoticsCore/IComponent.h>
#include <DomoticsCore/Timer.h>
#include <DomoticsCore/Platform_HAL.h>
#include <vector>
#include <cmath>

namespace DomoticsCore {
namespace Components {

// LED effect types
enum class LEDEffect {
    Solid,      // Constant brightness
    Blink,      // On/off blinking
    Fade,       // Smooth fade in/out
    Pulse,      // Heartbeat-like pulse
    Rainbow,    // Color cycling (RGB LEDs)
    Breathing   // Smooth breathing effect
};

// LED color structure for RGB LEDs
struct LEDColor {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    
    LEDColor() = default;
    LEDColor(uint8_t r, uint8_t g, uint8_t b) : red(r), green(g), blue(b) {}
    
    // Predefined colors
    static LEDColor White() { return LEDColor(255, 255, 255); }
    static LEDColor Red() { return LEDColor(255, 0, 0); }
    static LEDColor Green() { return LEDColor(0, 255, 0); }
    static LEDColor Blue() { return LEDColor(0, 0, 255); }
    static LEDColor Yellow() { return LEDColor(255, 255, 0); }
    static LEDColor Cyan() { return LEDColor(0, 255, 255); }
    static LEDColor Magenta() { return LEDColor(255, 0, 255); }
    static LEDColor Off() { return LEDColor(0, 0, 0); }
};

// Individual LED configuration
struct LEDConfig {
    int pin = -1;
    bool isRGB = false;
    int redPin = -1;
    int greenPin = -1;
    int bluePin = -1;
    uint8_t maxBrightness = 255;
    bool invertLogic = false;  // true for common anode RGB LEDs
    String name = "";
};

// LED state for effects
struct LEDState {
    LEDColor currentColor;
    uint8_t brightness = 0;
    LEDEffect effect = LEDEffect::Solid;
    unsigned long effectSpeed = 1000;  // milliseconds
    bool enabled = true;
    
    // Effect state variables
    unsigned long lastUpdate = 0;
    float effectPhase = 0.0;
};

/**
 * @class DomoticsCore::Components::LEDComponent
 * @brief Drives one or more LEDs (single-color or RGB) with PWM brightness and effects.
 *
 * Manages pin initialization, supports named LEDs, and provides built-in effects updated via a
 * non-blocking timer. Can be paired with a WebUI provider to expose UI controls.
 */
class LEDComponent : public IComponent {
private:
    std::vector<LEDConfig> ledConfigs;
    std::vector<LEDState> ledStates;
    Utils::NonBlockingDelay updateTimer;
    bool pinsInitialized = false;  // true between a successful begin() and shutdown()

public:
    /**
     * @brief Construct a new LEDComponent with a 20 Hz update timer.
     */
    LEDComponent() : updateTimer(50) {  // 20Hz update rate
        // Set component metadata
        metadata.name = "LED";
        metadata.version = "1.6.0";  // x-release-please-version
        metadata.author = "DomoticsCore";
        metadata.description = "Multi-LED management with PWM control and effects";
        metadata.category = "Hardware";
        metadata.tags = {"led", "pwm", "effects", "hardware"};
    }
    
    // Required IComponent methods
    
    // Component lifecycle
    /**
     * @brief Validate pin assignments, initialize hardware, and reset state.
     */
    ComponentStatus begin() override {
        DLOG_I(LOG_LED, "Initializing...");
        
        // Validate LED pin assignments
        if (!validateLEDPins()) {
            setStatus(ComponentStatus::ConfigError);
            return ComponentStatus::ConfigError;
        }
        
        // Initialize hardware pins
        initializePins();

        // Fresh runtime state: assign() rather than resize() so a second begin()
        // also resets effectPhase and lastUpdate, which resize() would have kept.
        ledStates.assign(ledConfigs.size(), LEDState());
        pinsInitialized = true;

        DLOG_I(LOG_LED, "Initialized %zu LEDs successfully", ledConfigs.size());
        setStatus(ComponentStatus::Success);
        return ComponentStatus::Success;
    }
    
    /**
     * @brief Update LED effects when the non-blocking timer fires.
     */
    void loop() override {
        if (getLastStatus() != ComponentStatus::Success) return;

        if (updateTimer.isReady()) {
            updateEffects();
        }
    }
    
    /**
     * @brief Turn off all LEDs and release the runtime state.
     *
     * The configuration survives: shutdown() is reversible, and a later begin()
     * must bring the same LEDs back up. Only @ref ledStates — pure runtime
     * churn — is released, per Constitution XIV.
     */
    ComponentStatus shutdown() override {
        DLOG_I(LOG_LED, "Shutting down...");

        // Turn off all LEDs
        for (size_t i = 0; i < ledConfigs.size(); i++) {
            setLEDOutput(i, LEDColor::Off(), 0);
        }

        ledStates.clear();
        ledStates.shrink_to_fit();
        pinsInitialized = false;

        setStatus(ComponentStatus::Success);
        return ComponentStatus::Success;
    }

    // Configuration setup
    /**
     * @brief Add a fully-specified LED configuration (single or RGB).
     *
     * Accepted before or after begin(). Called after begin(), the pins are
     * validated and initialized on the spot and the matching state entry is
     * created, so @ref ledConfigs and @ref ledStates never drift apart — an LED
     * added late is driveable, not merely listed.
     *
     * @return false if called after begin() with unusable pins; true otherwise.
     *         Before begin(), pins are validated there instead and this always
     *         returns true.
     */
    bool addLED(const LEDConfig& config) {
        if (!pinsInitialized) {
            ledConfigs.push_back(config);
            return true;
        }

        if (!validateLEDConfig(config)) return false;

        ledConfigs.push_back(config);
        initializePin(ledConfigs.back());
        ledStates.emplace_back();
        return true;
    }

    /**
     * @brief Convenience helper to register a single-channel LED.
     * @return see @ref addLED.
     */
    bool addSingleLED(int pin, const String& name = "", uint8_t maxBrightness = 255, bool invertLogic = false) {
        LEDConfig config;
        config.pin = pin;
        config.isRGB = false;
        config.name = name.isEmpty() ? ("LED_" + String(ledConfigs.size())) : name;
        config.maxBrightness = maxBrightness;
        config.invertLogic = invertLogic;
        return addLED(config);
    }

    /**
     * @brief Register a three-channel RGB LED using discrete GPIO pins.
     * @return see @ref addLED.
     */
    bool addRGBLED(int redPin, int greenPin, int bluePin, const String& name = "",
                   uint8_t maxBrightness = 255, bool invertLogic = false) {
        LEDConfig config;
        config.isRGB = true;
        config.redPin = redPin;
        config.greenPin = greenPin;
        config.bluePin = bluePin;
        config.name = name.isEmpty() ? ("RGB_" + String(ledConfigs.size())) : name;
        config.maxBrightness = maxBrightness;
        config.invertLogic = invertLogic;
        return addLED(config);
    }
    
    // LED control methods
    /**
     * @brief Set LED color/brightness and clear any active effect.
     */
    bool setLED(size_t ledIndex, const LEDColor& color, uint8_t brightness = 255) {
        if (ledIndex >= ledStates.size()) return false;

        ledStates[ledIndex].currentColor = color;
        ledStates[ledIndex].brightness = brightness;
        ledStates[ledIndex].effect = LEDEffect::Solid;
        return true;
    }

    /**
     * @brief Lookup an LED by name and assign color/brightness.
     */
    bool setLED(const String& name, const LEDColor& color, uint8_t brightness = 255) {
        for (size_t i = 0; i < ledConfigs.size(); i++) {
            if (ledConfigs[i].name == name) {
                return setLED(i, color, brightness);
            }
        }
        return false;
    }

    /**
     * @brief Apply an animated effect to an LED by index.
     */
    bool setLEDEffect(size_t ledIndex, LEDEffect effect, unsigned long speed = 1000) {
        if (ledIndex >= ledStates.size()) return false;
        
        // Reference state and config for convenience
        auto &state = ledStates[ledIndex];
        const auto &config = ledConfigs[ledIndex];

        // Apply effect parameters
        state.effect = effect;
        state.effectSpeed = speed;
        state.effectPhase = 0.0;
        state.lastUpdate = HAL::getMillis();

        // Ensure LED is enabled when an effect is applied
        state.enabled = true;

        // If no brightness has been set yet, default to the configured max brightness
        // This fixes the issue where effects appear to do nothing because brightness was 0.
        if (state.brightness == 0) {
            state.brightness = config.maxBrightness;
        }

        // If color was never set (Off), default to White so single LEDs (non-RGB)
        // actually emit light when effects run. For RGB, White is also a sensible default.
        if (state.currentColor.red == 0 && state.currentColor.green == 0 && state.currentColor.blue == 0) {
            state.currentColor = LEDColor::White();
        }

        return true;
    }

    /**
     * @brief Apply an animated effect to an LED by name.
     */
    bool setLEDEffect(const String& name, LEDEffect effect, unsigned long speed = 1000) {
        for (size_t i = 0; i < ledConfigs.size(); i++) {
            if (ledConfigs[i].name == name) {
                return setLEDEffect(i, effect, speed);
            }
        }
        return false;
    }

    /**
     * @brief Enable or disable an LED by index (disabled LEDs are forced off).
     */
    bool enableLED(size_t ledIndex, bool enabled = true) {
        if (ledIndex >= ledStates.size()) return false;
        ledStates[ledIndex].enabled = enabled;
        if (!enabled) {
            setLEDOutput(ledIndex, LEDColor::Off(), 0);
        }
        return true;
    }

    /**
     * @brief Enable or disable an LED by name.
     */
    bool enableLED(const String& name, bool enabled = true) {
        for (size_t i = 0; i < ledConfigs.size(); i++) {
            if (ledConfigs[i].name == name) {
                return enableLED(i, enabled);
            }
        }
        return false;
    }

    /**
     * @brief Number of configured LEDs (single or RGB entries).
     */
    size_t getLEDCount() const { return ledConfigs.size(); }

    /**
     * @brief Retrieve friendly names for all configured LEDs.
     */
    std::vector<String> getLEDNames() const {
        std::vector<String> names;
        for (const auto& config : ledConfigs) {
            names.push_back(config.name);
        }
        return names;
    }

    /**
     * @brief Rename the LED at the given index.
     */
    bool setLEDName(size_t ledIndex, const String& name) {
        if (ledIndex >= ledConfigs.size() || name.isEmpty()) return false;
        ledConfigs[ledIndex].name = name;
        return true;
    }

    /**
     * @brief Compose a human-readable description of an LED state.
     */
    String getLEDStatus(size_t ledIndex) const {
        if (ledIndex >= ledStates.size()) return "Invalid index";

        const auto& state = ledStates[ledIndex];
        const auto& config = ledConfigs[ledIndex];
        
        String status = "LED '" + config.name + "': ";
        status += state.enabled ? "Enabled" : "Disabled";
        
        if (state.enabled) {
            status += ", Color: RGB(" + String(state.currentColor.red) + "," + 
                     String(state.currentColor.green) + "," + String(state.currentColor.blue) + ")";
            status += ", Brightness: " + String(state.brightness);
            status += ", Effect: " + getEffectName(state.effect);
        }
        
        return status;
    }
    
    String getEffectName(LEDEffect effect) const {
        switch (effect) {
            case LEDEffect::Solid: return "Solid";
            case LEDEffect::Blink: return "Blink";
            case LEDEffect::Fade: return "Fade";
            case LEDEffect::Pulse: return "Pulse";
            case LEDEffect::Rainbow: return "Rainbow";
            case LEDEffect::Breathing: return "Breathing";
            default: return "Unknown";
        }
    }

    // ------------------------------------------------------------------
    // Effect engine — pure arithmetic, no hardware and no member state.
    // Split out of updateEffects() so the curves can be checked directly:
    // the HAL swallows analogWrite() on the host, so the only way to test
    // what a pin would receive is to test the value computed for it.
    // ------------------------------------------------------------------

    /**
     * @brief Brightness an effect emits at a given phase.
     * @param effect Effect being animated.
     * @param phase  Position in the cycle, 0.0 to 1.0.
     * @param base   Brightness the effect modulates (the LED's set brightness).
     * @return Modulated brightness. Solid and Rainbow return @p base unchanged —
     *         Rainbow animates the colour, not the brightness.
     */
    static uint8_t effectBrightness(LEDEffect effect, float phase, uint8_t base) {
        switch (effect) {
            case LEDEffect::Blink:
                return (phase < 0.5f) ? base : 0;

            case LEDEffect::Fade:
                return (uint8_t)(base * (sin(phase * 2 * HAL::PI) + 1) / 2);

            case LEDEffect::Pulse:
                if (phase < 0.3f) {
                    return (uint8_t)(base * sin(phase * HAL::PI / 0.3));
                } else if (phase < 0.5f) {
                    return (uint8_t)(base * sin((phase - 0.3) * HAL::PI / 0.2));
                }
                return 0;

            case LEDEffect::Breathing:
                return (uint8_t)(base * (1 - cos(phase * 2 * HAL::PI)) / 2);

            default:
                return base;
        }
    }

    /**
     * @brief Colour the Rainbow effect emits at a given phase (RGB LEDs only).
     * @param phase Position in the cycle, 0.0 to 1.0, mapped onto a 360° hue.
     */
    static LEDColor rainbowColor(float phase) {
        float hue = phase * 360.0;
        // Simple HSV to RGB conversion
        if (hue < 120) {
            return LEDColor(255 - hue * 2.125, hue * 2.125, 0);
        } else if (hue < 240) {
            return LEDColor(0, 255 - (hue - 120) * 2.125, (hue - 120) * 2.125);
        }
        return LEDColor((hue - 240) * 2.125, 0, 255 - (hue - 240) * 2.125);
    }

    /**
     * @brief Scale a 0-255 value onto a 0-@p max range.
     */
    static uint8_t scaleToMax(uint8_t value, uint8_t max) {
        return (uint8_t)HAL::map(value, 0, 255, 0, max);
    }

    /**
     * @brief PWM value actually written to a pin, after common-anode inversion.
     */
    static uint8_t pwmValue(uint8_t value, bool invert) {
        return invert ? (uint8_t)(255 - value) : value;
    }

private:
    // Private helper methods
    bool validateLEDPins() {
        for (const auto& config : ledConfigs) {
            if (!validateLEDConfig(config)) return false;
        }
        return true;
    }

    static bool validateLEDConfig(const LEDConfig& config) {
        if (config.isRGB) {
            if (config.redPin < 0 || config.greenPin < 0 || config.bluePin < 0) {
                DLOG_E(LOG_LED, "Invalid RGB pins for LED '%s': R=%d, G=%d, B=%d",
                            config.name.c_str(), config.redPin, config.greenPin, config.bluePin);
                return false;
            }
        } else {
            if (config.pin < 0) {
                DLOG_E(LOG_LED, "Invalid pin for LED '%s': %d", config.name.c_str(), config.pin);
                return false;
            }
        }
        return true;
    }

    void initializePins() {
        for (const auto& config : ledConfigs) {
            initializePin(config);
        }
    }

    void initializePin(const LEDConfig& config) {
        if (config.isRGB) {
            HAL::pinMode(config.redPin, OUTPUT);
            HAL::pinMode(config.greenPin, OUTPUT);
            HAL::pinMode(config.bluePin, OUTPUT);
            setPWMOutput(config.redPin, 0, config.invertLogic);
            setPWMOutput(config.greenPin, 0, config.invertLogic);
            setPWMOutput(config.bluePin, 0, config.invertLogic);
        } else {
            HAL::pinMode(config.pin, OUTPUT);
            setPWMOutput(config.pin, 0, config.invertLogic);
        }
    }

    void setPWMOutput(int pin, uint8_t value, bool invert) {
        if (pin < 0) return;
        HAL::analogWrite(pin, pwmValue(value, invert));
    }

    void setLEDOutput(size_t ledIndex, const LEDColor& color, uint8_t brightness) {
        if (ledIndex >= ledConfigs.size()) return;

        const auto& config = ledConfigs[ledIndex];
        uint8_t scaledBrightness = scaleToMax(brightness, config.maxBrightness);

        if (config.isRGB) {
            uint8_t red = scaleToMax(color.red, scaledBrightness);
            uint8_t green = scaleToMax(color.green, scaledBrightness);
            uint8_t blue = scaleToMax(color.blue, scaledBrightness);

            setPWMOutput(config.redPin, red, config.invertLogic);
            setPWMOutput(config.greenPin, green, config.invertLogic);
            setPWMOutput(config.bluePin, blue, config.invertLogic);
        } else {
            uint8_t value = (color.red > 0 || color.green > 0 || color.blue > 0) ? scaledBrightness : 0;
            setPWMOutput(config.pin, value, config.invertLogic);
        }
    }

    void updateEffects() {
        unsigned long currentTime = HAL::getMillis();
        
        for (size_t i = 0; i < ledStates.size(); i++) {
            auto& state = ledStates[i];
            
            if (!state.enabled || state.effect == LEDEffect::Solid) {
                if (state.effect == LEDEffect::Solid) {
                    setLEDOutput(i, state.currentColor, state.brightness);
                }
                continue;
            }
            
            // Calculate effect phase (0.0 to 1.0)
            float deltaTime = (currentTime - state.lastUpdate) / (float)state.effectSpeed;
            state.effectPhase += deltaTime;
            
            if (state.effectPhase > 1.0f) {
                state.effectPhase = fmod(state.effectPhase, 1.0f);
            }
            
            state.lastUpdate = currentTime;

            // Apply effect
            LEDColor outputColor = state.currentColor;
            uint8_t outputBrightness = effectBrightness(state.effect, state.effectPhase, state.brightness);

            if (state.effect == LEDEffect::Rainbow && ledConfigs[i].isRGB) {
                outputColor = rainbowColor(state.effectPhase);
            }

            setLEDOutput(i, outputColor, outputBrightness);
        }
    }

};

} // namespace Components
} // namespace DomoticsCore
