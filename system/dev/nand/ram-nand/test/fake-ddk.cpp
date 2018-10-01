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

    if (metadata_) {
        if (length != metadata_length_ || memcmp(data, metadata_, length) != 0) {
            unittest_printf_critical("Unexpected metadata\n");
            return ZX_ERR_BAD_STATE;
        }
    } else {
        metadata_length_ += length;
    }
    add_metadata_calls_++;
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

void Bind::ExpectMetadata(const void* data, size_t data_length) {
    metadata_ = data;
    metadata_length_ = data_length;
}

void Bind::GetMetadataInfo(int* num_calls, size_t* length) {
    *num_calls = add_metadata_calls_;
    *length = metadata_length_;
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
