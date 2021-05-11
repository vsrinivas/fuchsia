// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_TLV_FW_BUILDER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_TLV_FW_BUILDER_H_

#include <stdint.h>

#include <string>

namespace wlan::testing {

// This class builds a TLV-format firmware binary for testing use with the iwlwifi chipset.
class TlvFwBuilder {
 public:
  TlvFwBuilder();
  ~TlvFwBuilder();

  // Add a type-value pair to the firmware binary.
  void AddValue(uint32_t type, const void* data, size_t size);

  // Get the firmware binary.
  std::string GetBinary() const;

 private:
  std::string binary_;
};

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_TLV_FW_BUILDER_H_
