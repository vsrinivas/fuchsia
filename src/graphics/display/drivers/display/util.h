// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_UTIL_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_UTIL_H_

#include <fidl/fuchsia.hardware.display/cpp/wire.h>
#include <fuchsia/hardware/display/controller/c/banjo.h>

namespace display {

// Value-only wrappers cannot be compared directly using binary operators.
// This defines binary comparison operators for FIDL ConfigStamp and banjo
// config_stamp_t structs.
#define DECLARE_BINARY_OPERATOR(TYPE, OP) bool operator OP(const TYPE& a, const TYPE& b);

DECLARE_BINARY_OPERATOR(fuchsia_hardware_display::wire::ConfigStamp, ==)
DECLARE_BINARY_OPERATOR(fuchsia_hardware_display::wire::ConfigStamp, !=)
DECLARE_BINARY_OPERATOR(fuchsia_hardware_display::wire::ConfigStamp, >)
DECLARE_BINARY_OPERATOR(fuchsia_hardware_display::wire::ConfigStamp, >=)
DECLARE_BINARY_OPERATOR(fuchsia_hardware_display::wire::ConfigStamp, <)
DECLARE_BINARY_OPERATOR(fuchsia_hardware_display::wire::ConfigStamp, <=)

DECLARE_BINARY_OPERATOR(config_stamp_t, ==)
DECLARE_BINARY_OPERATOR(config_stamp_t, !=)
DECLARE_BINARY_OPERATOR(config_stamp_t, >)
DECLARE_BINARY_OPERATOR(config_stamp_t, >=)
DECLARE_BINARY_OPERATOR(config_stamp_t, <)
DECLARE_BINARY_OPERATOR(config_stamp_t, <=)

#undef DECLARE_BINARY_OPERATOR

}  // namespace display

// TODO(fxbug.dev/89828): FIDL and banjo don't support constant structs, for
// convenience we define the const structs here in their corresponding
// namespaces.
namespace fuchsia_hardware_display::wire {

constexpr ConfigStamp kInvalidConfigStampFidl = {
    .value = kInvalidConfigStampValue,
};

}  // namespace fuchsia_hardware_display::wire

constexpr config_stamp_t INVALID_CONFIG_STAMP_BANJO = {
    .value = INVALID_CONFIG_STAMP_VALUE,
};

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_UTIL_H_
