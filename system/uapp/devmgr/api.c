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

#include <runtime/mutex.h>

#include "devmgr.h"

// These are the API entry-points from drivers
// They must take the devmgr_api_lock before calling devmgr_* internals
//
// Driver code MUST NOT directly call devmgr_* APIs

void driver_add(mx_driver_t* drv) {
    DM_LOCK();
    devmgr_driver_add(drv);
    DM_UNLOCK();
}

void driver_remove(mx_driver_t* drv) {
    DM_LOCK();
    devmgr_driver_remove(drv);
    DM_UNLOCK();
}

mx_status_t device_create(mx_device_t** dev, mx_driver_t* drv,
                          const char* name, mx_protocol_device_t* ops) {
    mx_status_t r;
    DM_LOCK();
    r = devmgr_device_create(dev, drv, name, ops);
    DM_UNLOCK();
    return r;
}

mx_status_t device_init(mx_device_t* dev, mx_driver_t* drv,
                        const char* name, mx_protocol_device_t* ops) {
    mx_status_t r;
    DM_LOCK();
    r = devmgr_device_init(dev, drv, name, ops);
    DM_UNLOCK();
    return r;
}

mx_status_t device_add(mx_device_t* dev, mx_device_t* parent) {
    mx_status_t r;
    DM_LOCK();
    r = devmgr_device_add(dev, parent);
    DM_UNLOCK();
    return r;
}

mx_status_t device_add_instance(mx_device_t* dev, mx_device_t* parent) {
    mx_status_t r;
    DM_LOCK();
    if (dev) {
        dev->flags |= DEV_FLAG_INSTANCE | DEV_FLAG_UNBINDABLE;
    }
    r = devmgr_device_add(dev, parent);
    DM_UNLOCK();
    return r;
}

mx_status_t device_remove(mx_device_t* dev) {
    mx_status_t r;
    DM_LOCK();
    r = devmgr_device_remove(dev);
    DM_UNLOCK();
    return r;
}

void device_set_bindable(mx_device_t* dev, bool bindable) {
    DM_LOCK();
    devmgr_device_set_bindable(dev, bindable);
    DM_UNLOCK();
}

mx_status_t device_open(mx_device_t* dev, mx_device_t** out, uint32_t flags) {
    mx_status_t r;
    DM_LOCK();
    r = devmgr_device_open(dev, out, flags);
    DM_UNLOCK();
    return r;
}

mx_status_t device_close(mx_device_t* dev) {
    mx_status_t r;
    DM_LOCK();
    r = devmgr_device_close(dev);
    DM_UNLOCK();
    return r;
}
