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

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>

#include <hw/pci.h>
#include <magenta/syscalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kpci_priv.h"

// kpci is a driver that communicates with the kernel to publish a list of pci devices.

static mx_device_t* kpci_root_dev;

extern pci_protocol_t _pci_protocol;

static mx_status_t kpci_open(mx_device_t* dev, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t kpci_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t kpci_release(mx_device_t* dev) {
    kpci_device_t* device = get_kpci_device(dev);
    _magenta_handle_close(device->handle);
    free(device);
    return NO_ERROR;
}

static mx_protocol_device_t kpci_device_proto = {
    .get_protocol = device_base_get_protocol,
    .open = kpci_open,
    .close = kpci_close,
    .release = kpci_release,
};

static mx_status_t kpci_init_child(mx_driver_t* drv, mx_device_t** out, uint32_t index) {
    mx_pcie_get_nth_info_t info;

    mx_handle_t handle = _magenta_pci_get_nth_device(index, &info);
    if (handle < 0) {
        return handle;
    }

    kpci_device_t* device = calloc(1, sizeof(kpci_device_t));
    mx_status_t status = ERR_NO_MEMORY;
    if (!device)
        goto finished;

    char name[20];
    snprintf(name, sizeof(name), "%02x:%02x:%02x", info.bus_id, info.dev_id, info.func_id);
    status = device_init(&device->device, drv, name, &kpci_device_proto);
    if (status != NO_ERROR)
        goto finished;

    device->device.protocol_id = MX_PROTOCOL_PCI;
    device->device.protocol_ops = &_pci_protocol;
    device->handle = handle;
    device->index = index;
    *out = &device->device;

finished:
    if (status != NO_ERROR) {
        if (device)
            free(device);
        if (handle >= 0)
            _magenta_handle_close(handle);
    }
    return status;
}

static mx_status_t kpci_init_children(mx_driver_t* drv, mx_device_t* parent) {
    for (uint32_t index = 0;; index++) {
        mx_device_t* device;
        if (kpci_init_child(drv, &device, index) != NO_ERROR) {
            break;
        }
        device_add(device, parent);
    }

    return NO_ERROR;
}

static mx_status_t kpci_drv_init(mx_driver_t* drv) {
    mx_status_t status;

    if ((status = device_create(&kpci_root_dev, drv, "pci", &kpci_device_proto))) {
        return status;
    }

    // make the pci root non-bindable
    device_set_bindable(kpci_root_dev, false);

    if (device_add(kpci_root_dev, NULL) < 0) {
        free(kpci_root_dev);
        return NO_ERROR;
    } else {
        return kpci_init_children(drv, kpci_root_dev);
    }
}

mx_driver_t _driver_kpci BUILTIN_DRIVER = {
    .name = "pci",
    .ops = {
        .init = kpci_drv_init,
    },
};

mx_status_t devmgr_create_pcidev(mx_device_t** out, uint32_t index) {
    return kpci_init_child(&_driver_kpci, out, index);
}

int devmgr_get_pcidev_index(mx_device_t* dev) {
    if (dev->parent == kpci_root_dev) {
        kpci_device_t* pcidev = get_kpci_device(dev);
        return (int)pcidev->index;
    } else {
        return -1;
    }
}
