// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.clock banjo file

#ifndef SRC_DEVICES_CLOCK_LIB_BANJO_CLOCK_CPP_BANJO_H_
#define SRC_DEVICES_CLOCK_LIB_BANJO_CLOCK_CPP_BANJO_H_

#include <fuchsia/hardware/clock/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <ddktl/device-internal.h>

#include "banjo-internal.h"

// DDK clock-protocol support
//
// :: Proxies ::
//
// ddk::ClockProtocolClient is a simple wrapper around
// clock_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::ClockProtocol is a mixin class that simplifies writing DDK drivers
// that implement the clock protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_CLOCK device.
// class ClockDevice;
// using ClockDeviceType = ddk::Device<ClockDevice, /* ddk mixins */>;
//
// class ClockDevice : public ClockDeviceType,
//                      public ddk::ClockProtocol<ClockDevice> {
//   public:
//     ClockDevice(zx_device_t* parent)
//         : ClockDeviceType(parent) {}
//
//     zx_status_t ClockEnable();
//
//     zx_status_t ClockDisable();
//
//     zx_status_t ClockIsEnabled(bool* out_enabled);
//
//     zx_status_t ClockSetRate(uint64_t hz);
//
//     zx_status_t ClockQuerySupportedRate(uint64_t hz_in, uint64_t* out_hz_out);
//
//     zx_status_t ClockGetRate(uint64_t* out_hz);
//
//     zx_status_t ClockSetInput(uint32_t idx);
//
//     zx_status_t ClockGetNumInputs(uint32_t* out_n);
//
//     zx_status_t ClockGetInput(uint32_t* out_index);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class ClockProtocol : public Base {
 public:
  ClockProtocol() {
    internal::CheckClockProtocolSubclass<D>();
    clock_protocol_ops_.enable = ClockEnable;
    clock_protocol_ops_.disable = ClockDisable;
    clock_protocol_ops_.is_enabled = ClockIsEnabled;
    clock_protocol_ops_.set_rate = ClockSetRate;
    clock_protocol_ops_.query_supported_rate = ClockQuerySupportedRate;
    clock_protocol_ops_.get_rate = ClockGetRate;
    clock_protocol_ops_.set_input = ClockSetInput;
    clock_protocol_ops_.get_num_inputs = ClockGetNumInputs;
    clock_protocol_ops_.get_input = ClockGetInput;

    if constexpr (internal::is_base_proto<Base>::value) {
      auto dev = static_cast<D*>(this);
      // Can only inherit from one base_protocol implementation.
      ZX_ASSERT(dev->ddk_proto_id_ == 0);
      dev->ddk_proto_id_ = ZX_PROTOCOL_CLOCK;
      dev->ddk_proto_ops_ = &clock_protocol_ops_;
    }
  }

 protected:
  clock_protocol_ops_t clock_protocol_ops_ = {};

 private:
  // Enables (ungates) this clock.
  // Drivers *must* call enable on all clocks they depend upon.
  static zx_status_t ClockEnable(void* ctx) {
    auto ret = static_cast<D*>(ctx)->ClockEnable();
    return ret;
  }
  // Disables (gates) this clock.
  // Drivers should call this method to indicate to the clock subsystem that
  // a particular clock signal is no longer necessary.
  static zx_status_t ClockDisable(void* ctx) {
    auto ret = static_cast<D*>(ctx)->ClockDisable();
    return ret;
  }
  // Returns `true` if a given clock is running.
  // May query the hardware or return a cached value.
  static zx_status_t ClockIsEnabled(void* ctx, bool* out_enabled) {
    auto ret = static_cast<D*>(ctx)->ClockIsEnabled(out_enabled);
    return ret;
  }
  // Attempt to set the rate of the clock provider.
  static zx_status_t ClockSetRate(void* ctx, uint64_t hz) {
    auto ret = static_cast<D*>(ctx)->ClockSetRate(hz);
    return ret;
  }
  // Query the hardware for the highest supported rate that does not
  // exceed hz_in.
  static zx_status_t ClockQuerySupportedRate(void* ctx, uint64_t hz_in, uint64_t* out_hz_out) {
    auto ret = static_cast<D*>(ctx)->ClockQuerySupportedRate(hz_in, out_hz_out);
    return ret;
  }
  // Returns the current rate that a given clock is running at.
  static zx_status_t ClockGetRate(void* ctx, uint64_t* out_hz) {
    auto ret = static_cast<D*>(ctx)->ClockGetRate(out_hz);
    return ret;
  }
  // Sets the input of this clock by index. I.e. by selecting a mux.
  // This clock has N inputs defined 0 through N-1, which are valid arguemts
  // as the index to SetInput.
  static zx_status_t ClockSetInput(void* ctx, uint32_t idx) {
    auto ret = static_cast<D*>(ctx)->ClockSetInput(idx);
    return ret;
  }
  // Returns the number of inputs this clock has.
  // Any value between 0 and UINT32_MAX is a valid return for this method.
  // A Root Oscillator may return 0 for instance, if it has no inputs.
  static zx_status_t ClockGetNumInputs(void* ctx, uint32_t* out_n) {
    auto ret = static_cast<D*>(ctx)->ClockGetNumInputs(out_n);
    return ret;
  }
  // Returns the index of the current input of this clock.
  static zx_status_t ClockGetInput(void* ctx, uint32_t* out_index) {
    auto ret = static_cast<D*>(ctx)->ClockGetInput(out_index);
    return ret;
  }
};

class ClockProtocolClient {
 public:
  ClockProtocolClient() : ops_(nullptr), ctx_(nullptr) {}
  ClockProtocolClient(const clock_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

  ClockProtocolClient(zx_device_t* parent) {
    clock_protocol_t proto;
    if (device_get_protocol(parent, ZX_PROTOCOL_CLOCK, &proto) == ZX_OK) {
      ops_ = proto.ops;
      ctx_ = proto.ctx;
    } else {
      ops_ = nullptr;
      ctx_ = nullptr;
    }
  }

  ClockProtocolClient(zx_device_t* parent, const char* fragment_name) {
    clock_protocol_t proto;
    if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_CLOCK, &proto) == ZX_OK) {
      ops_ = proto.ops;
      ctx_ = proto.ctx;
    } else {
      ops_ = nullptr;
      ctx_ = nullptr;
    }
  }

  // Create a ClockProtocolClient from the given parent device + "fragment".
  //
  // If ZX_OK is returned, the created object will be initialized in |result|.
  static zx_status_t CreateFromDevice(zx_device_t* parent, ClockProtocolClient* result) {
    clock_protocol_t proto;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_CLOCK, &proto);
    if (status != ZX_OK) {
      return status;
    }
    *result = ClockProtocolClient(&proto);
    return ZX_OK;
  }

  // Create a ClockProtocolClient from the given parent device.
  //
  // If ZX_OK is returned, the created object will be initialized in |result|.
  static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                      ClockProtocolClient* result) {
    clock_protocol_t proto;
    zx_status_t status =
        device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_CLOCK, &proto);
    if (status != ZX_OK) {
      return status;
    }
    *result = ClockProtocolClient(&proto);
    return ZX_OK;
  }

  void GetProto(clock_protocol_t* proto) const {
    proto->ctx = ctx_;
    proto->ops = ops_;
  }
  bool is_valid() const { return ops_ != nullptr; }
  void clear() {
    ctx_ = nullptr;
    ops_ = nullptr;
  }

  // Enables (ungates) this clock.
  // Drivers *must* call enable on all clocks they depend upon.
  zx_status_t Enable() const { return ops_->enable(ctx_); }

  // Disables (gates) this clock.
  // Drivers should call this method to indicate to the clock subsystem that
  // a particular clock signal is no longer necessary.
  zx_status_t Disable() const { return ops_->disable(ctx_); }

  // Returns `true` if a given clock is running.
  // May query the hardware or return a cached value.
  zx_status_t IsEnabled(bool* out_enabled) const { return ops_->is_enabled(ctx_, out_enabled); }

  // Attempt to set the rate of the clock provider.
  zx_status_t SetRate(uint64_t hz) const { return ops_->set_rate(ctx_, hz); }

  // Query the hardware for the highest supported rate that does not
  // exceed hz_in.
  zx_status_t QuerySupportedRate(uint64_t hz_in, uint64_t* out_hz_out) const {
    return ops_->query_supported_rate(ctx_, hz_in, out_hz_out);
  }

  // Returns the current rate that a given clock is running at.
  zx_status_t GetRate(uint64_t* out_hz) const { return ops_->get_rate(ctx_, out_hz); }

  // Sets the input of this clock by index. I.e. by selecting a mux.
  // This clock has N inputs defined 0 through N-1, which are valid arguemts
  // as the index to SetInput.
  zx_status_t SetInput(uint32_t idx) const { return ops_->set_input(ctx_, idx); }

  // Returns the number of inputs this clock has.
  // Any value between 0 and UINT32_MAX is a valid return for this method.
  // A Root Oscillator may return 0 for instance, if it has no inputs.
  zx_status_t GetNumInputs(uint32_t* out_n) const { return ops_->get_num_inputs(ctx_, out_n); }

  // Returns the index of the current input of this clock.
  zx_status_t GetInput(uint32_t* out_index) const { return ops_->get_input(ctx_, out_index); }

 private:
  clock_protocol_ops_t* ops_;
  void* ctx_;
};

}  // namespace ddk

#endif  // SRC_DEVICES_CLOCK_LIB_BANJO_CLOCK_CPP_BANJO_H_
