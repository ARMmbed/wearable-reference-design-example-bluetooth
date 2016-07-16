


#ifndef __BLE_ADVERTISING_PARSING_H__
#define __BLE_ADVERTISING_PARSING_H__

#if 0
class AdvertisementPayloadIterator;

class AdvertisementPayload
{
    friend class AdvertisementPayloadIterator;

public:
    typedef struct {
        const uint8_t* data;
        uint8_t length;
    } DataField_t;

    typedef AdvertisementPayloadIterator iterator;
    typedef ptrdiff_t difference_type;
    typedef size_t size_type;
    typedef DataField_t  value_type;
    typedef DataField_t* pointer;
    typedef DataField_t& reference;

    AdvertisementPayload(const uint8_t* _payload, uint8_t _length)
        :   payload(_payload),
            length(_length)
    {}

    iterator begin()
    {
        return iterator(*this, 0);
    }

    iterator end()
    {
        return iterator(*this, length);
    }

private:
    const uint8_t* payload;
    uint8_t length;
};

class AdvertisementPayloadIterator
{
public:
    AdvertisementPayloadIterator(AdvertisementPayload& _payload, uint8_t _position)
        :   payload(_payload),
            position(_position)
    {}

private:
    AdvertisementPayload& payload;
    uint8_t position;
};

    AdvertisementPayload payload(params->advertisingData, params->advertisingDataLen);

    AdvertisementPayload::iterator iter = payload.begin();

    for (; iter != payload.end(); iter++)
    {
        iter->length;
        iter->type;
        iter->value;
    }
#endif

bool advertisementContainsUUID(const Gap::AdvertisementCallbackParams_t* params, const UUID& uuid)
{
    // scan through advertisement data
    for (uint8_t idx = 0; idx < params->advertisingDataLen; )
    {
        uint8_t fieldLength = params->advertisingData[idx];
        uint8_t fieldType = params->advertisingData[idx + 1];
        const uint8_t* fieldValue = &(params->advertisingData[idx + 2]);

        // find 16-bit service IDs
        if ((fieldType == GapAdvertisingData::COMPLETE_LIST_16BIT_SERVICE_IDS) ||
            (fieldType == GapAdvertisingData::INCOMPLETE_LIST_16BIT_SERVICE_IDS))
        {
            uint8_t units = (fieldLength - 1) / 2;

            for (uint8_t idx = 0; idx < units; idx++)
            {
                // compare short UUID
                UUID beaconUUID((fieldValue[idx * 2 + 1] << 8) | fieldValue[idx * 2]);

                if (beaconUUID == uuid)
                {
                    return true;
                }
            }
        }
        // find 128-bit service IDs
        else if ((fieldType == GapAdvertisingData::COMPLETE_LIST_128BIT_SERVICE_IDS) ||
                 (fieldType == GapAdvertisingData::INCOMPLETE_LIST_128BIT_SERVICE_IDS))
        {
            uint8_t units = (fieldLength - 1) / 16;

            for (uint8_t idx = 0; idx < units; idx++)
            {
                // compare long UUID
                UUID beaconUUID(&fieldValue[idx * 16]);

                if (beaconUUID == uuid)
                {
                    return true;
                }
            }
        }

        // move to next field
        idx += fieldLength + 1;
    }

    return false;
}


bool advertisementGetName(const Gap::AdvertisementCallbackParams_t* params, const char** name, uint8_t* length)
{
    // scan through advertisement data
    for (uint8_t idx = 0; idx < params->advertisingDataLen; )
    {
        uint8_t fieldLength = params->advertisingData[idx];
        uint8_t fieldType = params->advertisingData[idx + 1];
        const uint8_t* fieldValue = &(params->advertisingData[idx + 2]);

        // find name
        if ((fieldType == GapAdvertisingData::SHORTENED_LOCAL_NAME) ||
            (fieldType == GapAdvertisingData::COMPLETE_LOCAL_NAME))
        {
            *name = (const char*) fieldValue;
            *length = fieldLength - 1;

            return true;
        }

        // move to next field
        idx += fieldLength + 1;
    }

    return false;
}


#endif // __BLE_ADVERTISING_PARSING_H__