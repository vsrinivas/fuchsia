// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-ddk.h"

#include <stdlib.h>

#include <unittest/unittest.h>
#include <zircon/assert.h>

namespace fake_ddk {

zx_device_t* kFakeDevice = reinterpret_cast<zx_device_t*>(0x55);
zx_device_t* kFakeParent = reinterpret_cast<zx_device_t*>(0xaa);

Bind* Bind::instance_ = nullptr;

Bind::Bind() {
    ZX_ASSERT(!instance_);
    instance_ = this;
}

zx_status_t Bind::DeviceAdd(zx_driver_t* drv, zx_device_t* parent,
                            device_add_args_t* args, zx_device_t** out) {
    if (parent != kFakeParent) {
        bad_parent_ = true;
    }

    *out = kFakeDevice;
    add_called_ = true;
    return ZX_OK;
}

zx_status_t Bind::DeviceRemove(zx_device_t* device) {
    if (device != kFakeDevice) {
        bad_device_ = true;
    }
    remove_called_ = true;
    return ZX_OK;
}

zx_status_t Bind::DeviceAddMetadata(zx_device_t* device, uint32_t type, const void* data,
                                    size_t length) {
    if (device != kFakeDevice) {
        bad_device_ = true;
    }
    add_metadata_called_ = true;
    return ZX_OK;
}

void Bind::DeviceMakeVisible(zx_device_t* device) {
    if (device != kFakeDevice) {
        bad_device_ = true;
    }
    make_visible_called_ = true;
    return;
}

bool Bind::Ok() {
    BEGIN_HELPER;
    EXPECT_TRUE(add_called_);
    EXPECT_TRUE(remove_called_);
    EXPECT_FALSE(bad_parent_);
    EXPECT_FALSE(bad_device_);
    END_HELPER;
}

}  // namespace fake_ddk

zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent,
                                   device_add_args_t* args, zx_device_t** out) {
    if (!fake_ddk::Bind::Instance()) {
        return ZX_OK;
    }
    return fake_ddk::Bind::Instance()->DeviceAdd(drv, parent, args, out);
}

zx_status_t device_remove(zx_device_t* device) {
    if (!fake_ddk::Bind::Instance()) {
        return ZX_OK;
    }
    return fake_ddk::Bind::Instance()->DeviceRemove(device);
}

zx_status_t device_add_metadata(zx_device_t* device, uint32_t type, const void* data,
                                size_t length) {
    if (!fake_ddk::Bind::Instance()) {
        return ZX_OK;
    }
    return fake_ddk::Bind::Instance()->DeviceAddMetadata(device, type, data, length);
}

void device_make_visible(zx_device_t* device) {
    if (fake_ddk::Bind::Instance()) {
        fake_ddk::Bind::Instance()->DeviceMakeVisible(device);
    }
    return;
}


zx_driver_rec __zircon_driver_rec__ = {};
