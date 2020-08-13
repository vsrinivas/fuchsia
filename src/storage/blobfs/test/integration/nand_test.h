// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TEST_INTEGRATION_NAND_TEST_H_
#define SRC_STORAGE_BLOBFS_TEST_INTEGRATION_NAND_TEST_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <blobfs/mount.h>
#include <fbl/unique_fd.h>
#include <ramdevice-client/ramnand.h>
#include <zxtest/zxtest.h>

#include "blobfs.h"
#include "runner.h"

namespace blobfs {

// Test harness that sets up a blobfs backed by a mock raw NAND device held in memory.
class NandTest : public zxtest::Test {
 public:
  struct Connection {
    // Makes a new connection. The dev root should be something like "/something/dev".
    //
    // The vmo can be null which will make the ram-nand create its own backing store. If non-null,
    // the VMO memory (of at least GetVMOSize()) should be filled with 0xff for a new empty device.
    //
    // create_filesystem controls whether a new filesystem is initialized in the device. If unset,
    // the VMO should already contain a filesystem.
    Connection(const char* dev_root, zx::vmo vmo, bool create_filesystem);

    ~Connection();

    // Returns the size required for the VMO passed to the block device given the NAND device
    // parameters we're using.
    static size_t GetVMOSize();

    int root_fd() const { return root_fd_.get(); }

   private:
    std::unique_ptr<async::Loop> loop_;

    fbl::RefPtr<ramdevice_client::RamNandCtl> ram_nand_ctl_;
    std::optional<ramdevice_client::RamNand> ram_nand_;

    block_client::BlockDevice* block_device_ = nullptr;  // Owned by the runner_.

    fbl::unique_fd root_fd_;
    std::unique_ptr<blobfs::Runner> runner_;
  };

  void SetUp() override;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TEST_INTEGRATION_NAND_TEST_H_
