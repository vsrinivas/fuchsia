// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/partition/llcpp/fidl.h>
#include <fuchsia/hardware/block/volume/llcpp/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <utility>

#include <fbl/string_buffer.h>
#include <fs-management/fvm.h>
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
  static void SetUpTestCase() {
    IsolatedDevmgr::Args args;
    args.disable_block_watcher = true;
    args.load_drivers.push_back("/boot/driver/platform-bus.so");
    args.driver_search_paths.push_back("/boot/driver");

    devmgr_ = std::make_unique<IsolatedDevmgr>();
    ASSERT_OK(IsolatedDevmgr::Create(&args, devmgr_.get()));
  }

  static void TearDownTestCase() { devmgr_.reset(); }

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
  auto topo_result =
      fidl::WireCall<fuchsia_device::Controller>(vpartition->channel()).GetTopologicalPath();
  ASSERT_TRUE(topo_result.ok());
  ASSERT_TRUE(topo_result->result.is_response());
  auto partition_path = std::string(topo_result->result.response().path.begin() + strlen("/dev/"),
                                    topo_result->result.response().path.end());
  std::vector<uint8_t> partition_guid(kGuidSize, 0);
  {
    // After rebind the instance guid should not be kPlaceHolderGUID.
    ASSERT_OK(fvm->Rebind({}));

    fbl::unique_fd fvmfd;
    devmgr_integration_test::RecursiveWaitForFile(devmgr_->devfs_root(), partition_path.c_str(),
                                                  &fvmfd);

    fdio_cpp::UnownedFdioCaller caller(fvmfd.get());
    auto result =
        fidl::WireCall<fuchsia_hardware_block_partition::Partition>(caller.channel()->borrow())
            .GetInstanceGuid();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    EXPECT_FALSE(memcmp(result->guid.get(), kPlaceHolderInstanceGuid.data(),
                        kPlaceHolderInstanceGuid.size()) == 0);
    memcpy(partition_guid.data(), result->guid.get(), kGuidSize);
  }
  {
    // One more time to check that the UUID persisted, so it doesn't change between 'reboot'.
    ASSERT_OK(fvm->Rebind({}));

    fbl::unique_fd fvmfd;
    devmgr_integration_test::RecursiveWaitForFile(devmgr_->devfs_root(), partition_path.c_str(),
                                                  &fvmfd);

    fdio_cpp::UnownedFdioCaller caller(fvmfd.get());
    auto result =
        fidl::WireCall<fuchsia_hardware_block_partition::Partition>(caller.channel()->borrow())
            .GetInstanceGuid();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    EXPECT_TRUE(
        memcmp(result->guid.get(), partition_guid.data(), kPlaceHolderInstanceGuid.size()) == 0);
  }
}

}  // namespace
}  // namespace fvm
