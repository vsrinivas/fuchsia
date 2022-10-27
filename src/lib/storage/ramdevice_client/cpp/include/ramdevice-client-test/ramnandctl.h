// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_LIB_STORAGE_RAMDEVICE_CLIENT_CPP_INCLUDE_RAMDEVICE_CLIENT_TEST_RAMNANDCTL_H_
#define SRC_LIB_STORAGE_RAMDEVICE_CLIENT_CPP_INCLUDE_RAMDEVICE_CLIENT_TEST_RAMNANDCTL_H_

#include <lib/driver-integration-test/fixture.h>

#include "src/lib/storage/ramdevice_client/cpp/include/ramdevice-client/ramnand.h"

namespace ramdevice_client_test {

class RamNandCtl {
 public:
  // Creates an isolated devmgr and spawns a ram_nand_ctl device in it.
  static zx_status_t Create(std::unique_ptr<RamNandCtl>* out);

  static zx_status_t CreateWithRamNand(fuchsia_hardware_nand::wire::RamNandInfo config,
                                       std::optional<ramdevice_client::RamNand>* out);

  zx_status_t CreateRamNand(fuchsia_hardware_nand::wire::RamNandInfo config,
                            std::optional<ramdevice_client::RamNand>* out) const;

  ~RamNandCtl() = default;

  const fidl::ClientEnd<fuchsia_hardware_nand::RamNandCtl>& ctl() const { return ctl_; }

  const fbl::unique_fd& devfs_root() const { return devmgr_.devfs_root(); }

 private:
  RamNandCtl(driver_integration_test::IsolatedDevmgr devmgr,
             fidl::ClientEnd<fuchsia_hardware_nand::RamNandCtl> ctl)
      : devmgr_(std::move(devmgr)), ctl_(std::move(ctl)) {}

  driver_integration_test::IsolatedDevmgr devmgr_;
  const fidl::ClientEnd<fuchsia_hardware_nand::RamNandCtl> ctl_;
};

}  // namespace ramdevice_client_test

#endif  // SRC_LIB_STORAGE_RAMDEVICE_CLIENT_CPP_INCLUDE_RAMDEVICE_CLIENT_TEST_RAMNANDCTL_H_
