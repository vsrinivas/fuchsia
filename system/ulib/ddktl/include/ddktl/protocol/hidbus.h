// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/hidbus.h>
#include <ddktl/protocol/hidbus-internal.h>
#include <zircon/assert.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>

// DDK hidbus protocol support
//
// :: Proxies ::
//
// ddk::HidBusIfcProxy is simple wrappers around hidbus_ifc_t. It does not own the pointers passed
// to it.
//
// :: Mixins ::
//
// ddk::HidBusProtocol is a mixin class that simplifies writing DDK drivers that
// implement the hidbus protocol. They take care of implementing the function pointer tables
// and calling into the object that wraps them.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_HIDBUS device
// class HidBusDevice;
// using HidBusDeviceType = ddk::Device<HidBusDevice, /* ddk mixins */>;
//
// class HidBusDevice : public HidBusDeviceType,
//                      public ddk::HidBusProtocol<HidBusDevice> {
//   public:
//     HidBusDevice(zx_device_t* parent)
//       : HidBusDeviceType(parent) {}
//
//     zx_status_t Bind() {
//         DdkAdd();
//     }
//
//     void DdkRelease() {
//         // Clean up
//     }
//
//     zx_status_t HidBusStart(ddk::HidBusIfcProxy proxy) {
//         // Start hidbus operation
//         proxy_ = proxy;
//         return ZX_OK;
//     }
//
//     zx_status_t HidBusQuery(uint32_t options, hid_info_t* info) {
//         ...
//     }
//
//     ...
//   private:
//     ddk::HidBusIfcProxy proxy_;
//     ...
// };

namespace ddk {

class HidBusIfcProxy {
  public:
    HidBusIfcProxy()
      : ifc_(nullptr), cookie_(nullptr) {}

    HidBusIfcProxy(hidbus_ifc_t* ifc, void* cookie)
      : ifc_(ifc), cookie_(cookie) {}

    void IoQueue(const uint8_t* buf, size_t len) {
        ifc_->io_queue(cookie_, buf, len);
    }

    bool is_valid() const {
        return ifc_ != nullptr;
    }

    void clear() {
        ifc_ = nullptr;
        cookie_ = nullptr;
    }

  private:
    hidbus_ifc_t* ifc_;
    void* cookie_;
};

template <typename D>
class HidBusProtocol : public internal::base_protocol {
  public:
    HidBusProtocol() {
        internal::CheckHidBusProtocolSubclass<D>();
        ops_.query = Query;
        ops_.start = Start;
        ops_.stop = Stop;
        ops_.get_descriptor = GetDescriptor;
        ops_.get_report = GetReport;
        ops_.set_report = SetReport;
        ops_.get_idle = GetIdle;
        ops_.set_idle = SetIdle;
        ops_.get_protocol = GetProtocol;
        ops_.set_protocol = SetProtocol;

        // Can only inherit from one base_protocol implemenation
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_HIDBUS;
        ddk_proto_ops_ = &ops_;
    }

  private:
    static zx_status_t Query(void* ctx, uint32_t options, hid_info_t* info) {
        return static_cast<D*>(ctx)->HidBusQuery(options, info);
    }

    static zx_status_t Start(void* ctx, hidbus_ifc_t* ifc, void* cookie) {
        HidBusIfcProxy proxy(ifc, cookie);
        return static_cast<D*>(ctx)->HidBusStart(proxy);
    }

    static void Stop(void* ctx) {
        static_cast<D*>(ctx)->HidBusStop();
    }

    static zx_status_t GetDescriptor(void* ctx, uint8_t desc_type, void** data, size_t* len) {
        return static_cast<D*>(ctx)->HidBusGetDescriptor(desc_type, data, len);
    }

    static zx_status_t GetReport(void* ctx, uint8_t rpt_type, uint8_t rpt_id, void* data,
                                 size_t len, size_t* out_len) {
        return static_cast<D*>(ctx)->HidBusGetReport(rpt_type, rpt_id, data, len, out_len);
    }

    static zx_status_t SetReport(void* ctx, uint8_t rpt_type, uint8_t rpt_id, void* data,
                                 size_t len) {
        return static_cast<D*>(ctx)->HidBusSetReport(rpt_type, rpt_id, data, len);
    }

    static zx_status_t GetIdle(void* ctx, uint8_t rpt_id, uint8_t* duration) {
        return static_cast<D*>(ctx)->HidBusGetIdle(rpt_id, duration);
    }

    static zx_status_t SetIdle(void* ctx, uint8_t rpt_id, uint8_t duration) {
        return static_cast<D*>(ctx)->HidBusSetIdle(rpt_id, duration);
    }

    static zx_status_t GetProtocol(void* ctx, uint8_t* protocol) {
        return static_cast<D*>(ctx)->HidBusGetProtocol(protocol);
    }

    static zx_status_t SetProtocol(void* ctx, uint8_t protocol) {
        return static_cast<D*>(ctx)->HidBusSetProtocol(protocol);
    }

    hidbus_protocol_ops_t ops_ = {};
};

}  // namespace ddk
