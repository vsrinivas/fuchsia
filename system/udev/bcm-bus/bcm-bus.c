#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/bcm.h>

#include "../bcm-common/bcm28xx.h"



void devhost_launch_devhost(mx_device_t* parent, const char* name, uint32_t protocol_id,
                            const char* procname, int argc, char** argv);

static mx_status_t bcm_root_init(mx_driver_t* driver) {

    char name[32];
    snprintf(name, sizeof(name), "soc");

    char procname[64];
    snprintf(procname, sizeof(procname), "devhost:soc:bcm");

    char arg1[20];
    snprintf(arg1, sizeof(arg1), "soc");

    char arg2[20];
    snprintf(arg2, sizeof(arg2), "%d", SOC_VID_BROADCOMM);

    char arg3[20];
    snprintf(arg3, sizeof(arg3), "%d", SOC_DID_BROADCOMM_VIDEOCORE_BUS);

    const char* args[4] = { "/boot/bin/devhost", arg1 , arg2, arg3};
    devhost_launch_devhost(driver_get_root_device(), name, MX_PROTOCOL_SOC, procname, 4, (char**)args);

    return NO_ERROR;
}


#ifdef RASPBERRY_PI

mx_driver_t _driver_bcmroot = {
    .ops = {
        .init = bcm_root_init,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_bcmroot, "soc", "magenta", "0.1", 0)
MAGENTA_DRIVER_END(_driver_bcmroot)

#endif