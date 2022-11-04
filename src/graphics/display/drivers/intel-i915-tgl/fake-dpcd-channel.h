// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_FAKE_DPCD_CHANNEL_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_FAKE_DPCD_CHANNEL_H_

#include <cstdint>

#include "src/graphics/display/drivers/intel-i915-tgl/dp-display.h"

namespace i915_tgl {
namespace testing {

constexpr uint8_t kDefaultLaneCount = 2;
constexpr uint8_t kDefaultSinkCount = 1;
constexpr size_t kMaxLinkRateTableEntries =
    (dpcd::DPCD_SUPPORTED_LINK_RATE_END + 1 - dpcd::DPCD_SUPPORTED_LINK_RATE_START) / 2;

// FakeDpcdChannel is a utility that allows the DPCD register space to be mocked for test that need
// to exercise DisplayPort functionality.
class FakeDpcdChannel : public DpcdChannel {
 public:
  FakeDpcdChannel() { registers.fill(0); }

  // Populates the bare minimum of required fields to form a valid set of capabilities.
  void SetDefaults();

  void SetDpcdRevision(dpcd::Revision rev) {
    registers[dpcd::DPCD_REV] = static_cast<uint8_t>(rev);
  }

  void SetMaxLinkRate(uint8_t value) { registers[dpcd::DPCD_MAX_LINK_RATE] = value; }

  void SetMaxLaneCount(uint8_t value) { registers[dpcd::DPCD_MAX_LANE_COUNT] = value; }

  void SetSinkCount(uint8_t value) { registers[dpcd::DPCD_SINK_COUNT] = value; }

  void SetEdpCapable(dpcd::EdpRevision rev) {
    dpcd::EdpConfigCap reg;
    reg.set_dpcd_display_ctrl_capable(1);
    registers[dpcd::DPCD_EDP_CONFIG] = reg.reg_value();
    registers[dpcd::DPCD_EDP_REV] = static_cast<uint8_t>(rev);
  }

  void SetEdpBacklightBrightnessCapable() {
    dpcd::EdpGeneralCap1 gc;
    gc.set_tcon_backlight_adjustment_cap(1);
    gc.set_backlight_aux_enable_cap(1);
    registers[dpcd::DPCD_EDP_GENERAL_CAP1] = gc.reg_value();

    dpcd::EdpBacklightCap bc;
    bc.set_brightness_aux_set_cap(1);
    registers[dpcd::DPCD_EDP_BACKLIGHT_CAP] = bc.reg_value();
  }

  void PopulateLinkRateTable(std::vector<uint16_t> values);

  // DpcdChannel overrides:
  bool DpcdRead(uint32_t addr, uint8_t* buf, size_t size) override;
  bool DpcdWrite(uint32_t addr, const uint8_t* buf, size_t size) override;

  // The full DPCD field mapping spans addresses 0x00000-0xFFFFF however it's sufficient for us to
  // allocate only the subset that the driver uses. 0x800 contains all addresses up to and
  // including eDP-specific registers (see eDP v1.4a, 2.9.3 "DPCD Field Address Mapping").
  std::array<uint8_t, 0x800> registers;
};

}  // namespace testing
}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_FAKE_DPCD_CHANNEL_H_
