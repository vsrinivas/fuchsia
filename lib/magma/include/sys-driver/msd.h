// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _MSD_H_
#define _MSD_H_

#include "msd_defs.h"
#include "msd_platform_buffer.h"

#if defined(__cplusplus)
extern "C" {
#endif

// The magma system driver... driver :)
struct msd_driver {
    int32_t magic_;
};

// The magma system driver device.
struct msd_device {
    int32_t magic_;
};

// Instantiates a driver instance.
struct msd_driver* msd_driver_create(void);

// Destroys a driver instance.
void msd_driver_destroy(struct msd_driver* drv);

// Creates a device at system startup.
struct msd_device* msd_driver_create_device(struct msd_driver* drv, void* device);

// Destroys a device at system shutdown.
void msd_driver_destroy_device(struct msd_device* dev);

// Opens a device for the given client. Returns 0 on success
int32_t msd_device_open(struct msd_device* dev, msd_client_id client_id);

// Closes a device on behalf of the given client. Returns 0 on success
int32_t msd_device_close(struct msd_device* dev, msd_client_id client_id);

// Returns the device id.  0 is an invalid device id.
uint32_t msd_device_get_id(struct msd_device* dev);

#if defined(__cplusplus)
}
#endif

#endif // _MSD_H_
