// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <ramdevice-client/ramnand.h>
#include <zxtest/zxtest.h>

// The path for the block device under test.
constexpr char kTestDevice[] = "/fake/dev/sys/platform/00:00:2e/nand-ctl/ram-nand-0/ftl/block";

// Performs process-wide setup for the integration test.
class FtlTestObserver {
 public:
  FtlTestObserver();
  ~FtlTestObserver() {}

  void OnProgramStart();

  // Returns true if the setup was successful.
  explicit operator bool() const { return ok_; }

 private:
  void CreateDevice();
  zx_status_t WaitForBlockDevice();

  const fbl::unique_fd& devfs_root() { return (*ram_nand_ctl_)->devfs_root(); }

  std::optional<fbl::RefPtr<ramdevice_client::RamNandCtl>> ram_nand_ctl_;
  std::optional<ramdevice_client::RamNand> ram_nand_;
  bool ok_ = false;
};
