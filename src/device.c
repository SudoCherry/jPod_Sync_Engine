#include <string.h>
#include "device.h"

int device_init(ipod_device_t *device) {
    if (device == NULL) return DEVICE_ERR_NULL;

    memset(device, 0, sizeof(*device));   // zero every field, incl. serial
    device->status = IPOD_DISCONNECTED;
    return DEVICE_OK;
}

int device_connect(ipod_device_t *device) {
    if (device == NULL) return DEVICE_ERR_NULL;
    if (device->status != IPOD_DISCONNECTED) return DEVICE_ERR_STATE;

    device->status = IPOD_CONNECTED;
    return DEVICE_OK;
}

int device_disconnect(ipod_device_t *device) {
    if (device == NULL) return DEVICE_ERR_NULL;

    device->status = IPOD_DISCONNECTED;
    return DEVICE_OK;
}

int device_identify(ipod_device_t *device, uint32_t model_id, uint32_t year,
                    uint32_t generation, uint32_t storage_cap,
                    ipod_connection_t connection, const char *serial) {
    if (device == NULL || serial == NULL) return DEVICE_ERR_NULL;
    if (device->status != IPOD_CONNECTED) return DEVICE_ERR_STATE;

    device->model_id    = model_id;
    device->year        = year;
    device->generation  = generation;
    device->storage_cap = storage_cap;
    device->connection  = connection;

    strncpy(device->serial_number, serial, sizeof(device->serial_number) - 1);
    device->serial_number[sizeof(device->serial_number) - 1] = '\0';

    device->status = IPOD_READY;
    return DEVICE_OK;
}

int device_is_ready(const ipod_device_t *device) {
    return device != NULL && device->status == IPOD_READY;
}
