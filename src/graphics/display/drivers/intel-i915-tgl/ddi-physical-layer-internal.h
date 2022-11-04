// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_PHYSICAL_LAYER_INTERNAL_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_PHYSICAL_LAYER_INTERNAL_H_

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-physical-layer.h"

namespace i915_tgl {

// This describes the state machine to Enable / Disable the DDI PHY.
//
//       Uninitialized
//         |    ^
//         v    |
//       Type C Cold Blocked
//         |    ^
//         v    |
//       Safe Mode Set
//         |    ^
//         v    |
//       AUX Powered On
//         |    ^
//         v    |
//       Initialized
//
// The Top-to-bottom direction represents initialization procedure and bottom-
// to-top direction represents deinitialization.
enum class TypeCDdiTigerLake::InitializationPhase {
  // Initialization hasn't started yet.
  // This is the only valid starting state to enable a DDI PHY.
  kUninitialized = 0,

  // The following states are steps of Type-C DDI PHY initialization process.
  // Each state below means that the driver has *attempted* to take this step
  // but cannot guarantee whether this step is successful. The driver can only
  // take a new step when all previous steps have succeeded.

  // Step 1. Block Type-C Cold State.
  kTypeCColdBlocked = 1,

  // Step 2. Disable Type-C safe mode.
  kSafeModeSet = 2,

  // Step 3. Setup DDI AUX channel.
  kAuxPoweredOn = 3,

  // All the steps above have succeeded and the initialization process finishes.
  // In order to initialize a display device, the DDI PHY must be in this state.
  kInitialized = 4,
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_PHYSICAL_LAYER_INTERNAL_H_
