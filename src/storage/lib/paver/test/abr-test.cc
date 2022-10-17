// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/abr/abr.h"

#include <endian.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/cksum.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/sys/component/cpp/service_client.h>
#include <zircon/hw/gpt.h>

#include <iostream>

#include <mock-boot-arguments/server.h>
#include <zxtest/zxtest.h>

#include "gpt/cros.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/uuid/uuid.h"
#include "src/storage/lib/paver/abr-client-vboot.h"
#include "src/storage/lib/paver/abr-client.h"
#include "src/storage/lib/paver/astro.h"
#include "src/storage/lib/paver/chromebook-x64.h"
#include "src/storage/lib/paver/luis.h"
#include "src/storage/lib/paver/sherlock.h"
#include "src/storage/lib/paver/test/test-utils.h"
#include "src/storage/lib/paver/utils.h"
#include "src/storage/lib/paver/x64.h"

namespace {

using device_watcher::RecursiveWaitForFile;
using driver_integration_test::IsolatedDevmgr;
using paver::BlockWatcherPauser;

TEST(AstroAbrTests, CreateFails) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.disable_block_watcher = false;
  args.board_name = "sherlock";

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  fidl::ClientEnd<fuchsia_io::Directory> svc_root;
  ASSERT_NOT_OK(
      paver::AstroAbrClientFactory().New(devmgr.devfs_root().duplicate(), svc_root, nullptr));
}

TEST(SherlockAbrTests, CreateFails) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.disable_block_watcher = false;
  args.board_name = "astro";

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  ASSERT_NOT_OK(paver::SherlockAbrClientFactory().Create(devmgr.devfs_root().duplicate(),
                                                         devmgr.fshost_svc_dir(), nullptr));
}

TEST(LuisAbrTests, CreateFails) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.disable_block_watcher = false;
  args.board_name = "astro";

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  ASSERT_NOT_OK(paver::LuisAbrClientFactory().Create(devmgr.devfs_root().duplicate(),
                                                     devmgr.fshost_svc_dir(), nullptr));
}

TEST(X64AbrTests, CreateFails) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.disable_block_watcher = false;
  args.board_name = "x64";

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  ASSERT_NOT_OK(paver::X64AbrClientFactory().Create(devmgr.devfs_root().duplicate(),
                                                    devmgr.fshost_svc_dir(), nullptr));
}

class ChromebookX64AbrTests : public zxtest::Test {
  static constexpr int kBlockSize = 512;
  static constexpr size_t kKibibyte = 1024;
  static constexpr size_t kMebibyte = kKibibyte * 1024;
  static constexpr size_t kGibibyte = kMebibyte * 1024;
  static constexpr uint64_t kZxPartBlocks = 64 * kMebibyte / kBlockSize;
  static constexpr uint64_t kMinFvmSize = 16 * kGibibyte;
  // we need at least 3 * SZ_ZX_PART for zircon a/b/r, and kMinFvmSize for fvm.
  static constexpr uint64_t kDiskBlocks = 4 * kZxPartBlocks + kMinFvmSize;
  static constexpr uint8_t kEmptyType[GPT_GUID_LEN] = GUID_EMPTY_VALUE;
  static constexpr uint8_t kZirconType[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
  static constexpr uint8_t kFvmType[GPT_GUID_LEN] = GPT_FVM_TYPE_GUID;

 protected:
  ChromebookX64AbrTests()
      : dispatcher_(&kAsyncLoopConfigNoAttachToCurrentThread),
        dispatcher2_(&kAsyncLoopConfigAttachToCurrentThread),
        fake_svc_(dispatcher_.dispatcher(), mock_boot_arguments::Server()) {
    IsolatedDevmgr::Args args;
    args.disable_block_watcher = false;
    args.board_name = "chromebook-x64";

    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));
    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform/00:00:2d/ramctl", &fd));
    ASSERT_NO_FATAL_FAILURE(
        BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kDiskBlocks, kBlockSize, &disk_));
    fake_svc_.fake_boot_args().GetArgumentsMap().emplace("zvb.current_slot", "_a");
    dispatcher_.StartThread("abr-svc-test-loop");
    dispatcher2_.StartThread("abr-svc-test-loop-2");
  }

  ~ChromebookX64AbrTests() override { dispatcher_.Shutdown(); }

  fidl::ClientEnd<fuchsia_io::Directory> GetFshostSvcRoot() { return devmgr_.fshost_svc_dir(); }

  void SetupPartitions(AbrSlotIndex active_slot) {
    auto pauser = BlockWatcherPauser::Create(GetFshostSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_OK(gpt::GptDevice::Create(disk_->fd(), /*blocksize=*/disk_->block_size(),
                                     /*blocks=*/disk_->block_count(), &gpt));
    ASSERT_OK(gpt->Sync());
    // 2 (GPT header and MBR header) blocks + number of blocks in entry array.
    uint64_t cur_start = 2 + gpt->EntryArrayBlockCount();
    ASSERT_OK(gpt->AddPartition(GPT_ZIRCON_A_NAME, kZirconType, kZirconType, cur_start,
                                kZxPartBlocks, 0));
    cur_start += kZxPartBlocks;
    ASSERT_OK(gpt->AddPartition(GPT_ZIRCON_B_NAME, kZirconType, kZirconType, cur_start,
                                kZxPartBlocks, 0));
    cur_start += kZxPartBlocks;
    ASSERT_OK(gpt->AddPartition(GPT_ZIRCON_R_NAME, kZirconType, kZirconType, cur_start,
                                kZxPartBlocks, 0));
    cur_start += kZxPartBlocks;
    ASSERT_OK(gpt->AddPartition(GPT_FVM_NAME, kFvmType, kFvmType, cur_start, kMinFvmSize, 0));
    cur_start += kMinFvmSize;

    int active_partition = -1;
    auto current_slot = "_x";
    switch (active_slot) {
      case kAbrSlotIndexA:
        active_partition = 0;
        current_slot = "_a";
        break;
      case kAbrSlotIndexB:
        active_partition = 1;
        current_slot = "_b";
        break;
      case kAbrSlotIndexR:
        active_partition = 2;
        current_slot = "_r";
        break;
    }

    auto result = gpt->GetPartition(active_partition);
    ASSERT_OK(result.status_value());
    gpt_partition_t* part = *result;
    gpt_cros_attr_set_priority(&part->flags, 15);
    gpt_cros_attr_set_successful(&part->flags, true);
    fake_svc_.fake_boot_args().GetArgumentsMap().emplace("zvb.current_slot", current_slot);
    ASSERT_OK(gpt->Sync());

    fdio_cpp::UnownedFdioCaller caller(disk_->fd());
    auto result2 = fidl::WireCall<fuchsia_device::Controller>(caller.channel())
                       ->Rebind(fidl::StringView("gpt.so"));
    ASSERT_TRUE(result2.ok());
    ASSERT_FALSE(result2->is_error());
  }

  zx::result<std::unique_ptr<abr::Client>> GetAbrClient() {
    auto& svc_root = fake_svc_.svc_chan();
    return paver::ChromebookX64AbrClientFactory().New(devmgr_.devfs_root().duplicate(), svc_root,
                                                      nullptr);
  }

  gpt_partition_t* GetPartitionByName(std::unique_ptr<gpt::GptDevice>& gpt, const char* name) {
    gpt_partition_t* part = nullptr;
    uint16_t name_utf16[sizeof(part->name) / sizeof(uint16_t)];
    memset(name_utf16, 0, sizeof(name_utf16));
    cstring_to_utf16(name_utf16, name, std::size(name_utf16));
    for (uint32_t i = 0; i < gpt->EntryCount(); i++) {
      auto ret = gpt->GetPartition(i);
      if (ret.is_error()) {
        continue;
      }

      if (!memcmp(ret.value()->name, name_utf16, sizeof(name_utf16))) {
        part = ret.value();
        break;
      }
    }
    return part;
  }

  std::unique_ptr<BlockDevice> disk_;
  IsolatedDevmgr devmgr_;
  async::Loop dispatcher_;
  async::Loop dispatcher2_;
  FakeSvc<mock_boot_arguments::Server> fake_svc_;
};

TEST_F(ChromebookX64AbrTests, CreateSucceeds) {
  ASSERT_NO_FATAL_FAILURE(SetupPartitions(kAbrSlotIndexA));
  auto client = GetAbrClient();
  ASSERT_OK(client.status_value());
}

TEST_F(ChromebookX64AbrTests, QueryActiveSucceeds) {
  ASSERT_NO_FATAL_FAILURE(SetupPartitions(kAbrSlotIndexA));
  auto client = GetAbrClient();
  ASSERT_OK(client.status_value());

  bool marked_successful;
  AbrSlotIndex slot = client->GetBootSlot(false, &marked_successful);
  ASSERT_EQ(slot, kAbrSlotIndexA);
  ASSERT_TRUE(marked_successful);
}

TEST_F(ChromebookX64AbrTests, GetSlotInfoSucceeds) {
  ASSERT_NO_FATAL_FAILURE(SetupPartitions(kAbrSlotIndexB));
  auto client = GetAbrClient();
  ASSERT_OK(client.status_value());
  auto info = client->GetSlotInfo(kAbrSlotIndexB);
  ASSERT_OK(info.status_value());
  ASSERT_TRUE(info->is_active);
  ASSERT_TRUE(info->is_bootable);
  ASSERT_TRUE(info->is_marked_successful);
  ASSERT_EQ(info->num_tries_remaining, 0);
}

TEST_F(ChromebookX64AbrTests, AbrAlwaysMarksRSuccessful) {
  ASSERT_NO_FATAL_FAILURE(SetupPartitions(kAbrSlotIndexA));
  auto client = GetAbrClient();
  ASSERT_OK(client.status_value());
  // Force a write to the A/B/R data by marking a slot successful and then marking it unbootable.
  ASSERT_OK(client->MarkSlotSuccessful(kAbrSlotIndexA).status_value());
  ASSERT_OK(client->Flush().status_value());
  ASSERT_OK(client->MarkSlotUnbootable(kAbrSlotIndexA).status_value());
  ASSERT_OK(client->Flush().status_value());

  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_OK(gpt::GptDevice::Create(disk_->fd(), /*blocksize=*/disk_->block_size(),
                                   /*blocks=*/disk_->block_count(), &gpt));
  gpt_partition_t* part = GetPartitionByName(gpt, GPT_ZIRCON_R_NAME);
  ASSERT_NE(part, nullptr);
  ASSERT_TRUE(gpt_cros_attr_get_successful(part->flags));
}

class CurrentSlotUuidTest : public zxtest::Test {
 protected:
  static constexpr int kBlockSize = 512;
  static constexpr int kDiskBlocks = 1024;
  static constexpr uint8_t kEmptyType[GPT_GUID_LEN] = GUID_EMPTY_VALUE;
  static constexpr uint8_t kZirconType[GPT_GUID_LEN] = GPT_ZIRCON_ABR_TYPE_GUID;
  static constexpr uint8_t kTestUuid[GPT_GUID_LEN] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
                                                      0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                                      0xcc, 0xdd, 0xee, 0xff};
  CurrentSlotUuidTest() {
    IsolatedDevmgr::Args args;
    args.disable_block_watcher = true;

    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));
    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform/00:00:2d/ramctl", &fd));
    ASSERT_NO_FATAL_FAILURE(
        BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kDiskBlocks, kBlockSize, &disk_));
  }

  void CreateDiskWithPartition(const char* partition) {
    ASSERT_OK(gpt::GptDevice::Create(disk_->fd(), /*blocksize=*/disk_->block_size(),
                                     /*blocks=*/disk_->block_count(), &gpt_));
    ASSERT_OK(gpt_->Sync());
    ASSERT_OK(gpt_->AddPartition(partition, kZirconType, kTestUuid,
                                 2 + gpt_->EntryArrayBlockCount(), 10, 0));
    ASSERT_OK(gpt_->Sync());

    fdio_cpp::UnownedFdioCaller caller(disk_->fd());
    auto result = fidl::WireCall<fuchsia_device::Controller>(caller.channel())
                      ->Rebind(fidl::StringView("gpt.so"));
    ASSERT_TRUE(result.ok());
    ASSERT_FALSE(result->is_error());
  }

  fidl::ClientEnd<fuchsia_io::Directory> GetSvcRoot() { return devmgr_.fshost_svc_dir(); }

  IsolatedDevmgr devmgr_;
  std::unique_ptr<BlockDevice> disk_;
  std::unique_ptr<gpt::GptDevice> gpt_;
};

TEST_F(CurrentSlotUuidTest, TestZirconAIsSlotA) {
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithPartition("zircon-a"));

  auto result = abr::PartitionUuidToConfiguration(devmgr_.devfs_root(), uuid::Uuid(kTestUuid));
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(result.value(), fuchsia_paver::wire::Configuration::kA);
}

TEST_F(CurrentSlotUuidTest, TestZirconAWithUnderscore) {
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithPartition("zircon_a"));

  auto result = abr::PartitionUuidToConfiguration(devmgr_.devfs_root(), uuid::Uuid(kTestUuid));
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(result.value(), fuchsia_paver::wire::Configuration::kA);
}

TEST_F(CurrentSlotUuidTest, TestZirconAMixedCase) {
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithPartition("ZiRcOn-A"));

  auto result = abr::PartitionUuidToConfiguration(devmgr_.devfs_root(), uuid::Uuid(kTestUuid));
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(result.value(), fuchsia_paver::wire::Configuration::kA);
}

TEST_F(CurrentSlotUuidTest, TestZirconB) {
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithPartition("zircon_b"));

  auto result = abr::PartitionUuidToConfiguration(devmgr_.devfs_root(), uuid::Uuid(kTestUuid));
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(result.value(), fuchsia_paver::wire::Configuration::kB);
}

TEST_F(CurrentSlotUuidTest, TestZirconR) {
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithPartition("ZIRCON-R"));

  auto result = abr::PartitionUuidToConfiguration(devmgr_.devfs_root(), uuid::Uuid(kTestUuid));
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(result.value(), fuchsia_paver::wire::Configuration::kRecovery);
}

TEST_F(CurrentSlotUuidTest, TestInvalid) {
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithPartition("ZERCON-R"));

  auto result = abr::PartitionUuidToConfiguration(devmgr_.devfs_root(), uuid::Uuid(kTestUuid));
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error_value(), ZX_ERR_NOT_SUPPORTED);
}

TEST(CurrentSlotTest, TestA) {
  auto result = abr::CurrentSlotToConfiguration("_a");
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(result.value(), fuchsia_paver::wire::Configuration::kA);
}

TEST(CurrentSlotTest, TestB) {
  auto result = abr::CurrentSlotToConfiguration("_b");
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(result.value(), fuchsia_paver::wire::Configuration::kB);
}

TEST(CurrentSlotTest, TestR) {
  auto result = abr::CurrentSlotToConfiguration("_r");
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(result.value(), fuchsia_paver::wire::Configuration::kRecovery);
}

TEST(CurrentSlotTest, TestInvalid) {
  auto result = abr::CurrentSlotToConfiguration("_x");
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error_value(), ZX_ERR_NOT_SUPPORTED);
}

}  // namespace
