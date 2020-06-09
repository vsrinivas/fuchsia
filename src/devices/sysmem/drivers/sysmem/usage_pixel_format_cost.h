// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_USAGE_PIXEL_FORMAT_COST_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_USAGE_PIXEL_FORMAT_COST_H_

#include <fuchsia/sysmem2/llcpp/fidl.h>

#include <cstdint>

namespace sysmem_driver {

// This class effectively breaks ties in a platform-specific way among the list
// of PixelFormat(s) that a set of participants are all able to support.
//
// At first, the list of PixelFormat(s) that all participants are able to
// support is likely to be a short list.  But even if that list is only 2
// entries long, we'll typically want to prefer a particular choice depending
// on considerations like max throughput, power usage, efficiency
// considerations, etc.
//
// For now, the overrides are baked into sysmem based on the platform ID (AKA
// PID), in usage_overrides_*.cpp.
//
// Any override will take precidence over the default PixelFormat sort order.
class UsagePixelFormatCost {
 public:
  // Compare the cost of two pixel formats, returning -1 if the first format
  // is lower cost, 0 if they're equal cost or unknown, and 1 if the first
  // format is higher cost.
  //
  // Passing in pdev_device_info_vid and pdev_device_info_pid allows the
  // implementation to depend on the platform bus driver device VID and PID.
  //
  // By passing in the BufferCollectionConstraints, the implementation can
  // consider other aspects of constraints in addition to the usage.
  static int32_t Compare(
      uint32_t pdev_device_info_vid, uint32_t pdev_device_info_pid,
      const llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder& constraints,
      uint32_t image_format_constraints_index_a, uint32_t image_format_constraints_index_b);

 private:
  // For now the implementation is via a static table.
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_USAGE_PIXEL_FORMAT_COST_H_
