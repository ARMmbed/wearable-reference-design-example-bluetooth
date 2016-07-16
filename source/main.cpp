/* mbed Microcontroller Library
 * Copyright (c) 2006-2015 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "mbed-drivers/mbed.h"
#include "ble/BLE.h"

#include "watchdog/Watchdog.h"

#include "ancs/ANCSManager.h"

#include "message-center/MessageCenter.h"
#include "message-center-transport/MessageCenterSPISlave.h"

#include "cborg/Cbor.h"

/*****************************************************************************/
/* Configuration                                                             */
/*****************************************************************************/

// set default device name
#define DEVICE_NAME "mbed Watch";

// set TX power
#ifndef CFG_BLE_TX_POWER_LEVEL
#define CFG_BLE_TX_POWER_LEVEL 0
#endif

// control debug output
#if !defined(TARGET_LIKE_WATCH)
#define DEBUGOUT(...) { printf(__VA_ARGS__); }
#else
#define DEBUGOUT(...) /* nothing */
#endif // DEBUGOUT

#define VERBOSE_DEBUG_OUT 0

const uint8_t txPowerLevel = CFG_BLE_TX_POWER_LEVEL;

static bool ancsIsEnabled = true;
static bool radioIsEnabled = true;

std::string deviceNameString;

static spi_slave_config_t spi_slave_config = {
    .pin_miso         = SPIS_MISO,
    .pin_mosi         = SPIS_MOSI,
    .pin_sck          = SPIS_SCK,
};

static MessageCenterSPISlave transport(spi_slave_config, SPIS_CSN, SPIS_IRQ);

/*****************************************************************************/

static void slowBeaconing()
{
    if (radioIsEnabled)
    {
        DEBUGOUT("main: Restarting the advertising process\r\n");

        BLE::Instance().gap().stopAdvertising();
        BLE::Instance().gap().setAdvertisingInterval(1285);
        BLE::Instance().gap().startAdvertising();
    }
}

static void fastBeaconing()
{
    if (radioIsEnabled)
    {
        DEBUGOUT("main: Restarting the advertising process\r\n");

        BLE::Instance().gap().stopAdvertising();
        BLE::Instance().gap().setAdvertisingInterval(319);
        BLE::Instance().gap().startAdvertising();

        minar::Scheduler::postCallback(slowBeaconing)
            .delay(minar::milliseconds(30 * 1000));
    }
}

/*****************************************************************************/
/* Variables used by the app                                                 */
/*****************************************************************************/

static BLE ble;

static Gap::Handle_t connectionHandle;
static Gap::Handle_t peripheralHandle;

#if 0
static const Gap::ConnectionParams_t custom = {
        .minConnectionInterval = 80,          // 100 ms
        .maxConnectionInterval = 100,         // 125 ms
        .slaveLatency = 4,                    // 4 events
        .connectionSupervisionTimeout = 200,  // 2000 ms
    };
#elif 0
static const Gap::ConnectionParams_t custom = {
        .minConnectionInterval = 304,         // 380 ms
        .maxConnectionInterval = 320,         // 405 ms
        .slaveLatency = 4,                    // 4 events
        .connectionSupervisionTimeout = 600,  // 6000 ms
    };
#else
static const Gap::ConnectionParams_t custom = {
        .minConnectionInterval = 400,         // 500 ms
        .maxConnectionInterval = 480,         // 600 ms
        .slaveLatency = 2,                    // 2 events
        .connectionSupervisionTimeout = 540,  // 5400 ms
    };
#endif

/*****************************************************************************/
/* Message Center                                                            */
/*****************************************************************************/

uint8_t payload[10];
BlockStatic block(payload, 10);

void receivedControl(BlockStatic block)
{
    DEBUGOUT("main: Control: %p\r\n", block.getData());
    for (std::size_t idx = 0; idx < block.getLength(); idx++)
    {
        DEBUGOUT("%02X", block.at(idx));
    }
    DEBUGOUT("\r\n");

    uint32_t type = 0;
    uint32_t value = 0;

    Cborg cbor(block.getData(), block.getLength());
    cbor.at(0).getUnsigned(&type);
    cbor.at(1).getUnsigned(&value);

    if ((type == 1) && (value == 1234567890))
    {
        NVIC_SystemReset();
    }
}

void receivedRadio(BlockStatic block)
{
    DEBUGOUT("main: Radio: %p\r\n", block.getData());
    for (std::size_t idx = 0; idx < block.getLength(); idx++)
    {
        DEBUGOUT("%02X", block.at(idx));
    }
    DEBUGOUT("\r\n");

    uint32_t type = 0;
    uint32_t value = 0;

    Cborg cbor(block.getData(), block.getLength());
    cbor.at(0).getUnsigned(&type);
    cbor.at(1).getUnsigned(&value);

    if ((type == 1) && (value == 1))
    {
        DEBUGOUT("main: Radio: enable\r\n");
        radioIsEnabled = true;
        fastBeaconing();
    }
    else if ((type == 1) && (value == 0))
    {
        DEBUGOUT("main: Radio: disable\r\n");
        radioIsEnabled = false;
        ble.gap().stopAdvertising();
    }
}

void sendDone()
{
    DEBUGOUT("sendDone\r\n");
}

/*****************************************************************************/
/* BLE                                                                       */
/*****************************************************************************/

void updateConnectionParameters()
{
    DEBUGOUT("main: update connection parameters\r\n");

    // update connection to the prefered parameters
    ble.gap().updateConnectionParams(connectionHandle, &custom);
}

/*
    Functions called when BLE device connects and disconnects.
*/
void whenConnected(const Gap::ConnectionCallbackParams_t* params)
{
    DEBUGOUT("main: Connected: %d %d %d\r\n", params->connectionParams->minConnectionInterval,
                                              params->connectionParams->maxConnectionInterval,
                                              params->connectionParams->slaveLatency);

    Cbore cbor(block.getData(), block.getLength());

    // connected as peripheral to a central
    if (params->role == Gap::PERIPHERAL)
    {
        // store connection handle
        connectionHandle = params->handle;

        // construct "on connection peripheral" cbor object
        cbor.array(2)
            .item(1)
            .item(1);
    }

    block.setLength(cbor.getLength());
    MessageCenter::sendTask(MessageCenter::RemoteHost,
                            MessageCenter::ControlPort,
                            block,
                            sendDone);
}

void whenDisconnected(const Gap::DisconnectionCallbackParams_t* params)
{
    DEBUGOUT("main: Disconnected!\r\n");

    Cbore cbor(block.getData(), block.getLength());

    // disconnected from central
    if (params->handle == connectionHandle)
    {
        // clear handle
        connectionHandle = 0;

        // begin advertising again
        slowBeaconing();

        // construct "disconnected as peripheral" cbor
        cbor.array(2)
            .item(1)
            .item(2);
    }

    block.setLength(cbor.getLength());
    MessageCenter::sendTask(MessageCenter::RemoteHost,
                            MessageCenter::ControlPort,
                            block,
                            sendDone);
}



/*****************************************************************************/
/* main                                                                      */
/*****************************************************************************/

void updateAdvertisement()
{
    ble.gap().stopAdvertising();

    // clear advertisement beacon
    ble.gap().clearAdvertisingPayload();

    // construct advertising beacon
    ble.gap().setAdvertisingType(GapAdvertisingParams::ADV_CONNECTABLE_UNDIRECTED);

    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::BREDR_NOT_SUPPORTED|GapAdvertisingData::LE_GENERAL_DISCOVERABLE);

    if (ancsIsEnabled)
    {
        ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::LIST_128BIT_SOLICITATION_IDS, ANCS::UUID.getBaseUUID(), ANCS::UUID.getLen());
    }

    /*************************************************************************/

    // clear scan response
    ble.gap().clearScanResponse();

    // set Tx power level
    ble.gap().accumulateScanResponse(GapAdvertisingData::TX_POWER_LEVEL, &txPowerLevel, 1);

    // set name
    ble.gap().accumulateScanResponse(GapAdvertisingData::COMPLETE_LOCAL_NAME, (uint8_t*) deviceNameString.c_str(), deviceNameString.length());

    // ble setup complete - start advertising
    slowBeaconing();
}

static void bleInitDone(BLE::InitializationCompleteCallbackContext* context)
{
    DEBUGOUT("init done\r\n");

    MessageCenter::addTransportTask(MessageCenter::RemoteHost, &transport);

    MessageCenter::addListenerTask(MessageCenter::LocalHost,
                                   MessageCenter::ControlPort,
                                   receivedControl);

    MessageCenter::addListenerTask(MessageCenter::LocalHost,
                                   MessageCenter::RadioPort,
                                   receivedRadio);

    /*************************************************************************/

    ANCSManager::init();

    /*************************************************************************/

    // status callback functions
    ble.gap().onConnection(whenConnected);
    ble.gap().onDisconnection(whenDisconnected);

    // set TX power
    ble.gap().setTxPower(CFG_BLE_TX_POWER_LEVEL);

    // set device name
    deviceNameString = DEVICE_NAME;
    BLE::Instance().gap().setDeviceName((const uint8_t*) deviceNameString.c_str());

    updateAdvertisement();

    DEBUGOUT("Watch BLE Test: %s %s\r\n", __DATE__, __TIME__);
}

void feedWatchDog()
{
    DEBUGOUT("feed\r\n");

    watchdog::feed();
}

void app_start(int, char *[])
{
    watchdog::enable(20000);

    minar::Scheduler::postCallback(feedWatchDog)
        .period(minar::milliseconds(10000));

    /*************************************************************************/
    /*************************************************************************/

    /* bluetooth le */
    ble.init(bleInitDone);
}
