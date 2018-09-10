// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/acpi.banjo INSTEAD.

#pragma once

#include <ddk/protocol/acpi.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "acpi-internal.h"

// DDK acpi-protocol support
//
// :: Proxies ::
//
// ddk::AcpiProtocolProxy is a simple wrapper around
// acpi_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::AcpiProtocol is a mixin class that simplifies writing DDK drivers
// that implement the acpi protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_ACPI device.
// class AcpiDevice {
// using AcpiDeviceType = ddk::Device<AcpiDevice, /* ddk mixins */>;
//
// class AcpiDevice : public AcpiDeviceType,
//                    public ddk::AcpiProtocol<AcpiDevice> {
//   public:
//     AcpiDevice(zx_device_t* parent)
//         : AcpiDeviceType("my-acpi-protocol-device", parent) {}
//
//     zx_status_t AcpiMapResource(uint32_t resource_id, uint32_t cache_policy, void**
//     out_vaddr_buffer, size_t* vaddr_size, zx_handle_t* out_handle);
//
//     zx_status_t AcpiMapInterrupt(int64_t irq_id, zx_handle_t* out_handle);
//
//     ...
// };

namespace ddk {

template <typename D>
class AcpiProtocol : public internal::base_protocol {
public:
    AcpiProtocol() {
        internal::CheckAcpiProtocolSubclass<D>();
        ops_.map_resource = AcpiMapResource;
        ops_.map_interrupt = AcpiMapInterrupt;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_ACPI;
        ddk_proto_ops_ = &ops_;
    }

protected:
    acpi_protocol_ops_t ops_ = {};

private:
    static zx_status_t AcpiMapResource(void* ctx, uint32_t resource_id, uint32_t cache_policy,
                                       void** out_vaddr_buffer, size_t* vaddr_size,
                                       zx_handle_t* out_handle) {
        return static_cast<D*>(ctx)->AcpiMapResource(resource_id, cache_policy, out_vaddr_buffer,
                                                     vaddr_size, out_handle);
    }
    static zx_status_t AcpiMapInterrupt(void* ctx, int64_t irq_id, zx_handle_t* out_handle) {
        return static_cast<D*>(ctx)->AcpiMapInterrupt(irq_id, out_handle);
    }
};

class AcpiProtocolProxy {
public:
    AcpiProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    AcpiProtocolProxy(const acpi_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(acpi_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    zx_status_t MapResource(uint32_t resource_id, uint32_t cache_policy, void** out_vaddr_buffer,
                            size_t* vaddr_size, zx_handle_t* out_handle) {
        return ops_->map_resource(ctx_, resource_id, cache_policy, out_vaddr_buffer, vaddr_size,
                                  out_handle);
    }
    zx_status_t MapInterrupt(int64_t irq_id, zx_handle_t* out_handle) {
        return ops_->map_interrupt(ctx_, irq_id, out_handle);
    }

private:
    acpi_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
