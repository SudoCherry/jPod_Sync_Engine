
#ifndef IPOD_H
#define IPOD_H

#include <stdint.h>

// Connection types
typedef enum {
    IPOD_USB,
    IPOD_FIREWIRE
} ipod_connection_t;

// Device status
typedef enum {
    IPOD_DISCONNECTED,
    IPOD_CONNECTED,
    IPOD_READY,
    IPOD_ERROR
} ipod_status_t;

// Device information
typedef struct {

    uint32_t model_id;
    uint32_t year;
    uint32_t generation;
    uint32_t storage_cap;

    ipod_connection_t connection;
    ipod_status_t status;

    char serial_number[32];

} ipod_device_t;

#endif
