// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/hidbus.fidl INSTEAD.

#pragma once

#include <ddk/protocol/hidbus.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "hidbus-internal.h"

// DDK hidbus-protocol support
//
// :: Proxies ::
//
// ddk::HidbusProtocolProxy is a simple wrapper around
// hidbus_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::HidbusProtocol is a mixin class that simplifies writing DDK drivers
// that implement the hidbus protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_HIDBUS device.
// class HidbusDevice {
// using HidbusDeviceType = ddk::Device<HidbusDevice, /* ddk mixins */>;
//
// class HidbusDevice : public HidbusDeviceType,
//                      public ddk::HidbusProtocol<HidbusDevice> {
//   public:
//     HidbusDevice(zx_device_t* parent)
//         : HidbusDeviceType("my-hidbus-protocol-device", parent) {}
//
//     zx_status_t HidbusQuery(uint32_t options, hid_info_t* out_info);
//
//     zx_status_t HidbusStart(const hidbus_ifc_t* ifc);
//
//     void HidbusStop();
//
//     zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void** out_data_buffer,
//     size_t* data_size);
//
//     zx_status_t HidbusGetReport(hid_report_type_t rpt_type, uint8_t rpt_id, void*
//     out_data_buffer, size_t data_size, size_t* out_data_actual);
//
//     zx_status_t HidbusSetReport(hid_report_type_t rpt_type, uint8_t rpt_id, const void*
//     data_buffer, size_t data_size);
//
//     zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* out_duration);
//
//     zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
//
//     zx_status_t HidbusGetProtocol(hid_protocol_t* out_protocol);
//
//     zx_status_t HidbusSetProtocol(hid_protocol_t protocol);
//
//     ...
// };

namespace ddk {

template <typename D>
class HidbusIfc : public internal::base_mixin {
public:
    HidbusIfc() {
        internal::CheckHidbusIfcSubclass<D>();
        hidbus_ifc_ops_.io_queue = HidbusIfcIoQueue;
    }

protected:
    hidbus_ifc_ops_t hidbus_ifc_ops_ = {};

private:
    // Queues a report received by the hidbus device.
    static void HidbusIfcIoQueue(void* ctx, const void* buf_buffer, size_t buf_size) {
        static_cast<D*>(ctx)->HidbusIfcIoQueue(buf_buffer, buf_size);
    }
};

class HidbusIfcProxy {
public:
    HidbusIfcProxy() : ops_(nullptr), ctx_(nullptr) {}
    HidbusIfcProxy(const hidbus_ifc_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(hidbus_ifc_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Queues a report received by the hidbus device.
    void IoQueue(const void* buf_buffer, size_t buf_size) {
        ops_->io_queue(ctx_, buf_buffer, buf_size);
    }

private:
    hidbus_ifc_ops_t* ops_;
    void* ctx_;
};

template <typename D>
class HidbusProtocol : public internal::base_mixin {
public:
    HidbusProtocol() {
        internal::CheckHidbusProtocolSubclass<D>();
        hidbus_protocol_ops_.query = HidbusQuery;
        hidbus_protocol_ops_.start = HidbusStart;
        hidbus_protocol_ops_.stop = HidbusStop;
        hidbus_protocol_ops_.get_descriptor = HidbusGetDescriptor;
        hidbus_protocol_ops_.get_report = HidbusGetReport;
        hidbus_protocol_ops_.set_report = HidbusSetReport;
        hidbus_protocol_ops_.get_idle = HidbusGetIdle;
        hidbus_protocol_ops_.set_idle = HidbusSetIdle;
        hidbus_protocol_ops_.get_protocol = HidbusGetProtocol;
        hidbus_protocol_ops_.set_protocol = HidbusSetProtocol;
    }

protected:
    hidbus_protocol_ops_t hidbus_protocol_ops_ = {};

private:
    // Obtain information about the hidbus device and supported features.
    // Safe to call at any time.
    static zx_status_t HidbusQuery(void* ctx, uint32_t options, hid_info_t* out_info) {
        return static_cast<D*>(ctx)->HidbusQuery(options, out_info);
    }
    // Start the hidbus device. The device may begin queueing hid reports via
    // ifc->io_queue before this function returns. It is an error to start an
    // already-started hidbus device.
    static zx_status_t HidbusStart(void* ctx, const hidbus_ifc_t* ifc) {
        return static_cast<D*>(ctx)->HidbusStart(ifc);
    }
    // Stop the hidbus device. Safe to call if the hidbus is already stopped.
    static void HidbusStop(void* ctx) { static_cast<D*>(ctx)->HidbusStop(); }
    // What are the ownership semantics with regards to the data buffer passed back?
    // is len an input and output parameter?
    static zx_status_t HidbusGetDescriptor(void* ctx, hid_description_type_t desc_type,
                                           void** out_data_buffer, size_t* data_size) {
        return static_cast<D*>(ctx)->HidbusGetDescriptor(desc_type, out_data_buffer, data_size);
    }
    static zx_status_t HidbusGetReport(void* ctx, hid_report_type_t rpt_type, uint8_t rpt_id,
                                       void* out_data_buffer, size_t data_size,
                                       size_t* out_data_actual) {
        return static_cast<D*>(ctx)->HidbusGetReport(rpt_type, rpt_id, out_data_buffer, data_size,
                                                     out_data_actual);
    }
    static zx_status_t HidbusSetReport(void* ctx, hid_report_type_t rpt_type, uint8_t rpt_id,
                                       const void* data_buffer, size_t data_size) {
        return static_cast<D*>(ctx)->HidbusSetReport(rpt_type, rpt_id, data_buffer, data_size);
    }
    static zx_status_t HidbusGetIdle(void* ctx, uint8_t rpt_id, uint8_t* out_duration) {
        return static_cast<D*>(ctx)->HidbusGetIdle(rpt_id, out_duration);
    }
    static zx_status_t HidbusSetIdle(void* ctx, uint8_t rpt_id, uint8_t duration) {
        return static_cast<D*>(ctx)->HidbusSetIdle(rpt_id, duration);
    }
    static zx_status_t HidbusGetProtocol(void* ctx, hid_protocol_t* out_protocol) {
        return static_cast<D*>(ctx)->HidbusGetProtocol(out_protocol);
    }
    static zx_status_t HidbusSetProtocol(void* ctx, hid_protocol_t protocol) {
        return static_cast<D*>(ctx)->HidbusSetProtocol(protocol);
    }
};

class HidbusProtocolProxy {
public:
    HidbusProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    HidbusProtocolProxy(const hidbus_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(hidbus_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Obtain information about the hidbus device and supported features.
    // Safe to call at any time.
    zx_status_t Query(uint32_t options, hid_info_t* out_info) {
        return ops_->query(ctx_, options, out_info);
    }
    // Start the hidbus device. The device may begin queueing hid reports via
    // ifc->io_queue before this function returns. It is an error to start an
    // already-started hidbus device.
    zx_status_t Start(const hidbus_ifc_t* ifc) { return ops_->start(ctx_, ifc); }
    // Stop the hidbus device. Safe to call if the hidbus is already stopped.
    void Stop() { ops_->stop(ctx_); }
    // What are the ownership semantics with regards to the data buffer passed back?
    // is len an input and output parameter?
    zx_status_t GetDescriptor(hid_description_type_t desc_type, void** out_data_buffer,
                              size_t* data_size) {
        return ops_->get_descriptor(ctx_, desc_type, out_data_buffer, data_size);
    }
    zx_status_t GetReport(hid_report_type_t rpt_type, uint8_t rpt_id, void* out_data_buffer,
                          size_t data_size, size_t* out_data_actual) {
        return ops_->get_report(ctx_, rpt_type, rpt_id, out_data_buffer, data_size,
                                out_data_actual);
    }
    zx_status_t SetReport(hid_report_type_t rpt_type, uint8_t rpt_id, const void* data_buffer,
                          size_t data_size) {
        return ops_->set_report(ctx_, rpt_type, rpt_id, data_buffer, data_size);
    }
    zx_status_t GetIdle(uint8_t rpt_id, uint8_t* out_duration) {
        return ops_->get_idle(ctx_, rpt_id, out_duration);
    }
    zx_status_t SetIdle(uint8_t rpt_id, uint8_t duration) {
        return ops_->set_idle(ctx_, rpt_id, duration);
    }
    zx_status_t GetProtocol(hid_protocol_t* out_protocol) {
        return ops_->get_protocol(ctx_, out_protocol);
    }
    zx_status_t SetProtocol(hid_protocol_t protocol) { return ops_->set_protocol(ctx_, protocol); }

private:
    hidbus_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
