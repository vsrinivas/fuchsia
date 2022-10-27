// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <utility>

#include <fbl/string_buffer.h>
#include <zxtest/zxtest.h>

#include "src/storage/fvm/format.h"
#include "src/storage/fvm/test_support.h"

namespace fvm {
namespace {

constexpr uint64_t kBlockSize = 512;
constexpr uint64_t kSliceSize = 1 << 20;

using driver_integration_test::IsolatedDevmgr;

class FvmVPartitionLoadTest : public zxtest::Test {
 public:
  static void SetUpTestSuite() {
    IsolatedDevmgr::Args args;
    args.disable_block_watcher = true;

    devmgr_ = std::make_unique<IsolatedDevmgr>();
    ASSERT_OK(IsolatedDevmgr::Create(&args, devmgr_.get()));
  }

  static void TearDownTestSuite() { devmgr_.reset(); }

 protected:
  static std::unique_ptr<IsolatedDevmgr> devmgr_;
};

std::unique_ptr<IsolatedDevmgr> FvmVPartitionLoadTest::devmgr_ = nullptr;

TEST_F(FvmVPartitionLoadTest, LoadPartitionWithPlaceHolderGuidIsUpdated) {
  constexpr uint64_t kBlockCount = (50 * kSliceSize) / kBlockSize;

  std::unique_ptr<RamdiskRef> ramdisk =
      RamdiskRef::Create(devmgr_->devfs_root(), kBlockSize, kBlockCount);
  ASSERT_TRUE(ramdisk);

  std::unique_ptr<FvmAdapter> fvm =
      FvmAdapter::Create(devmgr_->devfs_root(), kBlockSize, kBlockCount, kSliceSize, ramdisk.get());
  ASSERT_TRUE(fvm);

  std::unique_ptr<VPartitionAdapter> vpartition = nullptr;
  ASSERT_OK(fvm->AddPartition(devmgr_->devfs_root(), "test-partition",
                              static_cast<fvm::Guid>(fvm::kPlaceHolderInstanceGuid.data()),
                              static_cast<fvm::Guid>(fvm::kPlaceHolderInstanceGuid.data()), 1,
                              &vpartition));
  // Get the device topological path
  fdio_cpp::UnownedFdioCaller caller(vpartition->fd());
  auto topo_result =
      fidl::WireCall(caller.borrow_as<fuchsia_device::Controller>())->GetTopologicalPath();
  ASSERT_TRUE(topo_result.ok());
  ASSERT_TRUE(topo_result->is_ok());
  auto partition_path = std::string(topo_result->value()->path.begin() + strlen("/dev/"),
                                    topo_result->value()->path.end());
  std::vector<uint8_t> partition_guid(kGuidSize, 0);
  {
    // After rebind the instance guid should not be kPlaceHolderGUID.
    ASSERT_OK(fvm->Rebind({}));

    fbl::unique_fd fvmfd;
    device_watcher::RecursiveWaitForFile(devmgr_->devfs_root(), partition_path.c_str(), &fvmfd);

    fdio_cpp::UnownedFdioCaller caller(fvmfd.get());
    auto result = fidl::WireCall(caller.borrow_as<fuchsia_hardware_block_partition::Partition>())
                      ->GetInstanceGuid();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    EXPECT_FALSE(memcmp(result.value().guid.get(), kPlaceHolderInstanceGuid.data(),
                        kPlaceHolderInstanceGuid.size()) == 0);
    memcpy(partition_guid.data(), result.value().guid.get(), kGuidSize);
  }
  {
    // One more time to check that the UUID persisted, so it doesn't change between 'reboot'.
    ASSERT_OK(fvm->Rebind({}));

    fbl::unique_fd fvmfd;
    device_watcher::RecursiveWaitForFile(devmgr_->devfs_root(), partition_path.c_str(), &fvmfd);

    fdio_cpp::UnownedFdioCaller caller(fvmfd.get());
    auto result = fidl::WireCall(caller.borrow_as<fuchsia_hardware_block_partition::Partition>())
                      ->GetInstanceGuid();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    EXPECT_TRUE(memcmp(result.value().guid.get(), partition_guid.data(),
                       kPlaceHolderInstanceGuid.size()) == 0);
  }
}

}  // namespace
}  // namespace fvm
