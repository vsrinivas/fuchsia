// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_PIPE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_PIPE_H_

#include <fuchsia/hardware/display/controller/c/banjo.h>
#include <lib/edid/edid.h>
#include <lib/fit/function.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/vmo.h>

#include <ddktl/device.h>
#include <region-alloc/region-alloc.h>

#include "gtt.h"
#include "power.h"
#include "registers-ddi.h"
#include "registers-pipe.h"
#include "registers-transcoder.h"

namespace i915 {

class Controller;
class DisplayDevice;

class Pipe {
 public:
  Pipe(i915::Pipe&& other) : Pipe(other.mmio_space_, other.pipe_, std::move(other.pipe_power_)) {}

  Pipe(ddk::MmioBuffer* mmio_space, registers::Pipe pipe, PowerWellRef pipe_power);

  void AttachToDisplay(uint64_t display_id, bool is_edp);
  void Detach();

  void ApplyModeConfig(const display_mode_t& mode);

  using SetupGttImageFunc = fit::function<uint64_t(const image_t* image, uint32_t rotation)>;
  void ApplyConfiguration(const display_config_t* config, const SetupGttImageFunc& setup_gtt_image);

  // The controller will reset pipe registers and pipe transcoder registers.
  // TODO(fxbug.dev/83998): Remove the circular dependency between Controller
  // and Pipe.
  void Reset(Controller* controller);

  void LoadActiveMode(display_mode_t* mode);

  registers::Pipe pipe() const { return pipe_; }
  registers::Trans transcoder() const {
    return attached_edp_ ? registers::TRANS_EDP : static_cast<registers::Trans>(pipe_);
  }

  uint64_t attached_display_id() const { return attached_display_; }
  bool in_use() const { return attached_display_ != INVALID_DISPLAY_ID; }

 private:
  // Borrowed reference to Controller instance
  ddk::MmioBuffer* mmio_space_ = nullptr;

  void ConfigurePrimaryPlane(uint32_t plane_num, const primary_layer_t* primary, bool enable_csc,
                             bool* scaler_1_claimed, registers::pipe_arming_regs* regs,
                             const SetupGttImageFunc& setup_gtt_image);
  void ConfigureCursorPlane(const cursor_layer_t* cursor, bool enable_csc,
                            registers::pipe_arming_regs* regs);
  void SetColorConversionOffsets(bool preoffsets, const float vals[3]);

  uint64_t attached_display_ = INVALID_DISPLAY_ID;
  bool attached_edp_ = false;

  registers::Pipe pipe_;

  PowerWellRef pipe_power_;

  // For any scaled planes, this contains the (1-based) index of the active scaler
  uint32_t scaled_planes_[registers::kPipeCount][registers::kImagePlaneCount] = {};
};

}  // namespace i915

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_PIPE_H_
