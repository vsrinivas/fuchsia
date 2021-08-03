// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_METADATA_LLCPP_VREG_H_
#define SRC_DEVICES_LIB_METADATA_LLCPP_VREG_H_

#include <fuchsia/hardware/vreg/llcpp/fidl.h>

#include <vector>

namespace vreg {

using fuchsia_hardware_vreg::wire::PwmVregMetadataEntry;
PwmVregMetadataEntry BuildMetadata(fidl::AnyArena& allocator, uint32_t pwm_index,
                                   uint32_t period_ns, uint32_t min_voltage_uv,
                                   uint32_t voltage_step_uv, uint32_t num_steps) {
  PwmVregMetadataEntry entry(allocator);
  entry.set_pwm_index(allocator, pwm_index);
  entry.set_period_ns(allocator, period_ns);
  entry.set_min_voltage_uv(allocator, min_voltage_uv);
  entry.set_voltage_step_uv(allocator, voltage_step_uv);
  entry.set_num_steps(allocator, num_steps);
  return entry;
}

using fuchsia_hardware_vreg::wire::Metadata;
Metadata BuildMetadata(fidl::AnyArena& allocator, fidl::VectorView<PwmVregMetadataEntry> pwm_vreg) {
  Metadata metadata(allocator);
  metadata.set_pwm_vreg(allocator, std::move(pwm_vreg));
  return metadata;
}

}  // namespace vreg

#endif  // SRC_DEVICES_LIB_METADATA_LLCPP_VREG_H_
