//
//  VoodooI2CNativeEngine.hpp
//  VoodooI2C
//
//  Created by Alexandre on 10/02/2018.
//  Copyright © 2018 Alexandre Daoud and Kishor Prins. All rights reserved.
//

#ifndef VoodooI2CNativeEngine_hpp
#define VoodooI2CNativeEngine_hpp

#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOService.h>

#include "../VoodooI2CMultitouchInterface.hpp"
#include "../VoodooI2CMultitouchEngine.hpp"


#include "VoodooInputMultitouch/VoodooInputTransducer.h"
#include "VoodooInputMultitouch/VoodooInputMessages.h"

class EXPORT VoodooI2CNativeEngine : public VoodooI2CMultitouchEngine {
    OSDeclareDefaultStructors(VoodooI2CNativeEngine);
    
    VoodooInputEvent message;
    VoodooI2CMultitouchInterface* parentProvider;
    IOService* voodooInputInstance;

    bool lastIsForceClickEnabled = false;
    AbsoluteTime lastForceClickPropertyUpdateTime;

    bool isForceClickEnabled();

    // Force Click for pressure pads (button-less touchpads that report TipPressure),
    // see Documentation/Force Click.md.
    //   ForceClickMode 0: original emulation, every physical press is a Force Click
    //   ForceClickMode 1: a press is a normal click until TipPressure reaches the threshold and then stays at
    //                     or above threshold - hysteresis for ForceClickDwellMs; it is a Force Click from then
    //                     on until the pressure drops below threshold - hysteresis or the press ends
    // Devices that never report a non-zero pressure always use mode 0.
    // Sources, later wins: defaults, Info.plist personality, boot-args, setProperties (administrator only).
    struct ForceClickConfig {
        UInt32 mode = 1;
        UInt32 threshold = 160;
        UInt32 hysteresis = 40;
        UInt32 dwellMs = 0;
        UInt32 followSystem = 1;      // move the threshold with the macOS "Click" setting
        UInt32 firmnessStep = 20;     // threshold change per step: light -1, medium 0, firm +1
        UInt32 diagnostics = 0;       // publish per-press data for tuning
    };
    ForceClickConfig forceConfig;     // latest configuration, protected by forceConfigLock
    ForceClickConfig activeConfig;    // snapshot used by the report path
    bool forceConfigChanged = false;  // protected by forceConfigLock
    IOLock* forceConfigLock = nullptr;

    UInt32 systemFirmness = 1;        // macOS FirstClickThreshold: 0 light, 1 medium, 2 firm
    UInt32 publishedThreshold = 0;
    bool pressureSeen = false;
    bool forceStageActive = false;
    bool forceArmed = false;
    UInt64 forceArmedAt = 0;

    // ForceClickDiagnostics: trace of (ms since press start, max TipPressure) per report of the last press
    static constexpr UInt32 kTraceMax = 200;
    UInt8 pressTrace[kTraceMax * 3];
    UInt32 pressTraceLen = 0;
    UInt64 pressStartedAt = 0;
    bool pressActive = false;
    bool pressForced = false;
    UInt32 pressMaxPressure = 0;
    UInt32 pressCount = 0;

    UInt32 effectiveThreshold(const ForceClickConfig& config) const;
    bool updateForceStage(UInt32 pressure, bool buttonDown, UInt64 now);
    IOReturn applyForceClickConfig(OSDictionary* dict, bool ignoreInvalid, ForceClickConfig& config);
    void publishForceClickConfig(const ForceClickConfig& config);
    void notePress(bool buttonDown, UInt32 pressure, bool forced, UInt64 now);
 public:
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    void free() override;
    
    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
    bool handleIsOpen(const IOService *forClient) const override;
    void handleClose(IOService *forClient, IOOptionBits options) override;
    void onPropertyChange() override;
    bool willTerminate(IOService* provider, IOOptionBits options) override;
    IOReturn setProperties(OSObject* properties) override;
    
    MultitouchReturn handleInterruptReport(VoodooI2CMultitouchEvent event, AbsoluteTime timestamp);
 private:
    int stylus_check = 0;
};


#endif /* VoodooI2CNativeEngine_hpp */
