// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_PROPERTIES_H_
#define GARNET_LIB_PERFMON_PROPERTIES_H_

#include <stdint.h>

namespace perfmon {

// The properties of this system.
// This is a copy of the FIDL struct, kept separate to not pass a FIDL
// dependency on to our clients.
struct Properties {
  // Values for |flags|.
  static constexpr uint32_t kFlagHasLastBranch = 1u << 0;

  // S/W API version = PERFMON_API_VERSION.
  uint16_t api_version;

  // The H/W Performance Monitor version.
  uint16_t pm_version;

  // The maximum number of events that can be simultaneously supported.
  // The combination of events that can be simultaneously supported is
  // architecture/model specific.
  uint16_t max_num_events;

  // Padding/reserved.
  uint16_t reserved;

  // The maximum number of fixed events that can be simultaneously
  // supported, and their maximum width.
  // These values are for informational/display purposes.
  uint16_t max_num_fixed_events;
  uint16_t max_fixed_counter_width;

  // The maximum number of programmable events that can be simultaneously
  // supported, and their maximum width.
  // These values are for informational/display purposes.
  uint16_t max_num_programmable_events;
  uint16_t max_programmable_counter_width;

  // The maximum number of misc events that can be simultaneously
  // supported, and their maximum width.
  // These values are for informational/display purposes.
  uint16_t max_num_misc_events;
  uint16_t max_misc_counter_width;

  // Various flags.
  uint32_t flags;
};

}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_PROPERTIES_H_
