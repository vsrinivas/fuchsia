#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_DEVICE_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_DEVICE_DEVICE_H_

/* Add an abstracted device interface that can be used for wlan driver tests without involving
 * devmgr.
 */

#include <ddk/device.h>
#include <ddk/driver.h>

// Simulated device_add()
zx_status_t wlan_sim_device_add(zx_device_t* parent, device_add_args_t* args, zx_device_t** out);

#endif // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_DEVICE_DEVICE_H_
