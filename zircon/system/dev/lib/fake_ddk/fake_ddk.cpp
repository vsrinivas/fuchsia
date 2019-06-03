// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <stdlib.h>

#include <utility>

#include <lib/fake_ddk/fake_ddk.h>
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

void Bind::SetProtocols(fbl::Array<ProtocolEntry>&& protocols) {
    protocols_ = std::move(protocols);
}

void Bind::SetSize(zx_off_t size) {
    size_ = size;
}

void Bind::SetMetadata(const void* data, size_t data_length) {
    get_metadata_ = data;
    get_metadata_length_ = data_length;
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

zx_status_t Bind::DeviceGetMetadata(zx_device_t* dev, uint32_t type, void* buf, size_t buflen,
                                    size_t* actual) {
    if (get_metadata_ == nullptr) {
        return ZX_ERR_BAD_STATE;
    }
    *actual = get_metadata_length_;
    if (buflen < get_metadata_length_) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(buf, get_metadata_, get_metadata_length_);
    get_metadata_calls_++;
    return ZX_OK;
}

zx_status_t Bind::DeviceGetMetadataSize(zx_device_t* dev, uint32_t type, size_t* out_size) {
    if (get_metadata_ == nullptr) {
        return ZX_ERR_BAD_STATE;
    }
    *out_size = get_metadata_length_;
    return ZX_OK;
}

void Bind::DeviceMakeVisible(zx_device_t* device) {
    if (device != kFakeDevice) {
        bad_device_ = true;
    }
    make_visible_called_ = true;
    return;
}

zx_status_t Bind::DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id, void* protocol) {
    if (device != kFakeParent) {
        bad_device_ = true;
        return ZX_ERR_NOT_SUPPORTED;
    }
    auto out = reinterpret_cast<Protocol*>(protocol);
    for (const auto& proto : protocols_) {
        if (proto_id == proto.id) {
            out->ops = proto.proto.ops;
            out->ctx = proto.proto.ctx;
            return ZX_OK;
        }
    }
    return ZX_ERR_NOT_SUPPORTED;
}

const char* Bind::DeviceGetName(zx_device_t* device) {
    if (device != kFakeParent) {
        bad_device_ = true;
    }
    return "";
}

zx_off_t Bind::DeviceGetSize(zx_device_t* device) {
    if (device != kFakeParent) {
        bad_device_ = true;
    }
    return size_;
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

zx_status_t device_get_protocol(const zx_device_t* device, uint32_t proto_id, void* protocol) {
    if (!fake_ddk::Bind::Instance()) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return fake_ddk::Bind::Instance()->DeviceGetProtocol(device, proto_id, protocol);
}

const char* device_get_name(zx_device_t* device) {
    if (!fake_ddk::Bind::Instance()) {
        return nullptr;
    }
    return fake_ddk::Bind::Instance()->DeviceGetName(device);
}

zx_off_t device_get_size(zx_device_t* device) {
    if (!fake_ddk::Bind::Instance()) {
        return 0;
    }
    return fake_ddk::Bind::Instance()->DeviceGetSize(device);
}

zx_status_t device_get_metadata(zx_device_t* device, uint32_t type, void* buf, size_t buflen,
                                size_t* actual) {
    if (!fake_ddk::Bind::Instance()) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return fake_ddk::Bind::Instance()->DeviceGetMetadata(device, type, buf, buflen, actual);
}

zx_status_t device_get_metadata_size(zx_device_t* device, uint32_t type, size_t* out_size) {
    if (!fake_ddk::Bind::Instance()) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return fake_ddk::Bind::Instance()->DeviceGetMetadataSize(device, type, out_size);
}

void device_state_clr_set(zx_device_t* dev, zx_signals_t clearflag, zx_signals_t setflag) {
    // This is currently a no-op.
}

extern "C" void driver_printf(uint32_t flags, const char* fmt, ...) {}

__WEAK zx_driver_rec __zircon_driver_rec__ = {};
