// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_PCI_IDS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_PCI_IDS_H_

#include <stdint.h>
#include <zircon/assert.h>

// PCI Device ID sources.
//
// Skylake: IHD-OS-SKL-Vol 4-05.16 page 11 and page 12
// Kaby Lake: IHD-OS-KBL-Vol 4-1.17 page 10
// Tiger Lake: IHD-OS-TGL-Vol 4-12.21 page 9
//
// Other lines that use Kaby Lake graphics:
// * Coffee Lake: IHD-OS-CFL-Vol 1-1.20 page 10
// * Amber Lake: IHD-OS-AML-Vol 1-1.20 pages 9-10
// * Whiskey Lake: IHD-OS-WHL-Vol 1-1.20 page 7
// * Comet Lake: IHD-OS-CML-Vol 1-4.20 pages 9-10

namespace i915_tgl {

constexpr bool is_skl(uint16_t device_id) { return (device_id & 0xff00) == 0x1900; }

constexpr bool is_kbl(uint16_t device_id) {
  return (device_id & 0xff00) == 0x5900 || (device_id & 0xff00) == 0x3e00;
}

constexpr bool is_tgl(uint16_t device_id) { return (device_id & 0xff00) == 0x9a00; }

constexpr bool is_skl_u(uint16_t device_id) {
  return device_id == 0x1916 || device_id == 0x1906 || device_id == 0x1926 || device_id == 0x1927 ||
         device_id == 0x1923;
}

constexpr bool is_skl_y(uint16_t device_id) { return device_id == 0x191e; }

constexpr bool is_kbl_u(uint16_t device_id) {
  return device_id == 0x5916 || device_id == 0x5926 || device_id == 0x5906 || device_id == 0x5927 ||
         device_id == 0x3ea5;
}

constexpr bool is_kbl_y(uint16_t device_id) { return device_id == 0x591c || device_id == 0x591e; }

constexpr bool is_tgl_u(uint16_t device_id) {
  return device_id == 0x9a49 || device_id == 0x9a78 || device_id == 0x9a40;
}

constexpr uint16_t kTestDeviceDid = 0xffff;
constexpr bool is_test_device(uint16_t device_id) { return device_id == kTestDeviceDid; }

constexpr uint16_t intel_display_device_gen(uint16_t device_id) {
  if (is_skl(device_id) || is_kbl(device_id)) {
    return 9;
  }
  if (is_tgl(device_id)) {
    return 12;
  }
  if (is_test_device(device_id)) {
    return 9;
  }
  ZX_DEBUG_ASSERT_MSG(false, "device id %u not supported", device_id);
  return 0;
}

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_PCI_IDS_H_
