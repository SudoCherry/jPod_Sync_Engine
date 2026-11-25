#include <stdio.h>
#include <assert.h>
#include "device.h"

int main(void) {
    ipod_device_t d;
    assert(device_init(&d) == DEVICE_OK);
    assert(!device_is_ready(&d));
    assert(device_identify(&d, 1, 2004, 4, 20, IPOD_USB, "ABC") == DEVICE_ERR_STATE);
    assert(device_connect(&d) == DEVICE_OK);
    assert(device_connect(&d) == DEVICE_ERR_STATE);
    assert(device_identify(&d, 1, 2004, 4, 20, IPOD_USB, "ABC123") == DEVICE_OK);
    assert(device_is_ready(&d));
    assert(device_disconnect(&d) == DEVICE_OK);
    assert(!device_is_ready(&d));
    assert(device_init(NULL) == DEVICE_ERR_NULL);
    puts("device tests passed");
    return 0;
}
