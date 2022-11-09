// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <fcntl.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/abr/data.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/cksum.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/paver/provider.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/sysconfig/sync-client.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/image.h>
#include <zircon/hw/gpt.h>

#include <memory>
#include <optional>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <soc/aml-common/aml-guid.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/storage/lib/paver/fvm.h"
#include "src/storage/lib/paver/gpt.h"
#include "src/storage/lib/paver/paver.h"
#include "src/storage/lib/paver/test/test-utils.h"
#include "src/storage/lib/paver/utils.h"
#include "src/storage/lib/utils/topological_path.h"

namespace {

namespace partition = fuchsia_hardware_block_partition;

using device_watcher::RecursiveWaitForFile;
using driver_integration_test::IsolatedDevmgr;

constexpr std::string_view kFirmwareTypeBootloader;
constexpr std::string_view kFirmwareTypeBl2 = "bl2";
constexpr std::string_view kFirmwareTypeUnsupported = "unsupported_type";

// BL2 images must be exactly this size.
constexpr size_t kBl2ImageSize = 0x10000;
// Make sure we can use our page-based APIs to work with the BL2 image.
static_assert(kBl2ImageSize % kPageSize == 0);
constexpr size_t kBl2ImagePages = kBl2ImageSize / kPageSize;

constexpr uint32_t kBootloaderFirstBlock = 4;
constexpr uint32_t kBootloaderBlocks = 4;
constexpr uint32_t kBootloaderLastBlock = kBootloaderFirstBlock + kBootloaderBlocks - 1;
constexpr uint32_t kBl2FirstBlock = kNumBlocks - 1;
constexpr uint32_t kFvmFirstBlock = 18;

fuchsia_hardware_nand::wire::RamNandInfo NandInfo() {
  return {
      .nand_info =
          {
              .page_size = kPageSize,
              .pages_per_block = kPagesPerBlock,
              .num_blocks = kNumBlocks,
              .ecc_bits = 8,
              .oob_size = kOobSize,
              .nand_class = fuchsia_hardware_nand::wire::Class::kPartmap,
              .partition_guid = {},
          },
      .partition_map =
          {
              .device_guid = {},
              .partition_count = 8,
              .partitions =
                  {
                      fuchsia_hardware_nand::wire::Partition{
                          .type_guid = {},
                          .unique_guid = {},
                          .first_block = 0,
                          .last_block = 3,
                          .copy_count = 0,
                          .copy_byte_offset = 0,
                          .name = {},
                          .hidden = true,
                          .bbt = true,
                      },
                      {
                          .type_guid = GUID_BOOTLOADER_VALUE,
                          .unique_guid = {},
                          .first_block = kBootloaderFirstBlock,
                          .last_block = kBootloaderLastBlock,
                          .copy_count = 0,
                          .copy_byte_offset = 0,
                          .name = {'b', 'o', 'o', 't', 'l', 'o', 'a', 'd', 'e', 'r'},
                          .hidden = false,
                          .bbt = false,
                      },
                      {
                          .type_guid = GUID_ZIRCON_A_VALUE,
                          .unique_guid = {},
                          .first_block = kBootloaderLastBlock + 1,
                          .last_block = 9,
                          .copy_count = 0,
                          .copy_byte_offset = 0,
                          .name = {'z', 'i', 'r', 'c', 'o', 'n', '-', 'a'},
                          .hidden = false,
                          .bbt = false,
                      },
                      {
                          .type_guid = GUID_ZIRCON_B_VALUE,
                          .unique_guid = {},
                          .first_block = 10,
                          .last_block = 11,
                          .copy_count = 0,
                          .copy_byte_offset = 0,
                          .name = {'z', 'i', 'r', 'c', 'o', 'n', '-', 'b'},
                          .hidden = false,
                          .bbt = false,
                      },
                      {
                          .type_guid = GUID_ZIRCON_R_VALUE,
                          .unique_guid = {},
                          .first_block = 12,
                          .last_block = 13,
                          .copy_count = 0,
                          .copy_byte_offset = 0,
                          .name = {'z', 'i', 'r', 'c', 'o', 'n', '-', 'r'},
                          .hidden = false,
                          .bbt = false,
                      },
                      {
                          .type_guid = GUID_SYS_CONFIG_VALUE,
                          .unique_guid = {},
                          .first_block = 14,
                          .last_block = 17,
                          .copy_count = 0,
                          .copy_byte_offset = 0,
                          .name = {'s', 'y', 's', 'c', 'o', 'n', 'f', 'i', 'g'},
                          .hidden = false,
                          .bbt = false,
                      },
                      {
                          .type_guid = GUID_FVM_VALUE,
                          .unique_guid = {},
                          .first_block = kFvmFirstBlock,
                          .last_block = kBl2FirstBlock - 1,
                          .copy_count = 0,
                          .copy_byte_offset = 0,
                          .name = {'f', 'v', 'm'},
                          .hidden = false,
                          .bbt = false,
                      },
                      {
                          .type_guid = GUID_BL2_VALUE,
                          .unique_guid = {},
                          .first_block = kBl2FirstBlock,
                          .last_block = kBl2FirstBlock,
                          .copy_count = 0,
                          .copy_byte_offset = 0,
                          .name =
                              {
                                  'b',
                                  'l',
                                  '2',
                              },
                          .hidden = false,
                          .bbt = false,
                      },
                  },
          },
      .export_nand_config = true,
      .export_partition_map = true,
  };
}

class FakeBootArgs : public fidl::WireServer<fuchsia_boot::Arguments> {
 public:
  void GetString(GetStringRequestView request, GetStringCompleter::Sync& completer) override {}

  // Stubs
  void GetStrings(GetStringsRequestView request, GetStringsCompleter::Sync& completer) override {
    std::vector<fidl::StringView> response = {
        fidl::StringView::FromExternal(arg_response_),
        fidl::StringView(),
    };
    completer.Reply(fidl::VectorView<fidl::StringView>::FromExternal(response));
  }
  void GetBool(GetBoolRequestView request, GetBoolCompleter::Sync& completer) override {
    if (strncmp(request->key.data(), "astro.sysconfig.abr-wear-leveling",
                sizeof("astro.sysconfig.abr-wear-leveling")) == 0) {
      completer.Reply(astro_sysconfig_abr_wear_leveling_);
    } else {
      completer.Reply(request->defaultval);
    }
  }
  void GetBools(GetBoolsRequestView request, GetBoolsCompleter::Sync& completer) override {}
  void Collect(CollectRequestView request, CollectCompleter::Sync& completer) override {}

  void SetAstroSysConfigAbrWearLeveling(bool opt) { astro_sysconfig_abr_wear_leveling_ = opt; }

  void SetArgResponse(std::string arg_response) { arg_response_ = std::move(arg_response); }

 private:
  bool astro_sysconfig_abr_wear_leveling_ = false;
  std::string arg_response_ = "-a";
};

class PaverServiceTest : public zxtest::Test {
 public:
  PaverServiceTest();

  ~PaverServiceTest() override;

 protected:
  static void CreatePayload(size_t num_pages, fuchsia_mem::wire::Buffer* out);

  static constexpr size_t kKilobyte = 1 << 10;

  static void ValidateWritten(const fuchsia_mem::wire::Buffer& buf, size_t num_pages) {
    ASSERT_GE(buf.size, num_pages * kPageSize);
    fzl::VmoMapper mapper;
    ASSERT_OK(mapper.Map(buf.vmo, 0,
                         fbl::round_up(num_pages * kPageSize, zx_system_get_page_size()),
                         ZX_VM_PERM_READ));
    const uint8_t* start = reinterpret_cast<uint8_t*>(mapper.start());
    for (size_t i = 0; i < num_pages * kPageSize; i++) {
      ASSERT_EQ(start[i], 0x4a, "i = %zu", i);
    }
  }

  void* provider_ctx_ = nullptr;
  fidl::WireSyncClient<fuchsia_paver::Paver> client_;
  async::Loop loop_;
  // The paver makes synchronous calls into /svc, so it must run in a separate loop to not
  // deadlock.
  async::Loop loop2_;
  FakeSvc<FakeBootArgs> fake_svc_;
};

PaverServiceTest::PaverServiceTest()
    : loop_(&kAsyncLoopConfigAttachToCurrentThread),
      loop2_(&kAsyncLoopConfigNoAttachToCurrentThread),
      fake_svc_(loop2_.dispatcher(), FakeBootArgs()) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_paver::Paver>();
  ASSERT_OK(endpoints.status_value());
  auto& [client, server] = endpoints.value();

  client_ = fidl::WireSyncClient(std::move(client));

  ASSERT_OK(paver_get_service_provider()->ops->init(&provider_ctx_));

  ASSERT_OK(paver_get_service_provider()->ops->connect(
      provider_ctx_, loop_.dispatcher(), fidl::DiscoverableProtocolName<fuchsia_paver::Paver>,
      server.TakeChannel().release()));
  loop_.StartThread("paver-svc-test-loop");
  loop2_.StartThread("paver-svc-test-loop-2");
}

PaverServiceTest::~PaverServiceTest() {
  loop_.Shutdown();
  loop2_.Shutdown();
  paver_get_service_provider()->ops->release(provider_ctx_);
  provider_ctx_ = nullptr;
}

void PaverServiceTest::CreatePayload(size_t num_pages, fuchsia_mem::wire::Buffer* out) {
  zx::vmo vmo;
  fzl::VmoMapper mapper;
  const size_t size = kPageSize * num_pages;
  ASSERT_OK(mapper.CreateAndMap(size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
  memset(mapper.start(), 0x4a, mapper.size());
  out->vmo = std::move(vmo);
  out->size = size;
}

class PaverServiceSkipBlockTest : public PaverServiceTest {
 public:
  // Initializes the RAM NAND device.
  void InitializeRamNand(fuchsia_hardware_nand::wire::RamNandInfo nand_info = NandInfo()) {
    ASSERT_NO_FATAL_FAILURE(SpawnIsolatedDevmgr(std::move(nand_info)));
    ASSERT_NO_FATAL_FAILURE(WaitForDevices());
  }

 protected:
  void SpawnIsolatedDevmgr(fuchsia_hardware_nand::wire::RamNandInfo nand_info) {
    ASSERT_EQ(device_.get(), nullptr);
    ASSERT_NO_FATAL_FAILURE(SkipBlockDevice::Create(std::move(nand_info), &device_));
    static_cast<paver::Paver*>(provider_ctx_)->set_dispatcher(loop_.dispatcher());
    static_cast<paver::Paver*>(provider_ctx_)->set_devfs_root(device_->devfs_root());
    static_cast<paver::Paver*>(provider_ctx_)->set_svc_root(std::move(fake_svc_.svc_chan()));
  }

  void WaitForDevices() {
    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(device_->devfs_root(),
                                   "sys/platform/00:00:2e/nand-ctl/ram-nand-0/sysconfig/skip-block",
                                   &fd));
    ASSERT_OK(RecursiveWaitForFile(
        device_->devfs_root(), "sys/platform/00:00:2e/nand-ctl/ram-nand-0/fvm/ftl/block", &fvm_));
  }

  void FindBootManager() {
    zx::result endpoints = fidl::CreateEndpoints<fuchsia_paver::BootManager>();
    ASSERT_OK(endpoints.status_value());
    auto& [local, remote] = endpoints.value();

    auto result = client_->FindBootManager(std::move(remote));
    ASSERT_OK(result.status());
    boot_manager_ = fidl::WireSyncClient(std::move(local));
  }

  void FindDataSink() {
    zx::result endpoints = fidl::CreateEndpoints<fuchsia_paver::DataSink>();
    ASSERT_OK(endpoints.status_value());
    auto& [local, remote] = endpoints.value();

    auto result = client_->FindDataSink(std::move(remote));
    ASSERT_OK(result.status());
    data_sink_ = fidl::WireSyncClient(std::move(local));
  }

  void FindSysconfig() {
    zx::result endpoints = fidl::CreateEndpoints<fuchsia_paver::Sysconfig>();
    ASSERT_OK(endpoints.status_value());
    auto& [local, remote] = endpoints.value();

    auto result = client_->FindSysconfig(std::move(remote));
    ASSERT_OK(result.status());
    sysconfig_ = fidl::WireSyncClient(std::move(local));
  }

  void SetAbr(const AbrData& data) {
    auto* buf = reinterpret_cast<uint8_t*>(device_->mapper().start()) +
                (static_cast<size_t>(14) * kSkipBlockSize) + (static_cast<size_t>(60) * kKilobyte);
    *reinterpret_cast<AbrData*>(buf) = data;
  }

  AbrData GetAbr() {
    auto* buf = reinterpret_cast<uint8_t*>(device_->mapper().start()) +
                (static_cast<size_t>(14) * kSkipBlockSize) + (static_cast<size_t>(60) * kKilobyte);
    return *reinterpret_cast<AbrData*>(buf);
  }

  // Equivalence of GetAbr() in the context of abr wear-leveling.
  // Since there can be multiple pages in abr sub-partition that may have valid abr data,
  // argument |copy_index| is used to read a specific one.
  AbrData GetAbrInWearLeveling(const sysconfig_header& header, size_t copy_index) {
    auto* buf = reinterpret_cast<uint8_t*>(device_->mapper().start()) +
                (static_cast<size_t>(14) * kSkipBlockSize) + header.abr_metadata.offset +
                copy_index * 4 * kKilobyte;
    AbrData ret;
    memcpy(&ret, buf, sizeof(ret));
    return ret;
  }

  using PaverServiceTest::ValidateWritten;

  // Checks that the device mapper contains |expected| at each byte in the given
  // range. Uses ASSERT_EQ() per-byte to give a helpful message on failure.
  void AssertContents(size_t offset, size_t length, uint8_t expected) {
    const uint8_t* contents = static_cast<uint8_t*>(device_->mapper().start()) + offset;
    for (size_t i = 0; i < length; i++) {
      ASSERT_EQ(expected, contents[i], "i = %zu", i);
    }
  }

  void ValidateWritten(uint32_t block, size_t num_blocks) {
    AssertContents(static_cast<size_t>(block) * kSkipBlockSize, num_blocks * kSkipBlockSize, 0x4A);
  }

  void ValidateUnwritten(uint32_t block, size_t num_blocks) {
    AssertContents(static_cast<size_t>(block) * kSkipBlockSize, num_blocks * kSkipBlockSize, 0xFF);
  }

  void ValidateWrittenPages(uint32_t page, size_t num_pages) {
    AssertContents(static_cast<size_t>(page) * kPageSize, num_pages * kPageSize, 0x4A);
  }

  void ValidateUnwrittenPages(uint32_t page, size_t num_pages) {
    AssertContents(static_cast<size_t>(page) * kPageSize, num_pages * kPageSize, 0xFF);
  }

  void ValidateWrittenBytes(size_t offset, size_t num_bytes) {
    AssertContents(offset, num_bytes, 0x4A);
  }

  void ValidateUnwrittenBytes(size_t offset, size_t num_bytes) {
    AssertContents(offset, num_bytes, 0xFF);
  }

  void WriteData(uint32_t page, size_t num_pages, uint8_t data) {
    WriteDataBytes(page * kPageSize, num_pages * kPageSize, data);
  }

  void WriteDataBytes(uint32_t start, size_t num_bytes, uint8_t data) {
    memset(static_cast<uint8_t*>(device_->mapper().start()) + start, data, num_bytes);
  }

  void WriteDataBytes(uint32_t start, void* data, size_t num_bytes) {
    memcpy(static_cast<uint8_t*>(device_->mapper().start()) + start, data, num_bytes);
  }

  void TestSysconfigWriteBufferedClient(uint32_t offset_in_pages, uint32_t sysconfig_pages);

  void TestSysconfigWipeBufferedClient(uint32_t offset_in_pages, uint32_t sysconfig_pages);

  void TestQueryConfigurationLastSetActive(fuchsia_paver::wire::Configuration this_slot,
                                           fuchsia_paver::wire::Configuration other_slot);

  fidl::WireSyncClient<fuchsia_paver::BootManager> boot_manager_;
  fidl::WireSyncClient<fuchsia_paver::DataSink> data_sink_;
  fidl::WireSyncClient<fuchsia_paver::Sysconfig> sysconfig_;

  std::unique_ptr<SkipBlockDevice> device_;
  fbl::unique_fd fvm_;
};

constexpr AbrData kAbrData = {
    .magic = {'\0', 'A', 'B', '0'},
    .version_major = kAbrMajorVersion,
    .version_minor = kAbrMinorVersion,
    .reserved1 = {},
    .slot_data =
        {
            {
                .priority = 0,
                .tries_remaining = 0,
                .successful_boot = 0,
                .reserved = {},
            },
            {
                .priority = 1,
                .tries_remaining = 0,
                .successful_boot = 1,
                .reserved = {},
            },
        },
    .one_shot_recovery_boot = 0,
    .reserved2 = {},
    .crc32 = {},
};

void ComputeCrc(AbrData* data) {
  data->crc32 = htobe32(crc32(0, reinterpret_cast<const uint8_t*>(data), offsetof(AbrData, crc32)));
}

TEST_F(PaverServiceSkipBlockTest, InitializeAbr) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  AbrData abr_data = {};
  memset(&abr_data, 0x3d, sizeof(abr_data));
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
}

TEST_F(PaverServiceSkipBlockTest, InitializeAbrAlreadyValid) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
}

TEST_F(PaverServiceSkipBlockTest, QueryActiveConfigurationInvalidAbr) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  AbrData abr_data = {};
  memset(&abr_data, 0x3d, sizeof(abr_data));
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
}

TEST_F(PaverServiceSkipBlockTest, QueryActiveConfigurationBothPriority0) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  AbrData abr_data = kAbrData;
  abr_data.slot_data[0].priority = 0;
  abr_data.slot_data[1].priority = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_error());
  ASSERT_STATUS(result->error_value(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PaverServiceSkipBlockTest, QueryActiveConfigurationSlotB) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_EQ(result->value()->configuration, fuchsia_paver::wire::Configuration::kB);
}

TEST_F(PaverServiceSkipBlockTest, QueryActiveConfigurationSlotA) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  AbrData abr_data = kAbrData;
  abr_data.slot_data[0].priority = 2;
  abr_data.slot_data[0].successful_boot = 1;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_EQ(result->value()->configuration, fuchsia_paver::wire::Configuration::kA);
}

void PaverServiceSkipBlockTest::TestQueryConfigurationLastSetActive(
    fuchsia_paver::wire::Configuration this_slot, fuchsia_paver::wire::Configuration other_slot) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  // Set both slots to the active state.
  {
    auto result = boot_manager_->SetConfigurationActive(other_slot);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  {
    auto result = boot_manager_->SetConfigurationActive(this_slot);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  // Marking the slot successful shall not change the result.
  {
    auto result = boot_manager_->SetConfigurationHealthy(this_slot);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);

    auto get_result = boot_manager_->QueryConfigurationLastSetActive();
    ASSERT_OK(get_result.status());
    ASSERT_TRUE(get_result->is_ok());
    ASSERT_EQ(get_result->value()->configuration, this_slot);
  }

  // Marking the slot unbootable shall not change the result.
  {
    auto result = boot_manager_->SetConfigurationUnbootable(this_slot);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);

    auto get_result = boot_manager_->QueryConfigurationLastSetActive();
    ASSERT_OK(get_result.status());
    ASSERT_TRUE(get_result->is_ok());
    ASSERT_EQ(get_result->value()->configuration, this_slot);
  }

  // Marking the other slot successful shall not change the result.
  {
    auto result = boot_manager_->SetConfigurationHealthy(other_slot);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);

    auto get_result = boot_manager_->QueryConfigurationLastSetActive();
    ASSERT_OK(get_result.status());
    ASSERT_TRUE(get_result->is_ok());
    ASSERT_EQ(get_result->value()->configuration, this_slot);
  }

  // Marking the other slot unbootable shall not change the result.
  {
    auto result = boot_manager_->SetConfigurationUnbootable(other_slot);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);

    auto get_result = boot_manager_->QueryConfigurationLastSetActive();
    ASSERT_OK(get_result.status());
    ASSERT_TRUE(get_result->is_ok());
    ASSERT_EQ(get_result->value()->configuration, this_slot);
  }

  // Marking the other slot active does change the result.
  {
    auto result = boot_manager_->SetConfigurationActive(other_slot);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);

    auto get_result = boot_manager_->QueryConfigurationLastSetActive();
    ASSERT_OK(get_result.status());
    ASSERT_TRUE(get_result->is_ok());
    ASSERT_EQ(get_result->value()->configuration, other_slot);
  }
}

TEST_F(PaverServiceSkipBlockTest, QueryConfigurationLastSetActiveSlotA) {
  TestQueryConfigurationLastSetActive(fuchsia_paver::wire::Configuration::kA,
                                      fuchsia_paver::wire::Configuration::kB);
}

TEST_F(PaverServiceSkipBlockTest, QueryConfigurationLastSetActiveSlotB) {
  TestQueryConfigurationLastSetActive(fuchsia_paver::wire::Configuration::kB,
                                      fuchsia_paver::wire::Configuration::kA);
}

TEST_F(PaverServiceSkipBlockTest, QueryCurrentConfigurationSlotA) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryCurrentConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_EQ(result->value()->configuration, fuchsia_paver::wire::Configuration::kA);
}

TEST_F(PaverServiceSkipBlockTest, QueryCurrentConfigurationSlotB) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  fake_svc_.fake_boot_args().SetArgResponse("-b");

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryCurrentConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_EQ(result->value()->configuration, fuchsia_paver::wire::Configuration::kB);
}

TEST_F(PaverServiceSkipBlockTest, QueryCurrentConfigurationSlotR) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  fake_svc_.fake_boot_args().SetArgResponse("-r");

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryCurrentConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_EQ(result->value()->configuration, fuchsia_paver::wire::Configuration::kRecovery);
}

TEST_F(PaverServiceSkipBlockTest, QueryCurrentConfigurationSlotInvalid) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  fake_svc_.fake_boot_args().SetArgResponse("");

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryCurrentConfiguration();
  ASSERT_STATUS(result, ZX_ERR_PEER_CLOSED);
}

TEST_F(PaverServiceSkipBlockTest, QueryConfigurationStatusHealthy) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  auto abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryConfigurationStatus(fuchsia_paver::wire::Configuration::kB);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_EQ(result->value()->status, fuchsia_paver::wire::ConfigurationStatus::kHealthy);
}

TEST_F(PaverServiceSkipBlockTest, QueryConfigurationStatusPending) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[1].successful_boot = 0;
  abr_data.slot_data[1].tries_remaining = 1;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryConfigurationStatus(fuchsia_paver::wire::Configuration::kB);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_EQ(result->value()->status, fuchsia_paver::wire::ConfigurationStatus::kPending);
}

TEST_F(PaverServiceSkipBlockTest, QueryConfigurationStatusUnbootable) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->QueryConfigurationStatus(fuchsia_paver::wire::Configuration::kA);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_EQ(result->value()->status, fuchsia_paver::wire::ConfigurationStatus::kUnbootable);
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationActive) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slot_data[0].priority = kAbrMaxPriority;
  abr_data.slot_data[0].tries_remaining = kAbrMaxTriesRemaining;
  abr_data.slot_data[0].successful_boot = 0;
  ComputeCrc(&abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  {
    auto result = boot_manager_->SetConfigurationActive(fuchsia_paver::wire::Configuration::kA);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationActiveRollover) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[1].priority = kAbrMaxPriority;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slot_data[1].priority = kAbrMaxPriority - 1;
  abr_data.slot_data[0].priority = kAbrMaxPriority;
  abr_data.slot_data[0].tries_remaining = kAbrMaxTriesRemaining;
  abr_data.slot_data[0].successful_boot = 0;
  ComputeCrc(&abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  {
    auto result = boot_manager_->SetConfigurationActive(fuchsia_paver::wire::Configuration::kA);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }
  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationUnbootableSlotA) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[0].priority = 2;
  abr_data.slot_data[0].tries_remaining = 3;
  abr_data.slot_data[0].successful_boot = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slot_data[0].tries_remaining = 0;
  abr_data.slot_data[0].successful_boot = 0;
  ComputeCrc(&abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  {
    auto result = boot_manager_->SetConfigurationUnbootable(fuchsia_paver::wire::Configuration::kA);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationUnbootableSlotB) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[1].tries_remaining = 3;
  abr_data.slot_data[1].successful_boot = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slot_data[1].tries_remaining = 0;
  abr_data.slot_data[1].successful_boot = 0;
  ComputeCrc(&abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  {
    auto result = boot_manager_->SetConfigurationUnbootable(fuchsia_paver::wire::Configuration::kB);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationHealthySlotA) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[0].priority = kAbrMaxPriority;
  abr_data.slot_data[0].tries_remaining = 0;
  abr_data.slot_data[0].successful_boot = 1;
  abr_data.slot_data[1].priority = 0;
  abr_data.slot_data[1].tries_remaining = 0;
  abr_data.slot_data[1].successful_boot = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  {
    auto result = boot_manager_->SetConfigurationHealthy(fuchsia_paver::wire::Configuration::kA);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationHealthySlotB) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ComputeCrc(&abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  {
    auto result = boot_manager_->SetConfigurationHealthy(fuchsia_paver::wire::Configuration::kB);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationHealthySlotR) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result =
      boot_manager_->SetConfigurationHealthy(fuchsia_paver::wire::Configuration::kRecovery);
  ASSERT_OK(result.status());
  ASSERT_EQ(result.value().status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationHealthyBothUnknown) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[0].priority = kAbrMaxPriority;
  abr_data.slot_data[0].tries_remaining = 3;
  abr_data.slot_data[0].successful_boot = 0;
  abr_data.slot_data[1].priority = kAbrMaxPriority - 1;
  abr_data.slot_data[1].tries_remaining = 3;
  abr_data.slot_data[1].successful_boot = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slot_data[0].tries_remaining = 0;
  abr_data.slot_data[0].successful_boot = 1;
  abr_data.slot_data[1].tries_remaining = kAbrMaxTriesRemaining;
  ComputeCrc(&abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  {
    auto result = boot_manager_->SetConfigurationHealthy(fuchsia_paver::wire::Configuration::kA);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationHealthyOtherHealthy) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[0].priority = kAbrMaxPriority - 1;
  abr_data.slot_data[0].tries_remaining = 0;
  abr_data.slot_data[0].successful_boot = 1;
  abr_data.slot_data[1].priority = kAbrMaxPriority;
  abr_data.slot_data[1].tries_remaining = 3;
  abr_data.slot_data[1].successful_boot = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slot_data[0].tries_remaining = kAbrMaxTriesRemaining;
  abr_data.slot_data[0].successful_boot = 0;
  abr_data.slot_data[1].tries_remaining = 0;
  abr_data.slot_data[1].successful_boot = 1;
  ComputeCrc(&abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  {
    auto result = boot_manager_->SetConfigurationHealthy(fuchsia_paver::wire::Configuration::kB);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetUnbootableConfigurationHealthy) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  auto result = boot_manager_->SetConfigurationHealthy(fuchsia_paver::wire::Configuration::kA);
  ASSERT_OK(result.status());
  ASSERT_EQ(result.value().status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PaverServiceSkipBlockTest, BootManagerBuffered) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  AbrData abr_data = kAbrData;
  // Successful slot b, active slot a. Like what happen after a reboot following an OTA.
  abr_data.slot_data[0].tries_remaining = 3;
  abr_data.slot_data[0].successful_boot = 0;
  abr_data.slot_data[0].priority = 1;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  {
    auto result = boot_manager_->QueryActiveConfiguration();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->is_ok());
    ASSERT_EQ(result->value()->configuration, fuchsia_paver::wire::Configuration::kA);
  }

  {
    auto result = boot_manager_->SetConfigurationHealthy(fuchsia_paver::wire::Configuration::kA);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  {
    auto result = boot_manager_->SetConfigurationUnbootable(fuchsia_paver::wire::Configuration::kB);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  // haven't flushed yet, storage shall stay the same.
  auto abr = GetAbr();
  ASSERT_BYTES_EQ(&abr, &abr_data, sizeof(abr));

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }

  abr_data.slot_data[0].tries_remaining = 0;
  abr_data.slot_data[0].successful_boot = 1;
  abr_data.slot_data[1].tries_remaining = 0;
  abr_data.slot_data[1].successful_boot = 0;
  ComputeCrc(&abr_data);

  abr = GetAbr();
  ASSERT_BYTES_EQ(&abr, &abr_data, sizeof(abr));
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetKernelConfigA) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  fuchsia_mem::wire::Buffer payload;
  CreatePayload(static_cast<size_t>(2) * kPagesPerBlock, &payload);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->WriteAsset(fuchsia_paver::wire::Configuration::kA,
                                       fuchsia_paver::wire::Asset::kKernel, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWritten(8, 2);
  ValidateUnwritten(10, 4);
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetKernelConfigB) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  fuchsia_mem::wire::Buffer payload;
  CreatePayload(static_cast<size_t>(2) * kPagesPerBlock, &payload);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->WriteAsset(fuchsia_paver::wire::Configuration::kB,
                                       fuchsia_paver::wire::Asset::kKernel, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateUnwritten(8, 2);
  ValidateWritten(10, 2);
  ValidateUnwritten(12, 2);
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetKernelConfigRecovery) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  fuchsia_mem::wire::Buffer payload;
  CreatePayload(static_cast<size_t>(2) * kPagesPerBlock, &payload);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->WriteAsset(fuchsia_paver::wire::Configuration::kRecovery,
                                       fuchsia_paver::wire::Asset::kKernel, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateUnwritten(8, 4);
  ValidateWritten(12, 2);
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetVbMetaConfigA) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  fuchsia_mem::wire::Buffer payload;
  CreatePayload(32, &payload);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result =
      data_sink_->WriteAsset(fuchsia_paver::wire::Configuration::kA,
                             fuchsia_paver::wire::Asset::kVerifiedBootMetadata, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);

  auto sync_result = data_sink_->Flush();
  ASSERT_OK(sync_result.status());
  ASSERT_OK(sync_result.value().status);

  ValidateWrittenPages(14 * kPagesPerBlock + 32, 32);
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetVbMetaConfigB) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  fuchsia_mem::wire::Buffer payload;
  CreatePayload(32, &payload);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result =
      data_sink_->WriteAsset(fuchsia_paver::wire::Configuration::kB,
                             fuchsia_paver::wire::Asset::kVerifiedBootMetadata, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);

  auto sync_result = data_sink_->Flush();
  ASSERT_OK(sync_result.status());
  ASSERT_OK(sync_result.value().status);

  ValidateWrittenPages(14 * kPagesPerBlock + 64, 32);
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetVbMetaConfigRecovery) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  fuchsia_mem::wire::Buffer payload;
  CreatePayload(32, &payload);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result =
      data_sink_->WriteAsset(fuchsia_paver::wire::Configuration::kRecovery,
                             fuchsia_paver::wire::Asset::kVerifiedBootMetadata, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);

  auto sync_result = data_sink_->Flush();
  ASSERT_OK(sync_result.status());
  ASSERT_OK(sync_result.value().status);

  ValidateWrittenPages(14 * kPagesPerBlock + 96, 32);
}

TEST_F(PaverServiceSkipBlockTest, AbrWearLevelingLayoutNotUpdated) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  // Enable write-caching + abr metadata wear-leveling
  fake_svc_.fake_boot_args().SetAstroSysConfigAbrWearLeveling(true);

  // Active slot b
  AbrData abr_data = kAbrData;
  abr_data.slot_data[0].tries_remaining = 3;
  abr_data.slot_data[0].successful_boot = 0;
  abr_data.slot_data[0].priority = 0;
  abr_data.slot_data[1].tries_remaining = 3;
  abr_data.slot_data[1].successful_boot = 0;
  abr_data.slot_data[1].priority = 1;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  // Layout will not be updated as A/B state does not meet requirement.
  // (one successful slot + one unbootable slot)
  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  {
    auto result = boot_manager_->QueryActiveConfiguration();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->is_ok());
    ASSERT_EQ(result->value()->configuration, fuchsia_paver::wire::Configuration::kB);
  }

  {
    auto result = boot_manager_->SetConfigurationHealthy(fuchsia_paver::wire::Configuration::kB);
    ASSERT_OK(result.status());
  }

  {
    // The query result will come from the cache as flushed is not called.
    // Validate that it is correct.
    auto result = boot_manager_->QueryActiveConfiguration();
    ASSERT_OK(result.status());
    ASSERT_EQ(result->value()->configuration, fuchsia_paver::wire::Configuration::kB);
  }

  {
    // Mark the old slot A as unbootable.
    auto set_unbootable_result =
        boot_manager_->SetConfigurationUnbootable(fuchsia_paver::wire::Configuration::kA);
    ASSERT_OK(set_unbootable_result.status());
  }

  // Haven't flushed yet. abr data in storage should stayed the same.
  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));

  {
    auto result_sync = boot_manager_->Flush();
    ASSERT_OK(result_sync.status());
    ASSERT_OK(result_sync.value().status);
  }

  // Expected result: unbootable slot a, successful active slot b
  abr_data.slot_data[0].tries_remaining = 0;
  abr_data.slot_data[0].successful_boot = 0;
  abr_data.slot_data[0].priority = 0;
  abr_data.slot_data[1].tries_remaining = 0;
  abr_data.slot_data[1].successful_boot = 1;
  abr_data.slot_data[1].priority = 1;
  ComputeCrc(&abr_data);

  // Validate that new abr data is flushed to memory.
  // Since layout is not updated, Abr metadata is expected to be at the traditional position
  // (16th page).
  actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

AbrData GetAbrWearlevelingSupportingLayout() {
  // Unbootable slot a, successful active slot b
  AbrData abr_data = kAbrData;
  abr_data.slot_data[0].tries_remaining = 0;
  abr_data.slot_data[0].successful_boot = 0;
  abr_data.slot_data[0].priority = 0;
  abr_data.slot_data[1].tries_remaining = 0;
  abr_data.slot_data[1].successful_boot = 1;
  abr_data.slot_data[1].priority = 1;
  ComputeCrc(&abr_data);
  return abr_data;
}

TEST_F(PaverServiceSkipBlockTest, AbrWearLevelingLayoutUpdated) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  // Enable write-caching + abr metadata wear-leveling
  fake_svc_.fake_boot_args().SetAstroSysConfigAbrWearLeveling(true);

  // Unbootable slot a, successful active slot b
  auto abr_data = GetAbrWearlevelingSupportingLayout();
  SetAbr(abr_data);

  // Layout will be updated. Since A/B state is one successful + one unbootable
  ASSERT_NO_FATAL_FAILURE(FindBootManager());

  {
    auto result = boot_manager_->QueryActiveConfiguration();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->is_ok());
    ASSERT_EQ(result->value()->configuration, fuchsia_paver::wire::Configuration::kB);
  }

  {
    auto result = boot_manager_->SetConfigurationActive(fuchsia_paver::wire::Configuration::kA);
    ASSERT_OK(result.status());
  }

  {
    // The query result will come from the cache as we haven't flushed.
    // Validate that it is correct.
    auto result = boot_manager_->QueryActiveConfiguration();
    ASSERT_OK(result.status());
    ASSERT_EQ(result->value()->configuration, fuchsia_paver::wire::Configuration::kA);
  }

  // Haven't flushed yet. abr data in storage should stayed the same.
  // Since layout changed, use the updated layout to find abr.
  auto header = sysconfig::SyncClientAbrWearLeveling::GetAbrWearLevelingSupportedLayout();
  auto actual = GetAbrInWearLeveling(header, 0);
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));

  {
    auto result_sync = boot_manager_->Flush();
    ASSERT_OK(result_sync.status());
    ASSERT_OK(result_sync.value().status);
  }

  // Expected result: successful slot a, active slot b with max tries and priority.
  abr_data.slot_data[0].tries_remaining = kAbrMaxTriesRemaining;
  abr_data.slot_data[0].successful_boot = 0;
  abr_data.slot_data[0].priority = kAbrMaxPriority;
  abr_data.slot_data[1].tries_remaining = 0;
  abr_data.slot_data[1].successful_boot = 1;
  abr_data.slot_data[1].priority = 1;
  ComputeCrc(&abr_data);

  // Validate that new abr data is flushed to memory.
  // The first page (page 0) in the abr sub-partition is occupied by the initial abr data.
  // Thus, the new abr metadata is expected to be appended at the 2nd page (page 1).
  actual = GetAbrInWearLeveling(header, 1);
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetBuffered) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  fuchsia_paver::wire::Configuration configs[] = {fuchsia_paver::wire::Configuration::kA,
                                                  fuchsia_paver::wire::Configuration::kB,
                                                  fuchsia_paver::wire::Configuration::kRecovery};

  for (auto config : configs) {
    fuchsia_mem::wire::Buffer payload;
    CreatePayload(32, &payload);
    auto result = data_sink_->WriteAsset(config, fuchsia_paver::wire::Asset::kVerifiedBootMetadata,
                                         std::move(payload));
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
  }
  ValidateUnwrittenPages(14 * kPagesPerBlock + 32, 96);

  auto sync_result = data_sink_->Flush();
  ASSERT_OK(sync_result.status());
  ASSERT_OK(sync_result.value().status);
  ValidateWrittenPages(14 * kPagesPerBlock + 32, 96);
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetTwice) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  fuchsia_mem::wire::Buffer payload;
  CreatePayload(static_cast<size_t>(2) * kPagesPerBlock, &payload);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  {
    auto result = data_sink_->WriteAsset(fuchsia_paver::wire::Configuration::kA,
                                         fuchsia_paver::wire::Asset::kKernel, std::move(payload));
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    CreatePayload(static_cast<size_t>(2) * kPagesPerBlock, &payload);
    ValidateWritten(8, 2);
    ValidateUnwritten(10, 4);
  }
  {
    auto result = data_sink_->WriteAsset(fuchsia_paver::wire::Configuration::kA,
                                         fuchsia_paver::wire::Asset::kKernel, std::move(payload));
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    ValidateWritten(8, 2);
    ValidateUnwritten(10, 4);
  }
}

TEST_F(PaverServiceSkipBlockTest, ReadFirmwareConfigA) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  WriteData(kBootloaderFirstBlock * kPagesPerBlock,
            static_cast<size_t>(kBootloaderBlocks) * kPagesPerBlock, 0x4a);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->ReadFirmware(fuchsia_paver::wire::Configuration::kA,
                                         fidl::StringView::FromExternal(kFirmwareTypeBootloader));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().is_ok());
  ValidateWritten(result.value().value()->firmware,
                  static_cast<size_t>(kBootloaderBlocks) * kPagesPerBlock);
}

TEST_F(PaverServiceSkipBlockTest, ReadFirmwareUnsupportedConfigBFallBackToA) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  WriteData(kBootloaderFirstBlock * kPagesPerBlock,
            static_cast<size_t>(kBootloaderBlocks) * kPagesPerBlock, 0x4a);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->ReadFirmware(fuchsia_paver::wire::Configuration::kB,
                                         fidl::StringView::FromExternal(kFirmwareTypeBootloader));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().is_ok());
  ValidateWritten(result.value().value()->firmware,
                  static_cast<size_t>(kBootloaderBlocks) * kPagesPerBlock);
}

TEST_F(PaverServiceSkipBlockTest, ReadFirmwareUnsupportedConfigR) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->ReadFirmware(fuchsia_paver::wire::Configuration::kRecovery,
                                         fidl::StringView::FromExternal(kFirmwareTypeBootloader));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().is_error());
}

TEST_F(PaverServiceSkipBlockTest, ReadFirmwareUnsupportedType) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->ReadFirmware(fuchsia_paver::wire::Configuration::kA,
                                         fidl::StringView::FromExternal(kFirmwareTypeUnsupported));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().is_error());
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareConfigASupported) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  fuchsia_mem::wire::Buffer payload;
  CreatePayload(static_cast<size_t>(4) * kPagesPerBlock, &payload);
  auto result = data_sink_->WriteFirmware(fuchsia_paver::wire::Configuration::kA,
                                          fidl::StringView::FromExternal(kFirmwareTypeBootloader),
                                          std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().result.is_status());
  ASSERT_OK(result.value().result.status());
  ValidateWritten(kBootloaderFirstBlock, 4);
  WriteData(kBootloaderFirstBlock, static_cast<size_t>(4) * kPagesPerBlock, 0xff);
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareUnsupportedConfigBFallBackToA) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  fuchsia_mem::wire::Buffer payload;
  CreatePayload(static_cast<size_t>(4) * kPagesPerBlock, &payload);
  auto result = data_sink_->WriteFirmware(fuchsia_paver::wire::Configuration::kB,
                                          fidl::StringView::FromExternal(kFirmwareTypeBootloader),
                                          std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().result.is_status());
  ASSERT_OK(result.value().result.status());
  ValidateWritten(kBootloaderFirstBlock, 4);
  WriteData(kBootloaderFirstBlock, static_cast<size_t>(4) * kPagesPerBlock, 0xff);
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareUnsupportedConfigR) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  fuchsia_mem::wire::Buffer payload;
  CreatePayload(static_cast<size_t>(4) * kPagesPerBlock, &payload);
  auto result = data_sink_->WriteFirmware(fuchsia_paver::wire::Configuration::kRecovery,
                                          fidl::StringView::FromExternal(kFirmwareTypeBootloader),
                                          std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().result.is_unsupported());
  ASSERT_TRUE(result.value().result.unsupported());
  ValidateUnwritten(kBootloaderFirstBlock, 4);
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareBl2ConfigASupported) {
  // BL2 special handling: we should always leave the first 4096 bytes intact.
  constexpr size_t kBl2StartByte{static_cast<size_t>(kBl2FirstBlock) * kPageSize * kPagesPerBlock};
  constexpr size_t kBl2SkipLength{4096};

  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  ASSERT_NO_FATAL_FAILURE(FindDataSink());

  WriteDataBytes(kBl2StartByte, kBl2SkipLength, 0xC6);
  fuchsia_mem::wire::Buffer payload;
  CreatePayload(kBl2ImagePages, &payload);
  auto result = data_sink_->WriteFirmware(fuchsia_paver::wire::Configuration::kA,
                                          fidl::StringView::FromExternal(kFirmwareTypeBl2),
                                          std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().result.is_status());
  ASSERT_OK(result.value().result.status());
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareBl2UnsupportedConfigBFallBackToA) {
  // BL2 special handling: we should always leave the first 4096 bytes intact.
  constexpr size_t kBl2StartByte{static_cast<size_t>(kBl2FirstBlock) * kPageSize * kPagesPerBlock};
  constexpr size_t kBl2SkipLength{4096};

  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  WriteDataBytes(kBl2StartByte, kBl2SkipLength, 0xC6);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  fuchsia_mem::wire::Buffer payload;
  CreatePayload(kBl2ImagePages, &payload);
  auto result = data_sink_->WriteFirmware(fuchsia_paver::wire::Configuration::kB,
                                          fidl::StringView::FromExternal(kFirmwareTypeBl2),
                                          std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().result.is_status());
  ASSERT_OK(result.value().result.status());
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareBl2UnsupportedConfigR) {
  // BL2 special handling: we should always leave the first 4096 bytes intact.
  constexpr size_t kBl2StartByte{static_cast<size_t>(kBl2FirstBlock) * kPageSize * kPagesPerBlock};
  constexpr size_t kBl2SkipLength{4096};

  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());
  WriteDataBytes(kBl2StartByte, kBl2SkipLength, 0xC6);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  fuchsia_mem::wire::Buffer payload;
  CreatePayload(kBl2ImagePages, &payload);
  auto result = data_sink_->WriteFirmware(fuchsia_paver::wire::Configuration::kRecovery,
                                          fidl::StringView::FromExternal(kFirmwareTypeBl2),
                                          std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().result.is_unsupported());
  ASSERT_TRUE(result.value().result.unsupported());
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareUnsupportedType) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  constexpr fuchsia_paver::wire::Configuration kAllConfigs[] = {
      fuchsia_paver::wire::Configuration::kA,
      fuchsia_paver::wire::Configuration::kB,
      fuchsia_paver::wire::Configuration::kRecovery,
  };

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  for (auto config : kAllConfigs) {
    fuchsia_mem::wire::Buffer payload;
    CreatePayload(static_cast<size_t>(4) * kPagesPerBlock, &payload);
    auto result = data_sink_->WriteFirmware(
        config, fidl::StringView::FromExternal(kFirmwareTypeUnsupported), std::move(payload));
    ASSERT_OK(result.status());
    ASSERT_TRUE(result.value().result.is_unsupported());
    ASSERT_TRUE(result.value().result.unsupported());
    ValidateUnwritten(kBootloaderFirstBlock, 4);
    ValidateUnwritten(kBl2FirstBlock, 1);
  }
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareError) {
  // Make a RAM NAND device without a visible "bootloader" partition so that
  // the partitioner initializes properly but then fails when trying to find it.
  fuchsia_hardware_nand::wire::RamNandInfo info = NandInfo();
  info.partition_map.partitions[1].hidden = true;
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand(std::move(info)));

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  fuchsia_mem::wire::Buffer payload;
  CreatePayload(static_cast<size_t>(4) * kPagesPerBlock, &payload);
  auto result = data_sink_->WriteFirmware(fuchsia_paver::wire::Configuration::kA,
                                          fidl::StringView::FromExternal(kFirmwareTypeBootloader),
                                          std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().result.is_status());
  ASSERT_NOT_OK(result.value().result.status());
  ValidateUnwritten(kBootloaderFirstBlock, 4);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetKernelConfigA) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  WriteData(8 * kPagesPerBlock, static_cast<size_t>(2) * kPagesPerBlock, 0x4a);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->ReadAsset(fuchsia_paver::wire::Configuration::kA,
                                      fuchsia_paver::wire::Asset::kKernel);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ValidateWritten(result->value()->asset, static_cast<size_t>(2) * kPagesPerBlock);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetKernelConfigB) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  WriteData(10 * kPagesPerBlock, static_cast<size_t>(2) * kPagesPerBlock, 0x4a);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->ReadAsset(fuchsia_paver::wire::Configuration::kB,
                                      fuchsia_paver::wire::Asset::kKernel);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ValidateWritten(result->value()->asset, static_cast<size_t>(2) * kPagesPerBlock);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetKernelConfigRecovery) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  WriteData(12 * kPagesPerBlock, static_cast<size_t>(2) * kPagesPerBlock, 0x4a);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->ReadAsset(fuchsia_paver::wire::Configuration::kRecovery,
                                      fuchsia_paver::wire::Asset::kKernel);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ValidateWritten(result->value()->asset, static_cast<size_t>(2) * kPagesPerBlock);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetVbMetaConfigA) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  WriteData(14 * kPagesPerBlock + 32, 32, 0x4a);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->ReadAsset(fuchsia_paver::wire::Configuration::kA,
                                      fuchsia_paver::wire::Asset::kVerifiedBootMetadata);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ValidateWritten(result->value()->asset, 32);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetVbMetaConfigB) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  WriteData(14 * kPagesPerBlock + 64, 32, 0x4a);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->ReadAsset(fuchsia_paver::wire::Configuration::kB,
                                      fuchsia_paver::wire::Asset::kVerifiedBootMetadata);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ValidateWritten(result->value()->asset, 32);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetVbMetaConfigRecovery) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  WriteData(14 * kPagesPerBlock + 96, 32, 0x4a);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->ReadAsset(fuchsia_paver::wire::Configuration::kRecovery,
                                      fuchsia_paver::wire::Asset::kVerifiedBootMetadata);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ValidateWritten(result->value()->asset, 32);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetZbiSize) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  zbi_header_t container;
  container.type = ZBI_TYPE_CONTAINER;
  container.extra = ZBI_CONTAINER_MAGIC;
  container.magic = ZBI_ITEM_MAGIC;
  container.flags = ZBI_FLAGS_VERSION;
  container.crc32 = ZBI_ITEM_NO_CRC32;
  container.length = sizeof(zbi_header_t);

  WriteDataBytes(8 * kPagesPerBlock * kPageSize, &container, sizeof(container));

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->ReadAsset(fuchsia_paver::wire::Configuration::kA,
                                      fuchsia_paver::wire::Asset::kKernel);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_EQ(result->value()->asset.size, sizeof(container));
}

TEST_F(PaverServiceSkipBlockTest, WriteBootloader) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  fuchsia_mem::wire::Buffer payload;
  CreatePayload(static_cast<size_t>(4) * kPagesPerBlock, &payload);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->WriteBootloader(std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWritten(4, 4);
}

// We prefill the bootloader partition with the expected data, leaving the last block as 0xFF.
// Normally the last page would be overwritten with 0s, but because the actual payload is identical,
// we don't actually pave the image, so the extra page stays as 0xFF.
TEST_F(PaverServiceSkipBlockTest, WriteBootloaderNotAligned) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  fuchsia_mem::wire::Buffer payload;
  CreatePayload(static_cast<size_t>(4) * kPagesPerBlock - 1, &payload);

  WriteData(4 * kPagesPerBlock, static_cast<size_t>(4) * kPagesPerBlock - 1, 0x4a);
  WriteData(8 * kPagesPerBlock - 1, 1, 0xff);

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->WriteBootloader(std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWrittenPages(4 * kPagesPerBlock, static_cast<size_t>(4) * kPagesPerBlock - 1);
  ValidateUnwrittenPages(8 * kPagesPerBlock - 1, 1);
}

TEST_F(PaverServiceSkipBlockTest, WriteVolumes) {
  // TODO(fxbug.dev/33793): Figure out a way to test this.
}

TEST_F(PaverServiceSkipBlockTest, WipeVolumeEmptyFvm) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->WipeVolume();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_TRUE(result->value()->volume);
}

void CheckGuid(const fbl::unique_fd& device, const uint8_t type[GPT_GUID_LEN]) {
  fdio_cpp::UnownedFdioCaller caller(device.get());
  auto result = fidl::WireCall(caller.borrow_as<partition::Partition>())->GetTypeGuid();
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  auto* guid = result.value().guid.get();
  EXPECT_BYTES_EQ(type, guid, GPT_GUID_LEN);
}

TEST_F(PaverServiceSkipBlockTest, WipeVolumeCreatesFvm) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  constexpr size_t kBufferSize = 8192;
  char buffer[kBufferSize];
  memset(buffer, 'a', kBufferSize);
  EXPECT_EQ(kBufferSize, pwrite(fvm_.get(), buffer, kBufferSize, 0));

  ASSERT_NO_FATAL_FAILURE(FindDataSink());
  auto result = data_sink_->WipeVolume();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_TRUE(result->value()->volume);

  EXPECT_EQ(kBufferSize, pread(fvm_.get(), buffer, kBufferSize, 0));
  EXPECT_BYTES_EQ(fs_management::kFvmMagic, buffer, sizeof(fs_management::kFvmMagic));

  fidl::ClientEnd<fuchsia_hardware_block_volume::VolumeManager> volume_client =
      std::move(result->value()->volume);
  // This force-casts the protocol type from
  // |fuchsia.hardware.block.volume/VolumeManager| into
  // |fuchsia.device/Controller|. It only works because protocols hosted
  // on devfs are automatically multiplexed with both the
  // |fuchsia.device/Controller| and the |fuchsia.io/File| protocol.
  fidl::ClientEnd<fuchsia_device::Controller> device_client(std::move(volume_client.channel()));
  std::string path = storage::GetTopologicalPath(device_client).value().substr(5);  // strip "/dev/"
  ASSERT_FALSE(path.empty());

  std::string blob_path = path + "/blobfs-p-1/block";
  fbl::unique_fd blob_device(openat(device_->devfs_root().get(), blob_path.c_str(), O_RDONLY));
  ASSERT_TRUE(blob_device);

  constexpr uint8_t kBlobType[GPT_GUID_LEN] = GUID_BLOB_VALUE;
  ASSERT_NO_FAILURES(CheckGuid(blob_device, kBlobType));

  uint8_t kEmptyData[kBufferSize];
  memset(kEmptyData, 0xff, kBufferSize);

  EXPECT_EQ(kBufferSize, pread(blob_device.get(), buffer, kBufferSize, 0));
  EXPECT_BYTES_EQ(kEmptyData, buffer, kBufferSize);

  std::string data_path = path + "/data-p-2/block";
  fbl::unique_fd data_device(openat(device_->devfs_root().get(), data_path.c_str(), O_RDONLY));
  ASSERT_TRUE(data_device);

  constexpr uint8_t kDataType[GPT_GUID_LEN] = GUID_DATA_VALUE;
  ASSERT_NO_FAILURES(CheckGuid(data_device, kDataType));

  EXPECT_EQ(kBufferSize, pread(data_device.get(), buffer, kBufferSize, 0));
  EXPECT_BYTES_EQ(kEmptyData, buffer, kBufferSize);
}

void PaverServiceSkipBlockTest::TestSysconfigWriteBufferedClient(uint32_t offset_in_pages,
                                                                 uint32_t sysconfig_pages) {
  {
    auto result = sysconfig_->GetPartitionSize();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->is_ok());
    ASSERT_EQ(result->value()->size, sysconfig_pages * kPageSize);
  }

  {
    fuchsia_mem::wire::Buffer payload;
    CreatePayload(sysconfig_pages, &payload);
    auto result = sysconfig_->Write(std::move(payload));
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    // Without flushing, data in the storage should remain unchanged.
    ASSERT_NO_FATAL_FAILURE(
        ValidateUnwrittenPages(14 * kPagesPerBlock + offset_in_pages, sysconfig_pages));
  }

  {
    auto result = sysconfig_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    ASSERT_NO_FATAL_FAILURE(
        ValidateWrittenPages(14 * kPagesPerBlock + offset_in_pages, sysconfig_pages));
  }

  {
    // Validate read.
    auto result = sysconfig_->Read();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->is_ok());
    ASSERT_NO_FATAL_FAILURE(ValidateWritten(result->value()->data, sysconfig_pages));
  }
}

TEST_F(PaverServiceSkipBlockTest, SysconfigWriteWithBufferredClientLayoutNotUpdated) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  // Enable write-caching + abr metadata wear-leveling
  fake_svc_.fake_boot_args().SetAstroSysConfigAbrWearLeveling(true);

  ASSERT_NO_FATAL_FAILURE(FindSysconfig());

  ASSERT_NO_FATAL_FAILURE(TestSysconfigWriteBufferedClient(0, 15 * 2));
}

TEST_F(PaverServiceSkipBlockTest, SysconfigWriteWithBufferredClientLayoutUpdated) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  // Enable write-caching + abr metadata wear-leveling
  fake_svc_.fake_boot_args().SetAstroSysConfigAbrWearLeveling(true);

  auto abr_data = GetAbrWearlevelingSupportingLayout();
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindSysconfig());

  ASSERT_NO_FATAL_FAILURE(TestSysconfigWriteBufferedClient(2, 5 * 2));
}

void PaverServiceSkipBlockTest::TestSysconfigWipeBufferedClient(uint32_t offset_in_pages,
                                                                uint32_t sysconfig_pages) {
  {
    auto result = sysconfig_->Wipe();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    // Without flushing, data in the storage should remain unchanged.
    ASSERT_NO_FATAL_FAILURE(
        ValidateUnwrittenPages(14 * kPagesPerBlock + offset_in_pages, sysconfig_pages));
  }

  {
    auto result = sysconfig_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    ASSERT_NO_FATAL_FAILURE(AssertContents(
        static_cast<size_t>(14) * kSkipBlockSize + offset_in_pages * static_cast<size_t>(kPageSize),
        sysconfig_pages * static_cast<size_t>(kPageSize), 0));
  }
}

TEST_F(PaverServiceSkipBlockTest, SysconfigWipeWithBufferredClientLayoutNotUpdated) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  // Enable write-caching + abr metadata wear-leveling
  fake_svc_.fake_boot_args().SetAstroSysConfigAbrWearLeveling(true);

  ASSERT_NO_FATAL_FAILURE(FindSysconfig());

  ASSERT_NO_FATAL_FAILURE(TestSysconfigWipeBufferedClient(0, 15 * 2));
}

TEST_F(PaverServiceSkipBlockTest, SysconfigWipeWithBufferredClientLayoutUpdated) {
  ASSERT_NO_FATAL_FAILURE(InitializeRamNand());

  // Enable write-caching + abr metadata wear-leveling
  fake_svc_.fake_boot_args().SetAstroSysConfigAbrWearLeveling(true);

  auto abr_data = GetAbrWearlevelingSupportingLayout();
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURE(FindSysconfig());

  ASSERT_NO_FATAL_FAILURE(TestSysconfigWipeBufferedClient(2, 5 * 2));
}

constexpr uint8_t kEmptyType[GPT_GUID_LEN] = GUID_EMPTY_VALUE;

#if defined(__x86_64__)
class PaverServiceBlockTest : public PaverServiceTest {
 public:
  PaverServiceBlockTest() { ASSERT_NO_FATAL_FAILURE(SpawnIsolatedDevmgr()); }

 protected:
  void SpawnIsolatedDevmgr() {
    driver_integration_test::IsolatedDevmgr::Args args;
    args.disable_block_watcher = false;

    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    // Forward the block watcher FIDL interface from the devmgr.
    fake_svc_.ForwardServiceTo(fidl::DiscoverableProtocolName<fuchsia_fshost::BlockWatcher>,
                               devmgr_.fshost_svc_dir());

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform/00:00:2d/ramctl", &fd));
    static_cast<paver::Paver*>(provider_ctx_)->set_devfs_root(devmgr_.devfs_root().duplicate());
    static_cast<paver::Paver*>(provider_ctx_)->set_svc_root(std::move(fake_svc_.svc_chan()));
  }

  void UseBlockDevice(fidl::ClientEnd<fuchsia_hardware_block::Block> block_device) {
    zx::result endpoints = fidl::CreateEndpoints<fuchsia_paver::DynamicDataSink>();
    ASSERT_OK(endpoints.status_value());
    auto& [local, remote] = endpoints.value();

    auto result = client_->UseBlockDevice(std::move(block_device), std::move(remote));
    ASSERT_OK(result.status());
    data_sink_ = fidl::WireSyncClient(std::move(local));
  }

  IsolatedDevmgr devmgr_;
  fidl::WireSyncClient<fuchsia_paver::DynamicDataSink> data_sink_;
};

TEST_F(PaverServiceBlockTest, DISABLED_InitializePartitionTables) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // 32GiB disk.
  constexpr uint64_t block_count = (32LU << 30) / kBlockSize;
  ASSERT_NO_FATAL_FAILURE(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count, &gpt_dev));

  fidl::UnownedClientEnd block_interface = gpt_dev->block_interface();
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  zx::result gpt_chan = component::Clone(block_interface, component::AssumeProtocolComposesNode);
  ASSERT_OK(gpt_chan.status_value());
  ASSERT_NO_FATAL_FAILURE(UseBlockDevice(std::move(gpt_chan.value())));

  auto result = data_sink_->InitializePartitionTables();
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
}

TEST_F(PaverServiceBlockTest, DISABLED_InitializePartitionTablesMultipleDevices) {
  std::unique_ptr<BlockDevice> gpt_dev1, gpt_dev2;
  // 32GiB disk.
  constexpr uint64_t block_count = (32LU << 30) / kBlockSize;
  ASSERT_NO_FATAL_FAILURE(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count, &gpt_dev1));
  ASSERT_NO_FATAL_FAILURE(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count, &gpt_dev2));

  fidl::UnownedClientEnd block_interface = gpt_dev1->block_interface();
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  zx::result gpt_chan = component::Clone(block_interface, component::AssumeProtocolComposesNode);
  ASSERT_OK(gpt_chan.status_value());
  ASSERT_NO_FATAL_FAILURE(UseBlockDevice(std::move(gpt_chan.value())));

  auto result = data_sink_->InitializePartitionTables();
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
}

TEST_F(PaverServiceBlockTest, DISABLED_WipePartitionTables) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // 32GiB disk.
  constexpr uint64_t block_count = (32LU << 30) / kBlockSize;
  ASSERT_NO_FATAL_FAILURE(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count, &gpt_dev));

  fidl::UnownedClientEnd block_interface = gpt_dev->block_interface();
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  zx::result gpt_chan = component::Clone(block_interface, component::AssumeProtocolComposesNode);
  ASSERT_OK(gpt_chan.status_value());
  ASSERT_NO_FATAL_FAILURE(UseBlockDevice(std::move(gpt_chan.value())));

  auto result = data_sink_->InitializePartitionTables();
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);

  auto wipe_result = data_sink_->WipePartitionTables();
  ASSERT_OK(wipe_result.status());
  ASSERT_OK(wipe_result.value().status);
}

TEST_F(PaverServiceBlockTest, DISABLED_WipeVolume) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // 32GiB disk.
  constexpr uint64_t block_count = (32LU << 30) / kBlockSize;
  ASSERT_NO_FATAL_FAILURE(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count, &gpt_dev));

  fidl::UnownedClientEnd block_interface = gpt_dev->block_interface();
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  zx::result gpt_chan = component::Clone(block_interface, component::AssumeProtocolComposesNode);
  ASSERT_OK(gpt_chan.status_value());
  ASSERT_NO_FATAL_FAILURE(UseBlockDevice(std::move(gpt_chan.value())));

  auto result = data_sink_->InitializePartitionTables();
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);

  auto wipe_result = data_sink_->WipeVolume();
  ASSERT_OK(wipe_result.status());
  ASSERT_FALSE(wipe_result->is_error());
}
#endif

class PaverServiceGptDeviceTest : public PaverServiceTest {
 protected:
  void SpawnIsolatedDevmgr(const char* board_name) {
    driver_integration_test::IsolatedDevmgr::Args args;
    args.disable_block_watcher = false;

    args.board_name = board_name;
    ASSERT_OK(driver_integration_test::IsolatedDevmgr::Create(&args, &devmgr_));

    // Forward the block watcher FIDL interface from the devmgr.
    fake_svc_.ForwardServiceTo(fidl::DiscoverableProtocolName<fuchsia_fshost::BlockWatcher>,
                               devmgr_.fshost_svc_dir());

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform/00:00:2d/ramctl", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
    static_cast<paver::Paver*>(provider_ctx_)->set_dispatcher(loop_.dispatcher());
    static_cast<paver::Paver*>(provider_ctx_)->set_devfs_root(devmgr_.devfs_root().duplicate());
    fidl::ClientEnd<fuchsia_io::Directory> svc_root = GetSvcRoot();
    static_cast<paver::Paver*>(provider_ctx_)->set_svc_root(std::move(svc_root));
  }

  void InitializeGptDevice(const char* board_name, uint64_t block_count, uint32_t block_size) {
    SpawnIsolatedDevmgr(board_name);
    block_count_ = block_count;
    block_size_ = block_size;
    ASSERT_NO_FATAL_FAILURE(
        BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count, block_size, &gpt_dev_));
  }

  fidl::ClientEnd<fuchsia_io::Directory> GetSvcRoot() {
    return component::MaybeClone(fake_svc_.svc_chan());
  }

  struct PartitionDescription {
    const char* name;
    const uint8_t* type;
    uint64_t start;
    uint64_t length;
  };

  void InitializeStartingGPTPartitions(const std::vector<PartitionDescription>& init_partitions) {
    InitializeStartingGPTPartitions(gpt_dev_.get(), init_partitions);
  }

  void InitializeStartingGPTPartitions(const BlockDevice* gpt_dev,
                                       const std::vector<PartitionDescription>& init_partitions) {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    // TODO(https://fxbug.dev/112484): this relies on multiplexing.
    zx::result clone =
        component::Clone(gpt_dev->block_interface(), component::AssumeProtocolComposesNode);
    ASSERT_OK(clone);
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_OK(gpt::GptDevice::Create(std::move(clone.value()), gpt_dev->block_size(),
                                     gpt_dev->block_count(), &gpt));
    ASSERT_OK(gpt->Sync());

    for (const auto& part : init_partitions) {
      ASSERT_OK(
          gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
          "%s", part.name);
    }

    ASSERT_OK(gpt->Sync());

    fidl::UnownedClientEnd block_interface = gpt_dev->block_interface();
    auto result = fidl::WireCall(
                      fidl::UnownedClientEnd<fuchsia_device::Controller>(block_interface.channel()))
                      ->Rebind(fidl::StringView("gpt.so"));
    ASSERT_TRUE(result.ok());
    ASSERT_FALSE(result->is_error());
  }

  uint8_t* GetRandomGuid() {
    static uint8_t random_guid[GPT_GUID_LEN];
    zx_cprng_draw(random_guid, GPT_GUID_LEN);
    return random_guid;
  }

  driver_integration_test::IsolatedDevmgr devmgr_;
  std::unique_ptr<BlockDevice> gpt_dev_;
  uint64_t block_count_;
  uint64_t block_size_;
};

class PaverServiceLuisTest : public PaverServiceGptDeviceTest {
 public:
  static constexpr size_t kFvmBlockStart = 0x20400;
  static constexpr size_t kFvmBlockSize = 0x10000;

  void SetUp() override { ASSERT_NO_FATAL_FAILURE(InitializeGptDevice("luis", 0x748034, 512)); }

  void InitializeLuisGPTPartitions() {
    constexpr uint8_t kDummyType[GPT_GUID_LEN] = {0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
                                                  0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4};
    const std::vector<PartitionDescription> kLuisStartingPartitions = {
        {GPT_DURABLE_BOOT_NAME, kDummyType, 0x10400, 0x10000},
        {GPT_FVM_NAME, kDummyType, kFvmBlockStart, kFvmBlockSize},
    };
    ASSERT_NO_FATAL_FAILURE(InitializeStartingGPTPartitions(kLuisStartingPartitions));
  }
};

TEST_F(PaverServiceLuisTest, CreateAbr) {
  ASSERT_NO_FATAL_FAILURE(InitializeLuisGPTPartitions());
  std::shared_ptr<paver::Context> context;
  fidl::ClientEnd<fuchsia_io::Directory> svc_root = GetSvcRoot();
  EXPECT_OK(abr::ClientFactory::Create(devmgr_.devfs_root().duplicate(), svc_root, context));
}

TEST_F(PaverServiceLuisTest, SysconfigNotSupportedAndFailWithPeerClosed) {
  ASSERT_NO_FATAL_FAILURE(InitializeLuisGPTPartitions());
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_paver::Sysconfig>();
  ASSERT_OK(endpoints.status_value());
  auto& [local, remote] = endpoints.value();
  auto result = client_->FindSysconfig(std::move(remote));
  ASSERT_OK(result.status());

  fidl::WireSyncClient sysconfig(std::move(local));
  auto wipe_result = sysconfig->Wipe();
  ASSERT_EQ(wipe_result.status(), ZX_ERR_PEER_CLOSED);
}

TEST_F(PaverServiceLuisTest, FindGPTDevicesIgnoreFvmPartitions) {
  // Initialize the primary block solely as FVM and allocate sub-partitions.
  fvm::SparseImage header = {};
  header.slice_size = 1 << 20;
  fidl::UnownedClientEnd block_interface = gpt_dev_->block_interface();
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  zx::result gpt_chan = component::Clone(block_interface, component::AssumeProtocolComposesNode);
  ASSERT_OK(gpt_chan.status_value());
  fbl::unique_fd block_fd;
  ASSERT_TRUE(zx::make_result(fdio_fd_create(gpt_chan.value().TakeChannel().release(),
                                             block_fd.reset_and_get_address()))
                  .is_ok());
  fbl::unique_fd fvm_fd(FvmPartitionFormat(devmgr_.devfs_root(), std::move(block_fd), header,
                                           paver::BindOption::Reformat));
  ASSERT_TRUE(fvm_fd);
  auto status = paver::AllocateEmptyPartitions(devmgr_.devfs_root(), fvm_fd);
  ASSERT_TRUE(status.is_ok());

  // Check that FVM created sub-partitions are not considered as candidates.
  paver::GptDevicePartitioner::GptDevices gpt_devices;
  paver::GptDevicePartitioner::FindGptDevices(devmgr_.devfs_root(), &gpt_devices);
  ASSERT_EQ(gpt_devices.size(), 1);
  ASSERT_EQ(gpt_devices[0].first, std::string("/dev/sys/platform/00:00:2d/ramctl/ramdisk-0/block"));
}

TEST_F(PaverServiceLuisTest, WriteOpaqueVolume) {
  // TODO(b/217597389): Consdier also adding an e2e test for this interface.
  ASSERT_NO_FATAL_FAILURE(InitializeLuisGPTPartitions());
  auto endpoints = fidl::CreateEndpoints<fuchsia_paver::DynamicDataSink>();
  ASSERT_OK(endpoints.status_value());
  auto& [local, remote] = endpoints.value();

  {
    fidl::UnownedClientEnd block_interface = gpt_dev_->block_interface();
    // TODO(https://fxbug.dev/112484): this relies on multiplexing.
    zx::result gpt_chan = component::Clone(block_interface, component::AssumeProtocolComposesNode);
    ASSERT_OK(gpt_chan.status_value());
    ASSERT_OK(client_->UseBlockDevice(std::move(gpt_chan.value()), std::move(remote)));
  }
  fidl::WireSyncClient data_sink{std::move(local)};

  // Create a payload
  constexpr size_t kPayloadSize = 2048;
  std::vector<uint8_t> payload(kPayloadSize, 0x4a);

  fuchsia_mem::wire::Buffer payload_wire_buffer;
  zx::vmo payload_vmo;
  fzl::VmoMapper payload_vmo_mapper;
  ASSERT_OK(payload_vmo_mapper.CreateAndMap(kPayloadSize, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                            nullptr, &payload_vmo));
  memcpy(payload_vmo_mapper.start(), payload.data(), kPayloadSize);
  payload_wire_buffer.vmo = std::move(payload_vmo);
  payload_wire_buffer.size = kPayloadSize;

  // Write the payload as opaque volume
  auto result = data_sink->WriteOpaqueVolume(std::move(payload_wire_buffer));
  ASSERT_OK(result.status());

  // Create a block partition client to read the written content directly.
  fidl::UnownedClientEnd block_interface = gpt_dev_->block_interface();
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  zx::result block_service_channel =
      component::Clone(block_interface, component::AssumeProtocolComposesNode);
  ASSERT_OK(block_service_channel.status_value());
  std::unique_ptr<paver::BlockPartitionClient> block_client =
      std::make_unique<paver::BlockPartitionClient>(std::move(block_service_channel.value()));

  // Read the partition directly from block and verify.
  zx::vmo block_read_vmo;
  fzl::VmoMapper block_read_vmo_mapper;
  ASSERT_OK(
      block_read_vmo_mapper.CreateAndMap(kPayloadSize, ZX_VM_PERM_READ, nullptr, &block_read_vmo));
  ASSERT_OK(block_client->Read(block_read_vmo, kPayloadSize, kFvmBlockStart, 0));

  // Verify the written data against the payload
  ASSERT_BYTES_EQ(block_read_vmo_mapper.start(), payload.data(), kPayloadSize);
}

}  // namespace
