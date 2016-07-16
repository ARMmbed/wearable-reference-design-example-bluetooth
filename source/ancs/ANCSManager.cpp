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
#include "BLE/ble.h"

#include "message-center/MessageCenter.h"
#include "cborg/Cbore.h"

#include "core-util/SharedPointer.h"

#include "ANCSManager.h"

#include <string>
#include <queue>

using namespace mbed::util;

// control debug output
#if 0
#include <stdio.h>
#define DEBUGOUT(...) { printf(__VA_ARGS__); }
#else
#define DEBUGOUT(...) /* nothing */
#endif // DEBUGOUT

#define ALERT_LEVEL 1
#define MAX_RETRIEVE_LENGTH 110

static ANCSClient ancs;

static SharedPointer<BlockStatic> sendBlock;
static SharedPointer<BlockStatic> titleBlock;
static SharedPointer<BlockStatic> subtitleBlock;

static ANCSClient::notification_attribute_id_t attributeIndex;
static uint32_t notificationID = 0;

static std::queue<uint32_t> notificationQueue;

static void onServiceFound(void);
static void onNotificationTask(ANCSClient::Notification_t event);
static void onNotificationAttributeTask(SharedPointer<BlockStatic> dataPayload);
static void sendTaskDone(void);
static void processQueue(void);

// extern function
void updateConnectionParameters(void);

/*****************************************************************************/
/* ANCS                                                                      */
/*****************************************************************************/

void ANCSManager::init()
{
    ancs.init();

    ancs.registerServiceFoundHandlerTask(onServiceFound);
    ancs.registerNotificationHandlerTask(onNotificationTask);
    ancs.registerDataHandlerTask(onNotificationAttributeTask);
}

static void onServiceFound()
{
    DEBUGOUT("ancs: ancs service found\r\n");

    updateConnectionParameters();
}

static void onNotificationTask(ANCSClient::Notification_t event)
{
    // only process newly added notifications that are not silent
    if ((event.eventID == ANCSClient::EventIDNotificationAdded) &&
        !(event.eventFlags & ANCSClient::EventFlagSilent))
    {
        DEBUGOUT("ancs: %u %u %u %u %lu\r\n", event.eventID, event.eventFlags, event.categoryID, event.categoryCount, event.notificationUID);

        notificationQueue.push(event.notificationUID);

        if (notificationQueue.size() == 1)
        {
            minar::Scheduler::postCallback(processQueue);
        }
    }
}

static void processQueue()
{
    DEBUGOUT("process queue: %d\r\n", notificationQueue.size());

    notificationID = notificationQueue.front();

    attributeIndex = ANCSClient::NotificationAttributeIDTitle;
    ancs.getNotificationAttribute(notificationID, attributeIndex, MAX_RETRIEVE_LENGTH);
}

static void onNotificationAttributeTask(SharedPointer<BlockStatic> dataPayload)
{
    DEBUGOUT("data: ");
    for (uint8_t idx = 0; idx < dataPayload->getLength(); idx++)
    {
        DEBUGOUT("%c", dataPayload->at(idx));
    }
    DEBUGOUT("\r\n");

    if (attributeIndex == ANCSClient::NotificationAttributeIDTitle)
    {
        // store title payload
        titleBlock = dataPayload;

        // get subtitle
        attributeIndex = ANCSClient::NotificationAttributeIDSubtitle;
        ancs.getNotificationAttribute(notificationID, attributeIndex, MAX_RETRIEVE_LENGTH);
    }
    else if (attributeIndex == ANCSClient::NotificationAttributeIDSubtitle)
    {
        // store title payload
        subtitleBlock = dataPayload;

        // get message
        attributeIndex = ANCSClient::NotificationAttributeIDMessage;
        ancs.getNotificationAttribute(notificationID, attributeIndex, MAX_RETRIEVE_LENGTH);
    }
    else if (attributeIndex == ANCSClient::NotificationAttributeIDMessage)
    {
        // get length for the title and subtitle
        uint32_t titleLength = titleBlock->getLength();
        uint32_t subtitleLength = subtitleBlock->getLength();

        // allocate buffer for combined title
        uint32_t messageLength = 1 + titleLength + subtitleLength;
        char messageBuffer[messageLength];

        // copy title and subtitle into message buffer
        memcpy(&messageBuffer[0], titleBlock->getData(), titleLength);
        messageBuffer[titleLength] = ' ';
        memcpy(&messageBuffer[titleLength + 1], subtitleBlock->getData(), subtitleLength);

        // allocate buffer for message center
        uint8_t cborLength = 1 + 1                          // array and alert level
                           + 2 + messageLength              // title
                           + 2 + dataPayload->getLength();  // message

        sendBlock = SharedPointer<BlockStatic>(new BlockDynamic(cborLength));

        // construct cbor
        Cbore cbor(sendBlock->getData(), sendBlock->getLength());

        cbor.array(3)
            .item(ALERT_LEVEL)
            .item((const char*) messageBuffer, messageLength)
            .item((const char*) dataPayload->getData(), dataPayload->getLength());

        // set length
        sendBlock->setLength(cbor.getLength());

        // send message
        MessageCenter::sendTask(MessageCenter::RemoteHost,
                                MessageCenter::AlertPort,
                                *(sendBlock.get()),
                                sendTaskDone);

        // remove ID from queue
        notificationQueue.pop();

        // process next ID if available
        if (notificationQueue.size() > 0)
        {
            minar::Scheduler::postCallback(processQueue);
        }
    }
}

static void sendTaskDone()
{
    // flag buffers as done
    titleBlock = SharedPointer<BlockStatic>();
    sendBlock = SharedPointer<BlockStatic>();
}


