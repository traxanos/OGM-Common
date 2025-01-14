#include "OpenKNX/Common.h"
#include "OpenKNX/Facade.h"
#include "OpenKNX/Stat/RuntimeStat.h"

namespace OpenKNX
{
    std::string Common::logPrefix()
    {
        return "Common";
    }

    void Common::init(uint8_t firmwareRevision)
    {
        ArduinoPlatform::SerialDebug = new OpenKNX::Log::VirtualSerial("KNX");

        openknx.timerInterrupt.init();
        openknx.hardware.initLeds();

#if defined(ARDUINO_ARCH_RP2040) && defined(OPENKNX_RECOVERY_ON)
        // Recovery
        processRecovery();
#endif

#ifdef OPENKNX_NO_BOOT_PULSATING
        openknx.progLed.on();
    #ifdef INFO1_LED_PIN
        openknx.info1Led.on();
    #endif
#else
        openknx.progLed.pulsing();
    #ifdef INFO1_LED_PIN
        openknx.info1Led.pulsing();
    #endif
#endif

        debugWait();

        logInfoP("Init firmware");

#ifdef OPENKNX_DEBUG
        showDebugInfo();
#endif

        openknx.hardware.initFlash();
        openknx.info.serialNumber(knx.platform().uniqueSerialNumber());
        openknx.info.firmwareRevision(firmwareRevision);

        initKnx();

        openknx.hardware.init();
    }

#ifdef OPENKNX_DEBUG
    void Common::showDebugInfo()
    {
        logDebugP("Debug logging is enabled!");
    #if defined(OPENKNX_TRACE1) || defined(OPENKNX_TRACE2) || defined(OPENKNX_TRACE3) || defined(OPENKNX_TRACE4) || defined(OPENKNX_TRACE5)
        logDebugP("Trace logging is enabled with:");
        logIndentUp();
        #ifdef OPENKNX_TRACE1
        logDebugP("Filter 1: %s", TRACE_STRINGIFY(OPENKNX_TRACE1));
        #endif
        #ifdef OPENKNX_TRACE2
        logDebugP("Filter 2: %s", TRACE_STRINGIFY(OPENKNX_TRACE2));
        #endif
        #ifdef OPENKNX_TRACE3
        logDebugP("Filter 3: %s", TRACE_STRINGIFY(OPENKNX_TRACE3));
        #endif
        #ifdef OPENKNX_TRACE4
        logDebugP("Filter 4: %s", TRACE_STRINGIFY(OPENKNX_TRACE4));
        #endif
        #ifdef OPENKNX_TRACE5
        logDebugP("Filter 5: %s", TRACE_STRINGIFY(OPENKNX_TRACE5));
        #endif
        logIndentDown();
    #endif
    }
#endif

#if defined(ARDUINO_ARCH_RP2040) && defined(OPENKNX_RECOVERY_ON)
    void Common::processRecovery()
    {
        uint8_t mode = 0;
        uint32_t recoveryStart = millis();
        pinMode(PROG_BUTTON_PIN, INPUT_PULLUP);
        while (digitalRead(PROG_BUTTON_PIN) == OPENKNX_RECOVERY_ON && mode < 3)
        {
            if (mode == 0 && delayCheck(recoveryStart, 500))
            {
                hardware.progLed.blinking(400);
                mode++;
            }

            if (mode == 1 && delayCheck(recoveryStart, 5500))
            {
                hardware.progLed.blinking(200);
                mode++;
            }

            if (mode == 2 && delayCheck(recoveryStart, 10500))
            {
                mode++;
                hardware.progLed.on();
            }
        }

        switch (mode)
        {
            case 1: // usbMode
                reset_usb_boot(0, 0);
                break;
            case 2: // nukeFLash KNX
                __nukeFlash(KNX_FLASH_OFFSET, KNX_FLASH_SIZE);
                break;
            case 3: // nukeFLash
                __nukeFlash(0, NUKE_FLASH_SIZE_BYTES);
                break;
        }
    }
#endif

    void Common::initKnx()
    {
        logInfoP("Init knx stack");
        logIndentUp();

#if defined(ARDUINO_ARCH_RP2040) && defined(KNX_SERIAL) && defined(KNX_UART_RX_PIN) && defined(KNX_UART_TX_PIN)
        KNX_SERIAL.setRX(KNX_UART_RX_PIN);
        KNX_SERIAL.setTX(KNX_UART_TX_PIN);
#endif

        // pin or GPIO programming button is connected to. Default is 0
        knx.buttonPin(PROG_BUTTON_PIN);
        // Is the interrupt created in RISING or FALLING signal? Default is RISING
        // knx.buttonPinInterruptOn(PROG_BUTTON_PIN_INTERRUPT_ON);

        knx.ledPin(0);
        knx.setProgLedOnCallback([]() -> void {
            openknx.progLed.forceOn(true);
        });
        knx.setProgLedOffCallback([]() -> void {
            openknx.progLed.forceOn(false);
        });

        uint8_t hardwareType[LEN_HARDWARE_TYPE] = {0x00, 0x00, MAIN_OpenKnxId, MAIN_ApplicationNumber, MAIN_ApplicationVersion, 0x00};

        // first setup flash version check
        knx.bau().versionCheckCallback(versionCheck);
        // set correct hardware type for flash compatibility check
        knx.bau().deviceObject().hardwareType(hardwareType);
        // read flash data
        knx.readMemory();
        // set hardware type again, in case an other hardware type was deserialized from flash
        knx.bau().deviceObject().hardwareType(hardwareType);
        // set firmware version as user info (PID_VERSION)
        // 5 bit revision, 5 bit major, 6 bit minor
        // output in ETS as [revision] major.minor
        knx.bau().deviceObject().version(openknx.info.firmwareVersion());

#ifdef MAIN_OrderNumber
        knx.orderNumber((const uint8_t*)MAIN_OrderNumber); // set the OrderNumber
#endif
        logIndentDown();
    }

    VersionCheckResult Common::versionCheck(uint16_t manufacturerId, uint8_t* hardwareType, uint16_t firmwareVersion)
    {
        // save ets app data in information struct
        openknx.info.applicationNumber((hardwareType[2] << 8) | hardwareType[3]);
        openknx.info.applicationVersion(hardwareType[4]);

        if (manufacturerId != 0x00FA)
        {
            logError(openknx.common.logPrefix(), "This firmware supports only ManufacturerID 0x00FA");
            return FlashAllInvalid;
        }

        // hardwareType has the format 0x00 00 Ap nn vv 00
        if (memcmp(knx.bau().deviceObject().hardwareType(), hardwareType, 4) != 0)
        {
            logError(openknx.common.logPrefix(), "MAIN_ApplicationVersion changed, ETS has to reprogram the application!");
            return FlashAllInvalid;
        }

        if (knx.bau().deviceObject().hardwareType()[4] != hardwareType[4])
        {
            logError(openknx.common.logPrefix(), "MAIN_ApplicationVersion changed, ETS has to reprogram the application!");
            return FlashTablesInvalid;
        }

        return FlashValid;
    }

    void Common::debugWait()
    {
#ifdef OPENKNX_NO_BOOT_PULSATING
        openknx.progLed.blinking();
    #ifdef INFO1_LED_PIN
        openknx.info1Led.blinking();
    #endif
#else
        openknx.progLed.pulsing(500);
    #ifdef INFO1_LED_PIN
        openknx.info1Led.pulsing(500);
    #endif
#endif

#if OPENKNX_WAIT_FOR_SERIAL > 1 && !defined(OPENKNX_RTT) && defined(SERIAL_DEBUG)
        uint32_t timeoutBase = millis();
        while (!SERIAL_DEBUG)
        {
            if (delayCheck(timeoutBase, OPENKNX_WAIT_FOR_SERIAL))
                break;
        }
#endif

#ifdef OPENKNX_NO_BOOT_PULSATING
        openknx.progLed.on();
    #ifdef INFO1_LED_PIN
        openknx.info1Led.on();
    #endif
#else
        openknx.progLed.pulsing();
    #ifdef INFO1_LED_PIN
        openknx.info1Led.pulsing();
    #endif
#endif
    }

    void Common::setup()
    {
        // Handle init of modules
        for (uint8_t i = 0; i < openknx.modules.count; i++)
            openknx.modules.list[i]->init();

#ifdef LOG_StartupDelayBase
        _startupDelay = millis();
#endif
#ifdef LOG_HeartbeatDelayBase
        _heartbeatDelay = 0;
#endif

        bool configured = knx.configured();

        // Handle setup of modules
        for (uint8_t i = 0; i < openknx.modules.count; i++)
            openknx.modules.list[i]->setup(configured);

        if (configured) openknx.flash.load();

        // start the framework
        openknx.progLed.off();
        knx.start();

        // when module was restarted during bcu was disabled, reenable
        openknx.hardware.activatePowerRail();

#ifdef OPENKNX_WATCHDOG
        watchdogSetup();
#endif

        // register callbacks
        registerCallbacks();

        // setup0 is done
        _setup0Ready = true;

#ifdef OPENKNX_DUALCORE
        // if we have a second core wait for setup1 is done
        if (openknx.usesDualCore())
            while (!_setup1Ready)
                delay(1);
#endif

#ifdef INFO1_LED_PIN
        // setup complete: turn info1Led off
        openknx.info1Led.off();
#endif

        openknx.logger.logOpenKnxHeader();
    }

#ifdef OPENKNX_DUALCORE
    void Common::setup1()
    {
        openknx.timerInterrupt.init1();

        // wait for setup0
        while (!_setup0Ready)
            delay(50);

        // skip if no dual core is used
        if (!openknx.usesDualCore())
        {
            logErrorP("setup1 is invoked without utilizing dual-core modules");
            _setup1Ready = true;
            return;
        }

        bool configured = knx.configured();

        // Handle loop of modules
        for (uint8_t i = 0; i < openknx.modules.count; i++)
            openknx.modules.list[i]->setup1(configured);

        _setup1Ready = true;
    }
#endif

#ifdef OPENKNX_WATCHDOG
    void Common::watchdogSetup()
    {
        if (!ParamLOG_Watchdog) return;

        logInfo("Watchdog", "Start with a watchtime of %ims", OPENKNX_WATCHDOG_MAX_PERIOD);
    #if defined(ARDUINO_ARCH_SAMD)
        // used for Diagnose command
        watchdog.resetCause = Watchdog.resetCause();

        // setup watchdog to prevent endless loops
        Watchdog.enable(OPENKNX_WATCHDOG_MAX_PERIOD, false);
    #elif defined(ARDUINO_ARCH_RP2040)
        Watchdog.enable(OPENKNX_WATCHDOG_MAX_PERIOD);
    #endif
    }
    void Common::watchdogLoop()
    {
        if (!delayCheck(watchdog.timer, OPENKNX_WATCHDOG_MAX_PERIOD / 10)) return;
        if (!ParamLOG_Watchdog) return;

        Watchdog.reset();
        watchdog.timer = millis();
    }
#endif

    // main loop
    void Common::loop()
    {
        _skipLooptimeWarning = false;

        uptime(false);

        if (!_setup0Ready) return;
#ifdef OPENKNX_DUALCORE
        if (!_setup1Ready) return;
#endif

        RUNTIME_MEASURE_BEGIN(_runtimeLoop);

#ifdef OPENKNX_HEARTBEAT
        openknx.progLed.debugLoop();
#endif

#ifdef OPENKNX_LOOPTIME_WARNING
        uint32_t start = millis();
#endif

        // loop console helper
        RUNTIME_MEASURE_BEGIN(_runtimeConsole);
        openknx.console.loop();
        RUNTIME_MEASURE_END(_runtimeConsole);

        // loop  knx stack
        RUNTIME_MEASURE_BEGIN(_runtimeKnxStack);
        knx.loop();
        RUNTIME_MEASURE_END(_runtimeKnxStack);

        // loop  appstack
        _loopMicros = micros();

        // knx is configured
        if (knx.configured())
        {

#ifdef LOG_HeartbeatDelayBase
            // Handle heartbeat delay
            processHeartbeat();
#endif

            processSavePin();
            processRestoreSavePin();
            processAfterStartupDelay();
        }

        RUNTIME_MEASURE_BEGIN(_runtimeModuleLoop);
        processModulesLoop();
        RUNTIME_MEASURE_END(_runtimeModuleLoop);

#ifdef OPENKNX_WATCHDOG
        watchdogLoop();
#endif

        RUNTIME_MEASURE_END(_runtimeLoop);

#if OPENKNX_LOOPTIME_WARNING > 1
        // loop took to long and last out is min 1ms ago
        if (!_skipLooptimeWarning && delayCheck(start, OPENKNX_LOOPTIME_WARNING) && delayCheck(_lastLooptimeWarning, OPENKNX_LOOPTIME_WARNING_INTERVAL))
        {
            logErrorP("Warning: The loop took longer than usual (%i >= %i)", (millis() - start), OPENKNX_LOOPTIME_WARNING);
            _lastLooptimeWarning = millis();
        }
#endif
    }

    void Common::skipLooptimeWarning()
    {
#if OPENKNX_LOOPTIME_WARNING > 1
        _skipLooptimeWarning = true;
#endif
    }

    bool Common::freeLoopTime()
    {
        return !delayCheckMicros(_loopMicros, OPENKNX_MAX_LOOPTIME);
    }

    bool Common::freeLoopIterate(uint8_t size, uint8_t& position, uint8_t& processed)
    {
        processed++;
        position++;

        // when you have to start from the beginning again
        if (position >= size) position = 0;

        // if freeloop time over
        if (!freeLoopTime()) return false;

        // once completely run through
        if (processed >= size) return false;

        return true;
    }

    void __time_critical_func(Common::collectMemoryStats)()
    {
        // int current = freeMemory();
        _freeMemoryMin = MIN(_freeMemoryMin, freeMemory());
#ifdef ARDUINO_ARCH_RP2040
    #ifdef OPENKNX_DUALCORE
        if (rp2040.cpuid())
            _freeStackMin1 = MIN(_freeStackMin1, freeStackSize());
        else
    #endif
            _freeStackMin = MIN(_freeStackMin, freeStackSize());
#endif
    }

    /**
     * Run loop() of as many modules as possible, within available free loop time.
     * Each module will be processed 0 or 1 times only, not more.
     *
     * Repeated calls will resume with the first/next unprocessed module.
     * For all modules the min/max number of loop()-calls will not differ more than 1.
     */
    void Common::processModulesLoop()
    {
        // Skip if no modules have been added (for testing)
        if (openknx.modules.count == 0) return;

        bool configured = knx.configured();

        uint8_t processed = 0;
        do
        {
            RUNTIME_MEASURE_BEGIN(openknx.modules.runtime[_currentModule]);
            openknx.modules.list[_currentModule]->loop(configured);
            RUNTIME_MEASURE_END(openknx.modules.runtime[_currentModule]);
        }
        while (freeLoopIterate(openknx.modules.count, _currentModule, processed));
    }

#ifdef OPENKNX_DUALCORE
    void Common::loop1()
    {
        if (!_setup1Ready) return;

    #ifdef OPENKNX_HEARTBEAT
        #ifdef INFO1_LED_PIN
        openknx.info1Led.debugLoop();
        #endif
    #endif

        bool configured = knx.configured();

        for (uint8_t i = 0; i < openknx.modules.count; i++)
        {
            RUNTIME_MEASURE_BEGIN(openknx.modules.runtime1[i]);
            openknx.modules.list[i]->loop1(configured);
            RUNTIME_MEASURE_END(openknx.modules.runtime1[i]);
        }
    }
#endif

    bool Common::afterStartupDelay()
    {
        return _afterStartupDelay;
    }

    void Common::processAfterStartupDelay()
    {
        if (afterStartupDelay())
            return;

#ifdef LOG_StartupDelayBase
        if (!delayCheck(_startupDelay, ParamLOG_StartupDelayTimeMS))
            return;
#endif

        logInfoP("processAfterStartupDelay");
        logIndentUp();

        _afterStartupDelay = true;

        for (uint8_t i = 0; i < openknx.modules.count; i++)
        {
            openknx.modules.list[i]->processAfterStartupDelay();
        }

        logIndentDown();
    }

#ifdef LOG_HeartbeatDelayBase
    void Common::processHeartbeat()
    {
        // the first heartbeat is send directly after startup delay of the device
        if (_heartbeatDelay == 0 || delayCheck(_heartbeatDelay, ParamLOG_HeartbeatDelayTimeMS))
        {
            // we waited enough, let's send a heartbeat signal
            KoLOG_Heartbeat.value(true, DPT_Switch);
            _heartbeatDelay = millis();
        }
    }
#endif

    void Common::triggerSavePin()
    {
        _savePinTriggered = true;
    }

    void Common::processSavePin()
    {
        // savePin not triggered
        if (!_savePinTriggered)
            return;

        // processSavePin already called
        if (_savedPinProcessed > 0)
            return;

        uint32_t start = millis();
        openknx.common.skipLooptimeWarning();

        logErrorP("SavePIN triggered!");
        logIndentUp();
        logInfoP("Save power");
        logIndentUp();

        openknx.progLed.powerSave();
#ifdef INFO1_LED_PIN
        openknx.info1Led.powerSave();
#endif
#ifdef INFO1_LED_PIN
        openknx.info1Led.powerSave();
#endif
#ifdef INFO2_LED_PIN
        openknx.info2Led.powerSave();
#endif
        openknx.hardware.stopKnxMode(false);

        // first save all modules to save power before...
        for (uint8_t i = 0; i < openknx.modules.count; i++)
            openknx.modules.list[i]->savePower();

        openknx.hardware.deactivatePowerRail();

        logInfoP("Completed (%ims)", millis() - start);
        logIndentDown();

        // save data
        openknx.flash.save();

        // manual recevie stopKnxMode respone
        uint8_t response[2] = {};
        openknx.hardware.receiveResponseFromBcu(response, 2); // Receive 2 bytes

        _savedPinProcessed = millis();
        logIndentDown();
    }
    void Common::processRestoreSavePin()
    {
        // savePin not triggered
        if (!_savePinTriggered)
            return;

        if (!delayCheck(_savedPinProcessed, 1000))
            return;

        openknx.common.skipLooptimeWarning();
        logInfoP("Restore power (after 1s)");
        logIndentUp();

        openknx.progLed.powerSave(false);
#ifdef INFO1_LED_PIN
        openknx.info1Led.powerSave(false);
#endif
#ifdef INFO1_LED_PIN
        openknx.info1Led.powerSave(false);
#endif
#ifdef INFO2_LED_PIN
        openknx.info2Led.powerSave(false);
#endif
        openknx.hardware.activatePowerRail();
        openknx.hardware.startKnxMode();

        bool reboot = false;

        // the inform modules
        for (uint8_t i = 0; i < openknx.modules.count; i++)
            if (!openknx.modules.list[i]->restorePower())
            {
                reboot = true;
                break;
            }

        if (reboot)
        {
            logInfoP("Need reboot");
            delay(10);
            knx.platform().restart();
        }

        logInfoP("Restore power without reboot was successful");

        _savePinTriggered = false;
        _savedPinProcessed = 0;
        logIndentDown();
    }

    void Common::processBeforeRestart()
    {
        logInfoP("processBeforeRestart");
        logIndentUp();
        for (uint8_t i = 0; i < openknx.modules.count; i++)
        {
            openknx.modules.list[i]->processBeforeRestart();
        }

        openknx.flash.save();
        logIndentDown();
    }

    void Common::processBeforeTablesUnload()
    {
        logInfoP("processBeforeTablesUnload");
        logIndentUp();
        for (uint8_t i = 0; i < openknx.modules.count; i++)
        {
            openknx.modules.list[i]->processBeforeTablesUnload();
        }

        openknx.flash.save();
        logIndentDown();
    }

#if (MASK_VERSION & 0x0900) != 0x0900 // Coupler do not have GroupObjects
    void Common::processInputKo(GroupObject& ko)
    {
    #ifdef LOG_KoDiagnose
        if (ko.asap() == LOG_KoDiagnose)
        {
            openknx.console.processDiagnoseKo(ko);
            return;
        }
    #endif

        for (uint8_t i = 0; i < openknx.modules.count; i++)
        {
            openknx.modules.list[i]->processInputKo(ko);
        }
    }

#endif

    void Common::registerCallbacks()
    {
        // Register Callbacks for FunctionProperty also when knx ist not configured
        knx.bau().functionPropertyCallback([](uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t* data, uint8_t* resultData, uint8_t& resultLength) -> bool {
            return openknx.common.processFunctionProperty(objectIndex, propertyId, length, data, resultData, resultLength);
        });
        knx.bau().functionPropertyStateCallback([](uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t* data, uint8_t* resultData, uint8_t& resultLength) -> bool {
            return openknx.common.processFunctionPropertyState(objectIndex, propertyId, length, data, resultData, resultLength);
        });

        // abort if knx not configured
        if (!knx.configured()) return;

        knx.beforeRestartCallback([]() -> void {
            openknx.common.processBeforeRestart();
        });
#if (MASK_VERSION & 0x0900) != 0x0900 // Coupler do not have GroupObjects
        GroupObject::classCallback([](GroupObject& iKo) -> void {
            openknx.common.processInputKo(iKo);
        });
#endif
        TableObject::beforeTablesUnloadCallback([]() -> void {
            openknx.common.processBeforeTablesUnload();
        });
#ifdef SAVE_INTERRUPT_PIN
        // we need to do this as late as possible, tried in constructor, but this doesn't work on RP2040
        pinMode(SAVE_INTERRUPT_PIN, INPUT);
        attachInterrupt(
            digitalPinToInterrupt(SAVE_INTERRUPT_PIN), []() -> void {
                openknx.common.triggerSavePin();
            },
            FALLING);
#endif
    }

    uint Common::freeMemoryMin()
    {
        return _freeMemoryMin;
    }

#ifdef ARDUINO_ARCH_RP2040
    int Common::freeStackMin()
    {
        return _freeStackMin;
    }
    #ifdef OPENKNX_DUALCORE
    int Common::freeStackMin1()
    {
        return _freeStackMin1;
    }
    #endif
#endif

    bool Common::processFunctionProperty(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t* data, uint8_t* resultData, uint8_t& resultLength)
    {
        for (uint8_t i = 0; i < openknx.modules.count; i++)
            if (openknx.modules.list[i]->processFunctionProperty(objectIndex, propertyId, length, data, resultData, resultLength))
                return true;

        return false;
    }

    bool Common::processFunctionPropertyState(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t* data, uint8_t* resultData, uint8_t& resultLength)
    {
        for (uint8_t i = 0; i < openknx.modules.count; i++)
            if (openknx.modules.list[i]->processFunctionPropertyState(objectIndex, propertyId, length, data, resultData, resultLength))
                return true;

        return false;
    }

#ifdef OPENKNX_RUNTIME_STAT
    void Common::showRuntimeStat(const bool stat /*= true*/, const bool hist /*= false*/)
    {
        logInfoP("Runtime Statistics: (Uptime=%dms)", millis());
        logIndentUp();
        {
            Stat::RuntimeStat::showStatHeader();
            // Use prefix '_' to preserve structure on sorting
            _runtimeLoop.showStat("___Loop", 0, stat, hist);
            _runtimeConsole.showStat("__Console", 0, stat, hist);
            _runtimeKnxStack.showStat("__KnxStack", 0, stat, hist);
            _runtimeModuleLoop.showStat("_All_Modules_Loop", 0, stat, hist);
            for (uint8_t i = 0; i < openknx.modules.count; i++)
            {
                openknx.modules.runtime[i].showStat(openknx.modules.list[i]->name().c_str(), 0, stat, hist);
    #ifdef OPENKNX_DUALCORE
                openknx.modules.runtime1[i].showStat(openknx.modules.list[i]->name().c_str(), 1, stat, hist);
    #endif
            }
        }
        logIndentDown();
    }
#endif

} // namespace OpenKNX
