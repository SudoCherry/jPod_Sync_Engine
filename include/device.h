#ifndef DEVICE_H
#define DEVICE_H

#include "ipod.h"

// Return codes: 0 = success, negative = error
#define DEVICE_OK            0
#define DEVICE_ERR_NULL     -1   // NULL pointer passed in
#define DEVICE_ERR_STATE    -2   // action not allowed in the current status

// Initialise a device structure to a clean, disconnected state
int device_init(ipod_device_t *device);

// Mark a device as connected (only valid from DISCONNECTED)
int device_connect(ipod_device_t *device);

// Mark a device as disconnected (valid from any state)
int device_disconnect(ipod_device_t *device);

// Fill in model info and mark the device READY (only valid from CONNECTED)
int device_identify(ipod_device_t *device, uint32_t model_id, uint32_t year,
                    uint32_t generation, uint32_t storage_cap,
                    ipod_connection_t connection, const char *serial);

// Returns 1 if the device is ready to sync, otherwise 0
int device_is_ready(const ipod_device_t *device);

#endif
