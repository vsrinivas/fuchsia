// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.clock banjo file

#ifndef SRC_DEVICES_CLOCK_LIB_BANJO_CLOCK_C_BANJO_H_
#define SRC_DEVICES_CLOCK_LIB_BANJO_CLOCK_C_BANJO_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct clock_protocol clock_protocol_t;
typedef struct clock_protocol_ops clock_protocol_ops_t;

// Declarations
struct clock_protocol_ops {
  zx_status_t (*enable)(void* ctx);
  zx_status_t (*disable)(void* ctx);
  zx_status_t (*is_enabled)(void* ctx, bool* out_enabled);
  zx_status_t (*set_rate)(void* ctx, uint64_t hz);
  zx_status_t (*query_supported_rate)(void* ctx, uint64_t hz_in, uint64_t* out_hz_out);
  zx_status_t (*get_rate)(void* ctx, uint64_t* out_hz);
  zx_status_t (*set_input)(void* ctx, uint32_t idx);
  zx_status_t (*get_num_inputs)(void* ctx, uint32_t* out_n);
  zx_status_t (*get_input)(void* ctx, uint32_t* out_index);
};

struct clock_protocol {
  clock_protocol_ops_t* ops;
  void* ctx;
};

// Helpers
// Enables (ungates) this clock.
// Drivers *must* call enable on all clocks they depend upon.
static inline zx_status_t clock_enable(const clock_protocol_t* proto) {
  return proto->ops->enable(proto->ctx);
}

// Disables (gates) this clock.
// Drivers should call this method to indicate to the clock subsystem that
// a particular clock signal is no longer necessary.
static inline zx_status_t clock_disable(const clock_protocol_t* proto) {
  return proto->ops->disable(proto->ctx);
}

// Returns `true` if a given clock is running.
// May query the hardware or return a cached value.
static inline zx_status_t clock_is_enabled(const clock_protocol_t* proto, bool* out_enabled) {
  return proto->ops->is_enabled(proto->ctx, out_enabled);
}

// Attempt to set the rate of the clock provider.
static inline zx_status_t clock_set_rate(const clock_protocol_t* proto, uint64_t hz) {
  return proto->ops->set_rate(proto->ctx, hz);
}

// Query the hardware for the highest supported rate that does not
// exceed hz_in.
static inline zx_status_t clock_query_supported_rate(const clock_protocol_t* proto, uint64_t hz_in,
                                                     uint64_t* out_hz_out) {
  return proto->ops->query_supported_rate(proto->ctx, hz_in, out_hz_out);
}

// Returns the current rate that a given clock is running at.
static inline zx_status_t clock_get_rate(const clock_protocol_t* proto, uint64_t* out_hz) {
  return proto->ops->get_rate(proto->ctx, out_hz);
}

// Sets the input of this clock by index. I.e. by selecting a mux.
// This clock has N inputs defined 0 through N-1, which are valid arguemts
// as the index to SetInput.
static inline zx_status_t clock_set_input(const clock_protocol_t* proto, uint32_t idx) {
  return proto->ops->set_input(proto->ctx, idx);
}

// Returns the number of inputs this clock has.
// Any value between 0 and UINT32_MAX is a valid return for this method.
// A Root Oscillator may return 0 for instance, if it has no inputs.
static inline zx_status_t clock_get_num_inputs(const clock_protocol_t* proto, uint32_t* out_n) {
  return proto->ops->get_num_inputs(proto->ctx, out_n);
}

// Returns the index of the current input of this clock.
static inline zx_status_t clock_get_input(const clock_protocol_t* proto, uint32_t* out_index) {
  return proto->ops->get_input(proto->ctx, out_index);
}

__END_CDECLS

#endif  // SRC_DEVICES_CLOCK_LIB_BANJO_CLOCK_C_BANJO_H_
