#pragma once

#include <Arduino.h>

enum class LedMode {
    OFF,
    SOLID_ON,
    BLINKING,
    FAST_BLINK,   // Soft-AP captive portal active
    SLOW_BLINK    // Routine cycle running
};

class LedController {
public:
    LedController(int8_t pin = 5)
        : _pin(pin), _mode(LedMode::OFF), _blinkIntervalMs(250), _lastToggle(0), _state(false), _activeLow(false) {}

    void setActiveLow(bool low) {
        _activeLow = low;
        if (_pin >= 0) {
            setOff();
        }
    }

    void setPin(int8_t pin) {
        if (_pin >= 0 && _pin != pin) {
            setOff();
        }
        _pin = pin;
        if (_pin >= 0) {
            pinMode(_pin, OUTPUT);
            setOff();
        }
    }

    void begin() {
        if (_pin >= 0) {
            pinMode(_pin, OUTPUT);
            setOff();
        }
    }

    void setSolid(bool on) {
        _mode = on ? LedMode::SOLID_ON : LedMode::OFF;
        _state = on;
        if (_pin >= 0) {
            digitalWrite(_pin, (_state ^ _activeLow) ? HIGH : LOW);
        }
    }

    void setOff() {
        setSolid(false);
    }

    void setBlink(uint32_t intervalMs) {
        _mode = (intervalMs <= 250) ? LedMode::FAST_BLINK : LedMode::SLOW_BLINK;
        _blinkIntervalMs = intervalMs;
        _lastToggle = millis();
        _state = true;
        if (_pin >= 0) {
            digitalWrite(_pin, (_state ^ _activeLow) ? HIGH : LOW);
        }
    }

    void setBlinkRateHz(float hz) {
        _mode = LedMode::BLINKING;
        if (hz <= 0.0f) {
            _blinkIntervalMs = 500;
        } else {
            _blinkIntervalMs = (uint32_t)(1000.0f / (hz * 2.0f));
        }
        _lastToggle = millis();
        _state = true;
        if (_pin >= 0) {
            digitalWrite(_pin, (_state ^ _activeLow) ? HIGH : LOW);
        }
    }

    void update() {
        if (_pin < 0) return;
        if (_mode == LedMode::BLINKING || _mode == LedMode::FAST_BLINK || _mode == LedMode::SLOW_BLINK) {
            uint32_t now = millis();
            if (now - _lastToggle >= _blinkIntervalMs) {
                _lastToggle = now;
                _state = !_state;
                digitalWrite(_pin, (_state ^ _activeLow) ? HIGH : LOW);
            }
        }
    }

    LedMode getMode() const {
        return _mode;
    }

private:
    int8_t _pin;
    LedMode _mode;
    uint32_t _blinkIntervalMs;
    uint32_t _lastToggle;
    bool _state;
    bool _activeLow;
};
