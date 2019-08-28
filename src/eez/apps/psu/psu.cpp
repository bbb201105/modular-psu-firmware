/* / mcu / sound.h
 * EEZ PSU Firmware
 * Copyright (C) 2015-present, Envox d.o.o.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <eez/apps/psu/psu.h>

#include <eez/system.h>

#include <eez/apps/psu/init.h>
#include <eez/apps/psu/serial_psu.h>

#if OPTION_ETHERNET
#include <eez/apps/psu/ethernet.h>
#include <eez/apps/psu/ntp.h>
#endif

#include <eez/apps/psu/board.h>
#include <eez/apps/psu/dac.h>
#include <eez/apps/psu/datetime.h>
#include <eez/apps/psu/eeprom.h>
#include <eez/apps/psu/ioexp.h>
#include <eez/apps/psu/persist_conf.h>
#include <eez/apps/psu/rtc.h>
#include <eez/apps/psu/temperature.h>
#include <eez/modules/psu/adc.h>
#include <eez/sound.h>

#if OPTION_SD_CARD
#include <eez/apps/psu/dlog.h>
#include <eez/apps/psu/sd_card.h>
#endif

#include <eez/apps/psu/calibration.h>
#include <eez/apps/psu/profile.h>

#if OPTION_DISPLAY
#include <eez/apps/psu/gui/psu.h>
#endif

#if OPTION_WATCHDOG
#include <eez/apps/psu/watchdog.h>
#endif
#include <eez/apps/psu/fan.h>

#include <eez/apps/psu/channel_dispatcher.h>
#include <eez/apps/psu/event_queue.h>
#include <eez/apps/psu/idle.h>
#include <eez/apps/psu/io_pins.h>
#include <eez/apps/psu/list_program.h>
#include <eez/apps/psu/trigger.h>

#include <eez/modules/bp3c/relays.h>
#include <eez/modules/dcpX05/eeprom.h>
#include <eez/index.h>

// TODO move this to some other place
#if OPTION_ENCODER
#include <eez/modules/mcu/encoder.h>
#endif

#include <eez/scpi/scpi.h>

namespace eez {

using namespace scpi;

TestResult g_masterTestResult;

namespace psu {

using namespace scpi;

bool g_isBooted = false;
static bool g_bootTestSuccess;
static bool g_powerIsUp = false;
static bool g_testPowerUpDelay = false;
static uint32_t g_powerDownTime;

static MaxCurrentLimitCause g_maxCurrentLimitCause;

RLState g_rlState = RL_STATE_LOCAL;

bool g_rprogAlarm = false;

ontime::Counter g_powerOnTimeCounter(ontime::ON_TIME_COUNTER_POWER);

void (*g_diagCallback)();

////////////////////////////////////////////////////////////////////////////////

static bool testMaster();

////////////////////////////////////////////////////////////////////////////////

void loadConf() {
    // loads global configuration parameters
    persist_conf::loadDevice();

    // loads global configuration parameters block 2
    persist_conf::loadDevice2();

    // load channels calibration parameters
    for (int i = 0; i < CH_NUM; ++i) {
        persist_conf::loadChannelCalibration(Channel::get(i));
    }
}

////////////////////////////////////////////////////////////////////////////////

extern Channel channels[CH_MAX];

void init() {
    for (int i = 0; i < CH_MAX; i++) {
    	channels[i].index = i + 1;
    }

    sound::init();

    eeprom::init();
    eeprom::test();

#if OPTION_SD_CARD
    sd_card::init();
#endif

    bp3c::relays::init();

    for (int i = 0; i < CH_MAX; i++) {
        uint16_t value;
        if (!dcpX05::eeprom::read(i, (uint8_t *)&value, 2, (uint16_t)0)) {
            g_slots[i].moduleType = MODULE_TYPE_NONE;
            channels[i].set(i, CH_BOARD_REVISION_NONE, CH_PARAMS_NONE);
        } else if (value == 405) {
            g_slots[i].moduleType = MODULE_TYPE_DCP405;
            channels[i].set(i, CH_BOARD_REVISION_DCP405_R1B1, CH_PARAMS_40V_5A);
        } else if (value == 406) {
            g_slots[i].moduleType = MODULE_TYPE_DCP405;
            channels[i].set(i, CH_BOARD_REVISION_DCP405_R2B5, CH_PARAMS_40V_5A);
        } else if (value == 505) {
            g_slots[i].moduleType = MODULE_TYPE_DCP505;
            channels[i].set(i, CH_BOARD_REVISION_DCP505_R1B3, CH_PARAMS_50V_5A);
        } else if (value == 220) {
            g_slots[i].moduleType = MODULE_TYPE_DCM220;
            channels[i].set(i, CH_BOARD_REVISION_DCM220_R1B1, CH_PARAMS_20V_4A);
        } else {
            g_slots[i].moduleType = MODULE_TYPE_NONE;
            channels[i].set(i, CH_BOARD_REVISION_NONE, CH_PARAMS_NONE);
        }
    }
    CH_NUM = CH_MAX;

    g_powerOnTimeCounter.init();
    for (int i = 0; i < CH_NUM; i++) {
    	Channel::get(i).onTimeCounter.setType(i + 1);
        Channel::get(i).onTimeCounter.init();
    }

    loadConf();

    serial::init();

    rtc::init();
    datetime::init();

    event_queue::init();

    list::init();

#if OPTION_ETHERNET
    ethernet::init();
    ntp::init();
#else
    DebugTrace("Ethernet initialization skipped!");
#endif

    fan::init();

    temperature::init();

    trigger::init();

    // TODO move this to some other place
#if OPTION_ENCODER
    mcu::encoder::init();
#endif
}

////////////////////////////////////////////////////////////////////////////////

static bool testChannels() {
   if (!g_powerIsUp) {
       // test is skipped
       return true;
   }

    bool result = true;

    for (int i = 0; i < CH_NUM; ++i) {
        result &= Channel::get(i).test();
    }

    return result;
}

static bool testMaster() {
    bool result = true;

    result &= rtc::test();
    result &= datetime::test();
    result &= eeprom::test();

#if OPTION_SD_CARD
    result &= sd_card::test();
#endif

#if OPTION_ETHERNET
    result &= ethernet::test();
#endif

    result &= temperature::test();

    // test BP3c
    result &= bp3c::relays::test();

    g_masterTestResult = result ? TEST_OK : TEST_FAILED;

    return result;
}

bool test() {
    bool testResult = true;

    fan::test_start();

    testResult &= testMaster();

    testResult &= testChannels();

    testResult &= fan::test();

    if (!testResult) {
        sound::playBeep();
    }

    return testResult;
}

////////////////////////////////////////////////////////////////////////////////

static bool psuReset() {
    // *ESE 0
    scpi_reg_set(SCPI_REG_ESE, 0);

    // *SRE 0
    scpi_reg_set(SCPI_REG_SRE, 0);

    // *STB 0
    scpi_reg_set(SCPI_REG_STB, 0);

    // *ESR 0
    scpi_reg_set(SCPI_REG_ESR, 0);

    // STAT:OPER[:EVEN] 0
    scpi_reg_set(SCPI_REG_OPER, 0);

    // STAT:OPER:COND 0
    reg_set(SCPI_PSU_REG_OPER_COND, 0);

    // STAT:OPER:ENAB 0
    scpi_reg_set(SCPI_REG_OPERE, 0);

    // STAT:OPER:INST[:EVEN] 0
    reg_set(SCPI_PSU_REG_OPER_INST_EVENT, 0);

    // STAT:OPER:INST:COND 0
    reg_set(SCPI_PSU_REG_OPER_INST_COND, 0);

    // STAT:OPER:INST:ENAB 0
    reg_set(SCPI_PSU_REG_OPER_INST_ENABLE, 0);

    // STAT:OPER:INST:ISUM[:EVEN] 0
    reg_set(SCPI_PSU_CH_REG_OPER_INST_ISUM_EVENT1, 0);
    reg_set(SCPI_PSU_CH_REG_OPER_INST_ISUM_EVENT2, 0);

    // STAT:OPER:INST:ISUM:COND 0
    reg_set(SCPI_PSU_CH_REG_OPER_INST_ISUM_COND1, 0);
    reg_set(SCPI_PSU_CH_REG_OPER_INST_ISUM_COND2, 0);

    // STAT:OPER:INST:ISUM:ENAB 0
    reg_set(SCPI_PSU_CH_REG_OPER_INST_ISUM_ENABLE1, 0);
    reg_set(SCPI_PSU_CH_REG_OPER_INST_ISUM_ENABLE2, 0);

    // STAT:QUES[:EVEN] 0
    scpi_reg_set(SCPI_REG_QUES, 0);

    // STAT:QUES:COND 0
    reg_set(SCPI_PSU_REG_QUES_COND, 0);

    // STAT:QUES:ENAB 0
    scpi_reg_set(SCPI_REG_QUESE, 0);

    // STAT:QUES:INST[:EVEN] 0
    reg_set(SCPI_PSU_REG_QUES_INST_EVENT, 0);

    // STAT:QUES:INST:COND 0
    reg_set(SCPI_PSU_REG_QUES_INST_COND, 0);

    // STAT:QUES:INST:ENAB 0
    reg_set(SCPI_PSU_REG_QUES_INST_ENABLE, 0);

    // STAT:QUES:INST:ISUM[:EVEN] 0
    reg_set(SCPI_PSU_CH_REG_QUES_INST_ISUM_EVENT1, 0);
    reg_set(SCPI_PSU_CH_REG_QUES_INST_ISUM_EVENT2, 0);

    // STAT:QUES:INST:ISUM:COND 0
    reg_set(SCPI_PSU_CH_REG_QUES_INST_ISUM_COND1, 0);
    reg_set(SCPI_PSU_CH_REG_QUES_INST_ISUM_COND2, 0);

    // STAT:OPER:INST:ISUM:ENAB 0
    reg_set(SCPI_PSU_CH_REG_QUES_INST_ISUM_ENABLE1, 0);
    reg_set(SCPI_PSU_CH_REG_QUES_INST_ISUM_ENABLE2, 0);

    eez::scpi::resetContext();

#if OPTION_ETHERNET
    ntp::reset();
#endif

    // TEMP:PROT[AUX]
    // TEMP:PROT:DEL
    // TEMP:PROT:STAT[AUX]
    for (int i = 0; i < temp_sensor::NUM_TEMP_SENSORS; ++i) {
        temperature::ProtectionConfiguration &temp_prot = temperature::sensors[i].prot_conf;
        temp_prot.sensor = i;
        if (temp_prot.sensor == temp_sensor::AUX) {
            temp_prot.delay = OTP_AUX_DEFAULT_DELAY;
            temp_prot.level = OTP_AUX_DEFAULT_LEVEL;
            temp_prot.state = OTP_AUX_DEFAULT_STATE;
        } else {
            temp_prot.delay = OTP_CH_DEFAULT_DELAY;
            temp_prot.level = OTP_CH_DEFAULT_LEVEL;
            temp_prot.state = OTP_CH_DEFAULT_STATE;
        }
    }

    // CAL[:MODE] OFF
    calibration::stop();

    // reset channels
    channel_dispatcher::setType(channel_dispatcher::TYPE_NONE);
    for (int i = 0; i < CH_NUM; ++i) {
        Channel::get(i).reset();
    }

    //
    trigger::reset();

    //
    list::reset();

    //
#if OPTION_SD_CARD
    dlog::reset();
#endif

    // SYST:POW ON
    if (powerUp()) {
    	for (int i = 0; i < CH_NUM; ++i) {
            Channel::get(i).update();
        }

    	return true;
    }

    return false;
}

////////////////////////////////////////////////////////////////////////////////

static bool loadAutoRecallProfile(profile::Parameters *profile, int *location) {
    if (persist_conf::isProfileAutoRecallEnabled()) {
        *location = persist_conf::getProfileAutoRecallLocation();
        if (profile::load(*location, profile)) {
            bool outputEnabled = false;

            for (int i = 0; i < CH_NUM; ++i) {
                if (profile->channels[i].flags.output_enabled) {
                    outputEnabled = true;
                    break;
                }
            }

            if (outputEnabled) {
                bool disableOutputs = false;

                if (persist_conf::isForceDisablingAllOutputsOnPowerUpEnabled() ||
                    !g_bootTestSuccess) {
                    disableOutputs = true;
                } else {
                    if (*location != 0) {
                        profile::Parameters defaultProfile;
                        if (profile::load(0, &defaultProfile)) {
                            if (profile->flags.channelsCoupling !=
                                defaultProfile.flags.channelsCoupling) {
                                disableOutputs = true;
                                event_queue::pushEvent(event_queue::EVENT_WARNING_AUTO_RECALL_VALUES_MISMATCH);
                            } else {
                                for (int i = 0; i < CH_NUM; ++i) {
                                    if (profile->channels[i].u_set != defaultProfile.channels[i].u_set ||
                                        profile->channels[i].i_set != defaultProfile.channels[i].i_set) {
                                        disableOutputs = true;
                                        event_queue::pushEvent( event_queue::EVENT_WARNING_AUTO_RECALL_VALUES_MISMATCH);
                                        break;
                                    }
                                }
                            }
                        } else {
                            disableOutputs = true;
                        }
                    }
                }

                if (disableOutputs) {
                    for (int i = 0; i < CH_NUM; ++i) {
                        profile->channels[i].flags.output_enabled = false;
                    }
                }
            }

            return true;
        }
    }

    return false;
}

static bool autoRecall() {
    profile::Parameters profile;
    int location;
    if (loadAutoRecallProfile(&profile, &location)) {
        if (profile::recallFromProfile(&profile, location)) {
            return true;
        }
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////

void boot() {
    init();

    // test
    g_bootTestSuccess = true;

    g_bootTestSuccess &= testMaster();

    if (!autoRecall()) {
        psuReset();
    }

    // play beep if there is an error during boot procedure
    if (!g_bootTestSuccess) {
        sound::playBeep();
    }

    g_isBooted = true;
}

////////////////////////////////////////////////////////////////////////////////

bool powerUp() {
    if (g_powerIsUp) {
        return true;
    }

    if (!temperature::isAllowedToPowerUp()) {
        return false;
    }

    sound::playPowerUp(sound::PLAY_POWER_UP_CONDITION_NONE);

    g_rlState = persist_conf::devConf.flags.isFrontPanelLocked ? RL_STATE_REMOTE : RL_STATE_LOCAL;

#if OPTION_DISPLAY
    eez::gui::showWelcomePage();
#endif

    fan::test_start();

    // reset channels
    for (int i = 0; i < CH_NUM; ++i) {
        Channel::get(i).reset();
    }

    // turn power on
    board::powerUp();
    g_powerIsUp = true;
    g_powerOnTimeCounter.start();

    // init channels
    for (int i = 0; i < CH_NUM; ++i) {
        Channel::get(i).init();
    }

    bool testSuccess = true;

    if (g_isBooted) {
        testSuccess &= testMaster();
    }

    // test channels
    for (int i = 0; i < CH_NUM; ++i) {
        testSuccess &= Channel::get(i).test();
    }

    testSuccess &= fan::test();

    // turn on Power On (PON) bit of ESE register
    reg_set_esr_bits(ESR_PON);

    event_queue::pushEvent(event_queue::EVENT_INFO_POWER_UP);

    // play power up tune on success
    if (testSuccess) {
        sound::playPowerUp(sound::PLAY_POWER_UP_CONDITION_TEST_SUCCESSFUL);
    }

    g_bootTestSuccess &= testSuccess;

    return true;
}

void powerDown() {
#if OPTION_DISPLAY
    if (g_isBooted) {
        eez::gui::showEnteringStandbyPage();
    } else {
        eez::gui::showStandbyPage();
    }
#endif

    if (!g_powerIsUp)
        return;

    trigger::abort();
#if OPTION_SD_CARD
    dlog::abort();
#endif

    channel_dispatcher::setType(channel_dispatcher::TYPE_NONE);

    for (int i = 0; i < CH_NUM; ++i) {
        Channel::get(i).onPowerDown();
    }

    board::powerDown();

    g_powerIsUp = false;
    g_powerOnTimeCounter.stop();

    event_queue::pushEvent(event_queue::EVENT_INFO_POWER_DOWN);

    fan::g_testResult = TEST_OK;

    io_pins::tick(micros());

    sound::playPowerDown();
}

bool isPowerUp() {
    return g_powerIsUp;
}

void changePowerState(bool up) {
    if (up == g_powerIsUp)
        return;

    // at least MIN_POWER_UP_DELAY seconds shall pass after last power down
    if (g_testPowerUpDelay) {
        if (millis() - g_powerDownTime < MIN_POWER_UP_DELAY * 1000)
            return;
        g_testPowerUpDelay = false;
    }

    if (osThreadGetId() != g_psuTaskHandle) {
        osMessagePut(g_psuMessageQueueId, PSU_QUEUE_MESSAGE(PSU_QUEUE_MESSAGE_TYPE_CHANGE_POWER_STATE, up ? 1 : 0), osWaitForever);
        return;
    }

    if (up) {
        g_bootTestSuccess = true;

        // auto recall channels parameters from profile
        profile::Parameters profile;
        int location;
        auto recall = loadAutoRecallProfile(&profile, &location);

        if (!powerUp()) {
            return;
        }

        // auto recall channels parameters from profile
        if (recall) {
            for (int i = 0; i < temp_sensor::NUM_TEMP_SENSORS; ++i) {
                memcpy(&temperature::sensors[i].prot_conf, profile.temp_prot + i,
                       sizeof(temperature::ProtectionConfiguration));
            }
            profile::recallChannelsFromProfile(&profile, location);
        }

        profile::save();
    } else {
#if OPTION_DISPLAY
        eez::gui::showEnteringStandbyPage();
#endif
        g_powerIsUp = false;
        profile::save(true);
        g_powerIsUp = true;

        profile::enableSave(false);
        powerDown();
        profile::enableSave(true);

        g_testPowerUpDelay = true;
        g_powerDownTime = millis();
    }
}

void powerDownBySensor() {
    if (g_powerIsUp) {
#if OPTION_DISPLAY
        eez::gui::showEnteringStandbyPage();
#endif

        for (int i = 0; i < CH_NUM; ++i) {
            Channel::get(i).outputEnable(false);
        }

        g_powerIsUp = false;
        profile::save(true);
        g_powerIsUp = true;

        profile::enableSave(false);
        powerDown();
        profile::enableSave(true);
    }
}

////////////////////////////////////////////////////////////////////////////////

bool reset() {
    if (osThreadGetId() != g_psuTaskHandle) {
        osMessagePut(g_psuMessageQueueId, PSU_QUEUE_MESSAGE(PSU_QUEUE_MESSAGE_TYPE_RESET, 0), osWaitForever);
        return true;
    }

    if (psuReset()) {
        profile::save();
        return true;
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////

static void disableChannels() {
    for (int i = 0; i < CH_NUM; ++i) {
        if (Channel::get(i).isOutputEnabled()) {
            Channel::get(i).outputEnable(false);
        }
    }
}

void onProtectionTripped() {
    if (isPowerUp()) {
        if (persist_conf::isShutdownWhenProtectionTrippedEnabled()) {
            powerDownBySensor();
        } else {
            if (persist_conf::isOutputProtectionCoupleEnabled()) {
                disableChannels();
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void tick() {
    uint32_t tick_usec = micros();

#if OPTION_SD_CARD
    dlog::tick(tick_usec);
#endif

    for (int i = 0; i < CH_NUM; ++i) {
        Channel::get(i).tick(tick_usec);
    }

    io_pins::tick(tick_usec);

#ifdef DEBUG
    debug::tick(tick_usec);
#endif

    g_powerOnTimeCounter.tick(tick_usec);

    temperature::tick(tick_usec);

    fan::tick(tick_usec);

    trigger::tick(tick_usec);

    list::tick(tick_usec);

    event_queue::tick(tick_usec);

    datetime::tick(tick_usec);

	sound::tick();

    idle::tick();

    if (g_diagCallback) {
        g_diagCallback();
        g_diagCallback = NULL;
    }

#if OPTION_WATCHDOG
    watchdog::tick(tick_usec);
#endif
}

////////////////////////////////////////////////////////////////////////////////

void setQuesBits(int bit_mask, bool on) {
    reg_set_ques_bit(bit_mask, on);
}

void setOperBits(int bit_mask, bool on) {
    reg_set_oper_bit(bit_mask, on);
}

void generateError(int16_t error) {
    eez::scpi::generateError(error);
}

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

const char *getCpuModel() {
#if defined(EEZ_PLATFORM_SIMULATOR)
    return "Simulator, " FIRMWARE;
#elif defined(EEZ_PLATFORM_STM32)
    return "STM32, " FIRMWARE;
#endif
}

const char *getCpuType() {
#if defined(EEZ_PLATFORM_SIMULATOR)
    return "Simulator";
#elif defined(EEZ_PLATFORM_STM32)
    return "STM32";
#endif
}

const char *getCpuEthernetType() {
#if defined(EEZ_PLATFORM_SIMULATOR)
    return "Simulator";
#elif defined(EEZ_PLATFORM_STM32)
    return "None";
#endif
}

bool isMaxCurrentLimited() {
    return g_maxCurrentLimitCause != MAX_CURRENT_LIMIT_CAUSE_NONE;
}

void limitMaxCurrent(MaxCurrentLimitCause cause) {
    if (g_maxCurrentLimitCause != cause) {
        g_maxCurrentLimitCause = cause;

        if (isMaxCurrentLimited()) {
            for (int i = 0; i < CH_NUM; ++i) {
                if (Channel::get(i).isOutputEnabled() &&
                    Channel::get(i).i.mon_last > ERR_MAX_CURRENT) {
                    Channel::get(i).setCurrent(Channel::get(i).i.min);
                }

                if (ERR_MAX_CURRENT < Channel::get(i).getCurrentLimit()) {
                    Channel::get(i).setCurrentLimit(ERR_MAX_CURRENT);
                }
            }
        }
    }
}

void unlimitMaxCurrent() {
    limitMaxCurrent(MAX_CURRENT_LIMIT_CAUSE_NONE);
}

MaxCurrentLimitCause getMaxCurrentLimitCause() {
    return g_maxCurrentLimitCause;
}

} // namespace psu
} // namespace eez
