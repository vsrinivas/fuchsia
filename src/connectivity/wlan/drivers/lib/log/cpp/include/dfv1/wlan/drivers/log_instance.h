// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_DFV1_WLAN_DRIVERS_LOG_INSTANCE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_DFV1_WLAN_DRIVERS_LOG_INSTANCE_H_

#include <stdint.h>

namespace wlan::drivers::log {

class Instance {
 public:
  static void Init(uint32_t filter);
  static bool IsFilterOn(uint32_t filter);

 private:
  static Instance& get();
  uint32_t filter_;
};

}  // namespace wlan::drivers::log

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_DFV1_WLAN_DRIVERS_LOG_INSTANCE_H_
