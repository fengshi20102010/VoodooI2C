//
//  VoodooI2CNativeEngine.cpp
//  VoodooI2C
//
//  Created by Alexandre on 10/02/2018.
//  Copyright © 2018 Alexandre Daoud and Kishor Prins. All rights reserved.
//

#include "VoodooI2CNativeEngine.hpp"
#include <pexpert/pexpert.h>
#include <IOKit/IOUserClient.h>

#define super VoodooI2CMultitouchEngine
OSDefineMetaClassAndStructors(VoodooI2CNativeEngine, VoodooI2CMultitouchEngine);

MultitouchReturn VoodooI2CNativeEngine::handleInterruptReport(VoodooI2CMultitouchEvent event, AbsoluteTime timestamp) {
    if (!voodooInputInstance) {
        return MultitouchReturnContinue;
    }

    message.timestamp = timestamp;
    message.contact_count = event.contact_count;
    memset(message.transducers, 0, VOODOO_INPUT_MAX_TRANSDUCERS * sizeof(VoodooInputTransducer));
    
    VoodooI2CDigitiserTransducer* firstTransducer = (VoodooI2CDigitiserTransducer*) event.transducers->getObject(0);

    if (!firstTransducer)
        return MultitouchReturnBreak;
    
    if (firstTransducer->type == kDigitiserTransducerStylus)
        stylus_check = 1;

    int valid_touch_count = 0;

    // Force Click: take the latest configuration, then decide once per report from the highest pressure
    // of the touching fingers.
    IOLockLock(forceConfigLock);
    if (forceConfigChanged) {
        activeConfig = forceConfig;
        forceConfigChanged = false;
        forceStageActive = forceArmed = false;
        pressActive = false;
    }
    IOLockUnlock(forceConfigLock);
    UInt32 max_pressure = 0;
    for (int i = 0; i < event.contact_count; i++) {
        VoodooI2CDigitiserTransducer* transducer = (VoodooI2CDigitiserTransducer*) event.transducers->getObject(i+stylus_check);
        if (transducer && transducer->tip_switch.value() && transducer->tip_pressure.value() > max_pressure)
            max_pressure = transducer->tip_pressure.value();
    }
    if (max_pressure && !pressureSeen) {
        pressureSeen = true;
        setProperty("ForceClickPressureSeen", true);
    }
    const bool button_down = !firstTransducer->has_secondary_button && firstTransducer->physical_button.value();
    bool force_click = false;
    if (button_down && isForceClickEnabled())
        force_click = !pressureSeen || activeConfig.mode == 0 || updateForceStage(max_pressure, true, timestamp);
    else
        updateForceStage(max_pressure, false, timestamp);
    const UInt32 threshold = effectiveThreshold(activeConfig);
    if (threshold != publishedThreshold) {
        publishedThreshold = threshold;
        setProperty("ForceClickSystemFirmness", systemFirmness, 32);
        setProperty("ForceClickEffectiveThreshold", threshold, 32);
    }
    if (activeConfig.diagnostics)
        notePress(button_down, max_pressure, force_click, timestamp);

    for (int i = 0; i < event.contact_count; i++) {
        VoodooI2CDigitiserTransducer* transducer = (VoodooI2CDigitiserTransducer*) event.transducers->getObject(i+stylus_check);
        VoodooInputTransducer* inputTransducer = &message.transducers[i];
        
        if (!transducer) {
            continue;
        }
        
        inputTransducer->fingerType = (MT2FingerType) (kMT2FingerTypeIndexFinger + (i % 4));
        inputTransducer->secondaryId = transducer->secondary_id;
        
        inputTransducer->type = (transducer->type == DigitiserTransducerType::kDigitiserTransducerFinger) ? VoodooInputTransducerType::FINGER : VoodooInputTransducerType::STYLUS;
        
        inputTransducer->isValid = transducer->is_valid;
        if (inputTransducer->isValid) {
            valid_touch_count++;
        }
        
        if (!transducer->confidence.value()) {
            inputTransducer->fingerType = kMT2FingerTypePalm;
        }
        
        inputTransducer->isTransducerActive = transducer->tip_switch.value();
        inputTransducer->isPhysicalButtonDown = !transducer->has_secondary_button && transducer->physical_button.value(); // if it has secondary button, then it will be passed as buttons on the "trackpoint" device
        
        inputTransducer->currentCoordinates.x = transducer->coordinates.x.value();
        inputTransducer->previousCoordinates.x = transducer->coordinates.x.last.value;
        
        inputTransducer->currentCoordinates.y = transducer->coordinates.y.value();
        inputTransducer->previousCoordinates.y = transducer->coordinates.y.last.value;
        inputTransducer->supportsPressure = false;
        inputTransducer->timestamp = timestamp;

        // TODO: does VoodooI2C know width(s)? how does it measure pressure?
        inputTransducer->currentCoordinates.width = transducer->tip_pressure.value() / 2;
        inputTransducer->previousCoordinates.width = transducer->tip_pressure.last.value / 2;

        inputTransducer->currentCoordinates.pressure = transducer->tip_pressure.value();
        inputTransducer->previousCoordinates.pressure = transducer->tip_pressure.last.value;

        // Force Touch emulation
        // The button state is saved in the first transducer
        // Disable Force Touch emulation for two-button trackpads
        // Pressure pads: only presses at/above the force threshold become a Force Click (see updateForceStage)
        if (force_click) {
            inputTransducer->supportsPressure = true;
            inputTransducer->isPhysicalButtonDown = false;
            inputTransducer->currentCoordinates.pressure = 0xff;
            inputTransducer->currentCoordinates.width = 10;
        }
    }
    
    // set the thumb to improve 4F pinch and spread gesture and cross-screen dragging
    if (valid_touch_count >= 4 || firstTransducer->physical_button.value()) {
        // simple thumb detection: to find the lowest finger touch in the vertical direction.
        UInt32 y_max = 0;
        int thumb_index = 0;
        for (int i = 0; i < event.contact_count; i++) {
            VoodooInputTransducer* inputTransducer = &message.transducers[i];
            if (inputTransducer->isValid && inputTransducer->currentCoordinates.y >= y_max) {
                y_max = inputTransducer->currentCoordinates.y;
                thumb_index = i;
            }
        }
        message.transducers[thumb_index].fingerType = kMT2FingerTypeThumb;
    }
        
    super::messageClient(kIOMessageVoodooInputMessage, voodooInputInstance, &message, sizeof(VoodooInputEvent));
    
    if (firstTransducer->has_secondary_button) {
        RelativePointerEvent event;
        event.dx = 0;
        event.dy = 0;
        event.buttons = firstTransducer->physical_button.value();
        event.timestamp = timestamp;
        super::messageClient(kIOMessageVoodooTrackpointRelativePointer, voodooInputInstance, &event, sizeof(event));
    }
    return MultitouchReturnBreak;
}

bool VoodooI2CNativeEngine::start(IOService* provider) {
    if (!super::start(provider))
        return false;

    parentProvider = OSDynamicCast(VoodooI2CMultitouchInterface, provider);
    
    if (!parentProvider) {
        return false;
    }
    
    voodooInputInstance = nullptr;
    
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, parentProvider->logical_max_x, 32);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, parentProvider->logical_max_y, 32);
    
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, parentProvider->physical_max_x, 32);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, parentProvider->physical_max_y, 32);

    setProperty("VoodooInputSupported", kOSBooleanTrue);
    
    stylus_check = 0;

    // Force Click configuration: defaults, Info.plist personality, then boot-args.
    forceConfigLock = IOLockAlloc();
    if (!forceConfigLock)
        return false;
    if (OSDictionary* personality = dictionaryWithProperties()) {
        applyForceClickConfig(personality, true, forceConfig);
        personality->release();
    }
    UInt32 arg;
    if (PE_parse_boot_argn("vi2c-force-mode", &arg, sizeof(arg))) {
        if (arg <= 1)
            forceConfig.mode = arg;
        else
            IOLog("%s: ignoring vi2c-force-mode=%u (0 or 1)\n", getName(), arg);
    }
    if (PE_parse_boot_argn("vi2c-force-threshold", &arg, sizeof(arg))) {
        if (arg == 0)
            forceConfig.mode = 0;                       // 0 = original behaviour, same as vi2c-force-mode=0
        else if (arg <= 255 && arg > forceConfig.hysteresis)
            forceConfig.threshold = arg;
        else
            IOLog("%s: ignoring vi2c-force-threshold=%u (0, or %u..255)\n", getName(), arg,
                  forceConfig.hysteresis + 1);
    }
    activeConfig = forceConfig;
    publishedThreshold = effectiveThreshold(activeConfig);
    publishForceClickConfig(activeConfig);

    return true;
}

// Threshold in use: the configured one, moved by the macOS "Click" setting when following it.
UInt32 VoodooI2CNativeEngine::effectiveThreshold(const ForceClickConfig& config) const {
    SInt32 t = (SInt32) config.threshold;
    if (config.followSystem)
        t += ((SInt32) systemFirmness - 1) * (SInt32) config.firmnessStep;
    const SInt32 lo = (SInt32) config.hysteresis + 1;
    if (t < lo)
        t = lo;
    if (t > 255)
        t = 255;
    return (UInt32) t;
}

// Mode 1: true while the current press should be reported as a Force Click.
// Enter: pressure reached the threshold, then stayed at/above the exit level for dwellMs.
// Leave: pressure below the exit level, or the press ended.
bool VoodooI2CNativeEngine::updateForceStage(UInt32 pressure, bool buttonDown, UInt64 now) {
    if (!buttonDown) {
        forceStageActive = false;
        forceArmed = false;
        return false;
    }
    const UInt32 threshold = effectiveThreshold(activeConfig);
    const UInt32 exitLevel = threshold - activeConfig.hysteresis;
    if (forceStageActive) {
        if (pressure < exitLevel) {
            forceStageActive = false;
            forceArmed = false;
        }
        return forceStageActive;
    }
    if (forceArmed && pressure < exitLevel)
        forceArmed = false;
    if (!forceArmed && pressure >= threshold) {
        forceArmed = true;
        forceArmedAt = now;
    }
    if (forceArmed) {
        uint64_t held_ns = 0;
        absolutetime_to_nanoseconds(now - forceArmedAt, &held_ns);
        if (held_ns >= (uint64_t) activeConfig.dwellMs * 1000000ULL)
            forceStageActive = true;
    }
    return forceStageActive;
}

// Reads the ForceClick* keys of dict into config.
// Returns kIOReturnUnsupported when dict has none of them, kIOReturnBadArgument when a value is out of
// range or the result is inconsistent (config untouched), kIOReturnSuccess otherwise.
// ignoreInvalid (Info.plist personality): skip bad keys and repair an inconsistent result instead.
IOReturn VoodooI2CNativeEngine::applyForceClickConfig(OSDictionary* dict, bool ignoreInvalid,
                                                      ForceClickConfig& config) {
    static const struct { const char* key; UInt32 ForceClickConfig::* field; UInt32 min, max; } keys[] = {
        {"ForceClickMode",         &ForceClickConfig::mode,         0, 1},
        {"ForceClickThreshold",    &ForceClickConfig::threshold,    1, 255},
        {"ForceClickHysteresis",   &ForceClickConfig::hysteresis,   0, 254},
        {"ForceClickDwellMs",      &ForceClickConfig::dwellMs,      0, 1000},
        {"ForceClickFollowSystem", &ForceClickConfig::followSystem, 0, 1},
        {"ForceClickFirmnessStep", &ForceClickConfig::firmnessStep, 0, 100},
        {"ForceClickDiagnostics",  &ForceClickConfig::diagnostics,  0, 1},
    };
    ForceClickConfig next = config;
    bool any = false;
    for (const auto& k : keys) {
        OSObject* obj = dict->getObject(k.key);
        if (!obj)
            continue;
        any = true;
        OSNumber* num = OSDynamicCast(OSNumber, obj);
        if (!num || num->unsigned64BitValue() < k.min || num->unsigned64BitValue() > k.max) {
            if (ignoreInvalid)
                continue;
            return kIOReturnBadArgument;
        }
        next.*(k.field) = num->unsigned32BitValue();
    }
    if (!any)
        return kIOReturnUnsupported;
    if (next.hysteresis >= next.threshold) {
        if (!ignoreInvalid)
            return kIOReturnBadArgument;
        next.hysteresis = next.threshold / 2;
    }
    config = next;
    return kIOReturnSuccess;
}

void VoodooI2CNativeEngine::publishForceClickConfig(const ForceClickConfig& config) {
    setProperty("ForceClickMode", config.mode, 32);
    setProperty("ForceClickThreshold", config.threshold, 32);
    setProperty("ForceClickHysteresis", config.hysteresis, 32);
    setProperty("ForceClickDwellMs", config.dwellMs, 32);
    setProperty("ForceClickFollowSystem", config.followSystem, 32);
    setProperty("ForceClickFirmnessStep", config.firmnessStep, 32);
    setProperty("ForceClickDiagnostics", config.diagnostics, 32);
    setProperty("ForceClickSystemFirmness", systemFirmness, 32);
    setProperty("ForceClickEffectiveThreshold", effectiveThreshold(config), 32);
}

// ForceClickDiagnostics: per press the highest TipPressure, whether it became a Force Click, and a trace
// of (ms since press start: 2 bytes LE, pressure: 1 byte) per report, published when the press ends.
void VoodooI2CNativeEngine::notePress(bool buttonDown, UInt32 pressure, bool forced, UInt64 now) {
    if (buttonDown) {
        if (!pressActive) {
            pressActive = true;
            pressMaxPressure = 0;
            pressForced = false;
            pressStartedAt = now;
            pressTraceLen = 0;
        }
        if (pressure > pressMaxPressure)
            pressMaxPressure = pressure;
        pressForced = pressForced || forced;
        if (pressTraceLen < kTraceMax) {
            uint64_t ns = 0;
            absolutetime_to_nanoseconds(now - pressStartedAt, &ns);
            UInt32 ms = (UInt32) (ns / 1000000ULL);
            if (ms > 0xffff)
                ms = 0xffff;
            UInt8* e = &pressTrace[pressTraceLen * 3];
            e[0] = ms & 0xff;
            e[1] = (ms >> 8) & 0xff;
            e[2] = pressure > 0xff ? 0xff : (UInt8) pressure;
            pressTraceLen++;
        }
        return;
    }
    if (!pressActive)
        return;
    pressActive = false;
    pressCount++;
    if (OSData* trace = OSData::withBytes(pressTrace, pressTraceLen * 3)) {
        setProperty("ForceClickLastPressTrace", trace);
        trace->release();
    }
    setProperty("ForceClickLastPressMax", pressMaxPressure, 32);
    setProperty("ForceClickLastPressForced", pressForced);
    setProperty("ForceClickPressCount", pressCount, 32);   // last: readers use it as the "press done" signal
}

// Runtime tuning without a reboot: an administrator can set any ForceClick* key, for example with
// Utilities/ForceClick/vi2c-forcectl. Other keys go to IOService.
IOReturn VoodooI2CNativeEngine::setProperties(OSObject* properties) {
    OSDictionary* dict = OSDynamicCast(OSDictionary, properties);
    if (!dict || !forceConfigLock)
        return super::setProperties(properties);
    ForceClickConfig probe;
    if (applyForceClickConfig(dict, false, probe) == kIOReturnUnsupported)
        return super::setProperties(properties);
    if (IOUserClient::clientHasPrivilege(current_task(), kIOClientPrivilegeAdministrator) != kIOReturnSuccess)
        return kIOReturnNotPrivileged;
    IOLockLock(forceConfigLock);
    ForceClickConfig next = forceConfig;
    IOReturn ret = applyForceClickConfig(dict, false, next);
    if (ret == kIOReturnSuccess) {
        forceConfig = next;
        forceConfigChanged = true;
    }
    IOLockUnlock(forceConfigLock);
    if (ret != kIOReturnSuccess)
        return ret;
    publishForceClickConfig(next);
    IOLog("%s: Force Click mode %u threshold %u hysteresis %u dwell %u ms follow %u step %u diagnostics %u\n",
          getName(), next.mode, next.threshold, next.hysteresis, next.dwellMs, next.followSystem,
          next.firmnessStep, next.diagnostics);
    return kIOReturnSuccess;
}

bool VoodooI2CNativeEngine::isForceClickEnabled() {
    AbsoluteTime now_abs;
    uint64_t diff_ns;
    clock_get_uptime(&now_abs);
    absolutetime_to_nanoseconds(now_abs - lastForceClickPropertyUpdateTime, &diff_ns);

    if (diff_ns < 1e9) {
        lastForceClickPropertyUpdateTime = now_abs;
        return lastIsForceClickEnabled;
    }
    
    // Blocking reading here takes about 10 microseconds
    OSDictionary* dict = OSDynamicCast(OSDictionary, this->getProperty("MultitouchPreferences", gIOServicePlane, kIORegistryIterateRecursively));
    if (!dict) {
        return lastIsForceClickEnabled;
    }
    if (OSCollectionIterator* i = OSCollectionIterator::withCollection(dict)) {
        while (OSSymbol* key = OSDynamicCast(OSSymbol, i->getNextObject())) {
            // If tap to click (property "Clicking") is disabled,
            // then we should forbid force clicking,
            // because it makes impossible to perform a non-force click
            // System Settings -> Trackpad -> Click (light 0, medium 1, firm 2)
            if (key->isEqualTo("FirstClickThreshold")) {
                OSNumber* firmness = OSDynamicCast(OSNumber, dict->getObject(key));
                if (firmness != nullptr && firmness->unsigned32BitValue() <= 2)
                    systemFirmness = firmness->unsigned32BitValue();
            }
            if (key->isEqualTo("Clicking")) {
                OSBoolean *boolValue;
                OSNumber *numValue;
                if ((boolValue = OSDynamicCast(OSBoolean, dict->getObject(key))) != nullptr) {
                    if (!boolValue->getValue()) {
                        lastIsForceClickEnabled = false;
                        break;
                    }
                }
                // is it needed? in default properties it's number, not boolean
                else if ((numValue = OSDynamicCast(OSNumber, dict->getObject(key))) != nullptr && !numValue->unsigned64BitValue()) {
                    lastIsForceClickEnabled = false;
                    break;
                }
            }
            // System -> Preferences -> Trackpad -> Force Click and haptic feedback
            // ForceSuppressed
            if (key->isEqualTo("ForceSuppressed")) {
                OSBoolean* value = OSDynamicCast(OSBoolean, dict->getObject(key));

                if (value != nullptr) {
                    lastIsForceClickEnabled = !value->getValue();
                }
            }
        }

        i->release();
    }

    lastForceClickPropertyUpdateTime = now_abs;
    return lastIsForceClickEnabled;
}

void VoodooI2CNativeEngine::onPropertyChange() {
    if (voodooInputInstance) {
        super::messageClient(kIOMessageVoodooInputUpdatePropertiesNotification, voodooInputInstance);
    }
}

void VoodooI2CNativeEngine::free() {
    if (forceConfigLock) {
        IOLockFree(forceConfigLock);
        forceConfigLock = nullptr;
    }
    super::free();
}

void VoodooI2CNativeEngine::stop(IOService* provider) {
    super::stop(provider);
}

bool VoodooI2CNativeEngine::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    if (forClient && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)) {
        voodooInputInstance = forClient;
        voodooInputInstance->retain();
        
        return true;
    }
    return false;
}

bool VoodooI2CNativeEngine::handleIsOpen(const IOService *forClient) const {
    return voodooInputInstance != nullptr && forClient == voodooInputInstance;
}

void VoodooI2CNativeEngine::handleClose(IOService *forClient, IOOptionBits options) {
    if (voodooInputInstance && forClient == voodooInputInstance) {
        OSSafeReleaseNULL(voodooInputInstance);
    }
}

bool VoodooI2CNativeEngine::willTerminate(IOService *provider, IOOptionBits options) {
    return super::willTerminate(provider, options);
}
