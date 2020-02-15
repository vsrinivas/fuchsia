// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_NVM_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_NVM_H_

#include <zircon/status.h>
#include <zircon/syscalls.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
}
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim.h"

using std::vector;

namespace wlan::testing {

typedef vector<uint8_t> ByteArray;

// This structure is used to simulate the data stored in firmware, which is accessed by
// NVM_ACCESS_CMD. See 'iwl_nvm_access_cmd' for command structure.
//
struct NvmSection {
  uint8_t target;  // enum iwl_nvm_access_target
  uint16_t type;   // enum iwl_nvm_section_type
  std::vector<uint8_t> data;
};

// A sub-module of simulated MVM firmware that simulates NVM behavior.
//
class SimNvm {
 public:
  explicit SimNvm() {}
  ~SimNvm() {}

  // Handle the NVM_ACCESS_CMD host command.
  //
  // Args:
  //   cmd: the input command.
  //   [out] resp: the response back to caller.
  //
  zx_status_t HandleCommand(struct iwl_host_cmd* cmd, SimMvmResponse* resp);

 private:
  // Read a chunk from a segment (aka type).
  //
  // Args:
  //   target: enum iwl_nvm_access_target
  //   type: enum iwl_nvm_section_type, aka section in driver.
  //   offset: starting offset to read
  //   length: num of bytes to read
  //
  ByteArray HandleChunkRead(uint8_t target, uint16_t type, uint16_t offset, uint16_t length);
};

extern std::vector<NvmSection> default_sections;

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_NVM_H_
