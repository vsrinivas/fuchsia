// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <fcntl.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/fshost/llcpp/fidl.h>
#include <fuchsia/hardware/block/partition/llcpp/fidl.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/abr/data.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/cksum.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/paver/provider.h>
#include <lib/sysconfig/sync-client.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/image.h>
#include <zircon/hw/gpt.h>

#include <memory>
#include <optional>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <soc/aml-common/aml-guid.h>
#include <zxtest/zxtest.h>

#include "src/storage/lib/paver/device-partitioner.h"
#include "src/storage/lib/paver/luis.h"
#include "src/storage/lib/paver/paver.h"
#include "src/storage/lib/paver/test/test-utils.h"
#include "src/storage/lib/paver/utils.h"

namespace {

namespace partition = ::llcpp::fuchsia::hardware::block::partition;

using devmgr_integration_test::IsolatedDevmgr;
using devmgr_integration_test::RecursiveWaitForFile;

constexpr std::string_view kFirmwareTypeBootloader("");
constexpr std::string_view kFirmwareTypeBl2("bl2");
constexpr std::string_view kFirmwareTypeUnsupported("unsupported_type");

// BL2 images must be exactly this size.
constexpr size_t kBl2ImageSize = 0x10000;
// Make sure we can use our page-based APIs to work with the BL2 image.
static_assert(kBl2ImageSize % kPageSize == 0);
constexpr size_t kBl2ImagePages = kBl2ImageSize / kPageSize;

constexpr uint32_t kBootloaderFirstBlock = 4;
constexpr uint32_t kBl2FirstBlock = 39;

constexpr fuchsia_hardware_nand_RamNandInfo
    kNandInfo =
        {
            .vmo = ZX_HANDLE_INVALID,
            .nand_info =
                {
                    .page_size = kPageSize,
                    .pages_per_block = kPagesPerBlock,
                    .num_blocks = kNumBlocks,
                    .ecc_bits = 8,
                    .oob_size = kOobSize,
                    .nand_class = fuchsia_hardware_nand_Class_PARTMAP,
                    .partition_guid = {},
                },
            .partition_map =
                {
                    .device_guid = {},
                    .partition_count = 8,
                    .partitions =
                        {
                            {
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
                                .last_block = 7,
                                .copy_count = 0,
                                .copy_byte_offset = 0,
                                .name = {'b', 'o', 'o', 't', 'l', 'o', 'a', 'd', 'e', 'r'},
                                .hidden = false,
                                .bbt = false,
                            },
                            {
                                .type_guid = GUID_ZIRCON_A_VALUE,
                                .unique_guid = {},
                                .first_block = 8,
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
                                .first_block = 18,
                                .last_block = 38,
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
                                .last_block = 39,
                                .copy_count = 0,
                                .copy_byte_offset = 0,
                                .name = {'b', 'l', '2',},
                                .hidden = false,
                                .bbt = false,
                            },
                        },
                },
            .export_nand_config = true,
            .export_partition_map = true,
};

class FakeBootArgs : public ::llcpp::fuchsia::boot::Arguments::Interface {
 public:
  zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
    return fidl::BindSingleInFlightOnly(dispatcher, std::move(request), this);
  }

  void GetString(::fidl::StringView arg, GetStringCompleter::Sync completer) override {
    completer.Reply(fidl::unowned_str(arg_response_));
  }

  // Stubs
  void GetStrings(::fidl::VectorView<::fidl::StringView> names,
                  GetStringsCompleter::Sync completer) override {}
  void GetBool(::fidl::StringView name, bool defaultval,
               GetBoolCompleter::Sync completer) override {
    if (strncmp(name.data(), "astro.sysconfig.abr-wear-leveling",
                sizeof("astro.sysconfig.abr-wear-leveling")) == 0) {
      completer.Reply(astro_sysconfig_abr_wear_leveling_);
    } else if (strncmp(name.data(), "astro.sysconfig.buffered-client",
                       sizeof("astro.sysconfig.buffered-client")) == 0) {
      completer.Reply(astro_sysconfig_buffered_client_);
    } else {
      completer.Reply(defaultval);
    }
  }
  void GetBools(::fidl::VectorView<::llcpp::fuchsia::boot::BoolPair> name,
                GetBoolsCompleter::Sync completer) override {}
  void Collect(::fidl::StringView name, CollectCompleter::Sync completer) override {}

  void SetAstroSysConfigAbrWearLeveling(bool opt) { astro_sysconfig_abr_wear_leveling_ = opt; }

  void SetAstroSysConfigBufferedClient(bool opt) { astro_sysconfig_buffered_client_ = opt; }

  void SetArgResponse(std::string arg_response) { arg_response_ = arg_response; }

 private:
  bool astro_sysconfig_abr_wear_leveling_ = false;
  bool astro_sysconfig_buffered_client_ = false;
  std::string arg_response_ = "-a";
};

class FakeSvc {
 public:
  explicit FakeSvc(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher), vfs_(dispatcher) {
    root_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
    root_dir_->AddEntry(::llcpp::fuchsia::boot::Arguments::Name,
                        fbl::MakeRefCounted<fs::Service>([this](zx::channel request) {
                          return fake_boot_args_.Connect(dispatcher_, std::move(request));
                        }));

    zx::channel svc_remote;
    ASSERT_OK(zx::channel::create(0, &svc_local_, &svc_remote));

    vfs_.ServeDirectory(root_dir_, std::move(svc_remote));
  }

  void ForwardServiceTo(const char* name, const zx::channel& root) {
    zx::channel cloned = zx::channel(fdio_service_clone(root.get()));
    root_dir_->AddEntry(
        name,
        fbl::MakeRefCounted<fs::Service>([name, cloned = std::move(cloned)](zx::channel request) {
          return fdio_service_connect_at(cloned.get(), fbl::StringPrintf("/svc/%s", name).data(),
                                         request.release());
        }));
  }

  FakeBootArgs& fake_boot_args() { return fake_boot_args_; }
  zx::channel& svc_chan() { return svc_local_; }

 private:
  async_dispatcher_t* dispatcher_;
  fbl::RefPtr<fs::PseudoDir> root_dir_;
  fs::SynchronousVfs vfs_;
  FakeBootArgs fake_boot_args_;
  zx::channel svc_local_;
};

class PaverServiceTest : public zxtest::Test {
 public:
  PaverServiceTest();

  ~PaverServiceTest();

 protected:
  void CreatePayload(size_t num_pages, ::llcpp::fuchsia::mem::Buffer* out);

  static constexpr size_t kKilobyte = 1 << 10;

  void ValidateWritten(const ::llcpp::fuchsia::mem::Buffer& buf, size_t num_pages) {
    ASSERT_GE(buf.size, num_pages * kPageSize);
    fzl::VmoMapper mapper;
    ASSERT_OK(mapper.Map(buf.vmo, 0, fbl::round_up(num_pages * kPageSize, ZX_PAGE_SIZE),
                         ZX_VM_PERM_READ));
    const uint8_t* start = reinterpret_cast<uint8_t*>(mapper.start());
    for (size_t i = 0; i < num_pages * kPageSize; i++) {
      ASSERT_EQ(start[i], 0x4a, "i = %zu", i);
    }
  }

  void* provider_ctx_ = nullptr;
  std::optional<::llcpp::fuchsia::paver::Paver::SyncClient> client_;
  async::Loop loop_;
  // The paver makes synchronous calls into /svc, so it must run in a seperate loop to not
  // deadlock.
  async::Loop loop2_;
  FakeSvc fake_svc_;
};

PaverServiceTest::PaverServiceTest()
    : loop_(&kAsyncLoopConfigAttachToCurrentThread),
      loop2_(&kAsyncLoopConfigNoAttachToCurrentThread),
      fake_svc_(loop2_.dispatcher()) {
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  client_.emplace(std::move(client));

  ASSERT_OK(paver_get_service_provider()->ops->init(&provider_ctx_));

  ASSERT_OK(paver_get_service_provider()->ops->connect(
      provider_ctx_, loop_.dispatcher(), ::llcpp::fuchsia::paver::Paver::Name, server.release()));
  loop_.StartThread("paver-svc-test-loop");
  loop2_.StartThread("paver-svc-test-loop-2");
}

PaverServiceTest::~PaverServiceTest() {
  loop_.Shutdown();
  loop2_.Shutdown();
  paver_get_service_provider()->ops->release(provider_ctx_);
  provider_ctx_ = nullptr;
}

void PaverServiceTest::CreatePayload(size_t num_pages, ::llcpp::fuchsia::mem::Buffer* out) {
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
  void InitializeRamNand(const fuchsia_hardware_nand_RamNandInfo& nand_info = kNandInfo) {
    ASSERT_NO_FATAL_FAILURES(SpawnIsolatedDevmgr(nand_info));
    ASSERT_NO_FATAL_FAILURES(WaitForDevices());
  }

 protected:
  void SpawnIsolatedDevmgr(const fuchsia_hardware_nand_RamNandInfo& nand_info) {
    ASSERT_EQ(device_.get(), nullptr);
    ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(nand_info, &device_));
    static_cast<paver::Paver*>(provider_ctx_)->set_dispatcher(loop_.dispatcher());
    static_cast<paver::Paver*>(provider_ctx_)->set_devfs_root(device_->devfs_root());
    static_cast<paver::Paver*>(provider_ctx_)->set_svc_root(std::move(fake_svc_.svc_chan()));
  }

  void WaitForDevices() {
    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(device_->devfs_root(),
                                   "misc/nand-ctl/ram-nand-0/sysconfig/skip-block", &fd));
    ASSERT_OK(RecursiveWaitForFile(device_->devfs_root(), "misc/nand-ctl/ram-nand-0/fvm/ftl/block",
                                   &fvm_));
  }

  void FindBootManager() {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));

    auto result = client_->FindBootManager(std::move(remote));
    ASSERT_OK(result.status());
    boot_manager_.emplace(std::move(local));
  }

  void FindDataSink() {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));

    auto result = client_->FindDataSink(std::move(remote));
    ASSERT_OK(result.status());
    data_sink_.emplace(std::move(local));
  }

  void FindSysconfig() {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));

    auto result = client_->FindSysconfig(std::move(remote));
    ASSERT_OK(result.status());
    sysconfig_.emplace(std::move(local));
  }

  void SetAbr(const AbrData& data) {
    auto* buf = reinterpret_cast<uint8_t*>(device_->mapper().start()) + (14 * kSkipBlockSize) +
                (60 * kKilobyte);
    *reinterpret_cast<AbrData*>(buf) = data;
  }

  AbrData GetAbr() {
    auto* buf = reinterpret_cast<uint8_t*>(device_->mapper().start()) + (14 * kSkipBlockSize) +
                (60 * kKilobyte);
    return *reinterpret_cast<AbrData*>(buf);
  }

  // Equivalence of GetAbr() in the context of abr wear-leveling.
  // Since there can be multiple pages in abr sub-partition that may have valid abr data,
  // argument |copy_index| is used to read a specific one.
  AbrData GetAbrInWearLeveling(const sysconfig_header& header, size_t copy_index) {
    auto* buf = reinterpret_cast<uint8_t*>(device_->mapper().start()) + (14 * kSkipBlockSize) +
                header.abr_metadata.offset + copy_index * 4 * kKilobyte;
    AbrData ret;
    memcpy(&ret, buf, sizeof(ret));
    return ret;
  }

  using PaverServiceTest::ValidateWritten;

  // Checks that the device mapper contains |expected| at each byte in the given
  // range. Uses ASSERT_EQ() per-byte to give a helperful message on failure.
  void AssertContents(size_t offset, size_t length, uint8_t expected) {
    const uint8_t* contents = static_cast<uint8_t*>(device_->mapper().start()) + offset;
    for (size_t i = 0; i < length; i++) {
      ASSERT_EQ(expected, contents[i], "i = %zu", i);
    }
  }

  void ValidateWritten(uint32_t block, size_t num_blocks) {
    AssertContents(block * kSkipBlockSize, num_blocks * kSkipBlockSize, 0x4A);
  }

  void ValidateUnwritten(uint32_t block, size_t num_blocks) {
    AssertContents(block * kSkipBlockSize, num_blocks * kSkipBlockSize, 0xFF);
  }

  void ValidateWrittenPages(uint32_t page, size_t num_pages) {
    AssertContents(page * kPageSize, num_pages * kPageSize, 0x4A);
  }

  void ValidateUnwrittenPages(uint32_t page, size_t num_pages) {
    AssertContents(page * kPageSize, num_pages * kPageSize, 0xFF);
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

  std::optional<::llcpp::fuchsia::paver::BootManager::SyncClient> boot_manager_;
  std::optional<::llcpp::fuchsia::paver::DataSink::SyncClient> data_sink_;
  std::optional<::llcpp::fuchsia::paver::Sysconfig::SyncClient> sysconfig_;

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
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  AbrData abr_data = {};
  memset(&abr_data, 0x3d, sizeof(abr_data));
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
}

TEST_F(PaverServiceSkipBlockTest, InitializeAbrAlreadyValid) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
}

TEST_F(PaverServiceSkipBlockTest, QueryActiveConfigurationInvalidAbr) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  AbrData abr_data = {};
  memset(&abr_data, 0x3d, sizeof(abr_data));
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
}

TEST_F(PaverServiceSkipBlockTest, QueryActiveConfigurationBothPriority0) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  AbrData abr_data = kAbrData;
  abr_data.slot_data[0].priority = 0;
  abr_data.slot_data[1].priority = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_err());
  ASSERT_STATUS(result->result.err(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PaverServiceSkipBlockTest, QueryActiveConfigurationSlotB) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_EQ(result->result.response().configuration, ::llcpp::fuchsia::paver::Configuration::B);
}

TEST_F(PaverServiceSkipBlockTest, QueryActiveConfigurationSlotA) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  AbrData abr_data = kAbrData;
  abr_data.slot_data[0].priority = 2;
  abr_data.slot_data[0].successful_boot = 1;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_EQ(result->result.response().configuration, ::llcpp::fuchsia::paver::Configuration::A);
}

TEST_F(PaverServiceSkipBlockTest, QueryCurrentConfigurationSlotA) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryCurrentConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_EQ(result->result.response().configuration, ::llcpp::fuchsia::paver::Configuration::A);
}

TEST_F(PaverServiceSkipBlockTest, QueryCurrentConfigurationSlotB) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  fake_svc_.fake_boot_args().SetArgResponse("-b");

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryCurrentConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_EQ(result->result.response().configuration, ::llcpp::fuchsia::paver::Configuration::B);
}

TEST_F(PaverServiceSkipBlockTest, QueryCurrentConfigurationSlotR) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  fake_svc_.fake_boot_args().SetArgResponse("-r");

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryCurrentConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_EQ(result->result.response().configuration,
            ::llcpp::fuchsia::paver::Configuration::RECOVERY);
}

TEST_F(PaverServiceSkipBlockTest, QueryCurrentConfigurationSlotInvalid) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  fake_svc_.fake_boot_args().SetArgResponse("");

  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryCurrentConfiguration();
  ASSERT_STATUS(result, ZX_ERR_PEER_CLOSED);
}

TEST_F(PaverServiceSkipBlockTest, QueryConfigurationStatusHealthy) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  auto abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryConfigurationStatus(::llcpp::fuchsia::paver::Configuration::B);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_EQ(result->result.response().status,
            ::llcpp::fuchsia::paver::ConfigurationStatus::HEALTHY);
}

TEST_F(PaverServiceSkipBlockTest, QueryConfigurationStatusPending) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[1].successful_boot = 0;
  abr_data.slot_data[1].tries_remaining = 1;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryConfigurationStatus(::llcpp::fuchsia::paver::Configuration::B);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_EQ(result->result.response().status,
            ::llcpp::fuchsia::paver::ConfigurationStatus::PENDING);
}

TEST_F(PaverServiceSkipBlockTest, QueryConfigurationStatusUnbootable) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->QueryConfigurationStatus(::llcpp::fuchsia::paver::Configuration::A);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_EQ(result->result.response().status,
            ::llcpp::fuchsia::paver::ConfigurationStatus::UNBOOTABLE);
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationActive) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  AbrData abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slot_data[0].priority = kAbrMaxPriority;
  abr_data.slot_data[0].tries_remaining = kAbrMaxTriesRemaining;
  abr_data.slot_data[0].successful_boot = 0;
  ComputeCrc(&abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  {
    auto result = boot_manager_->SetConfigurationActive(::llcpp::fuchsia::paver::Configuration::A);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationActiveRollover) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[1].priority = kAbrMaxPriority;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slot_data[1].priority = kAbrMaxPriority - 1;
  abr_data.slot_data[0].priority = kAbrMaxPriority;
  abr_data.slot_data[0].tries_remaining = kAbrMaxTriesRemaining;
  abr_data.slot_data[0].successful_boot = 0;
  ComputeCrc(&abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  {
    auto result = boot_manager_->SetConfigurationActive(::llcpp::fuchsia::paver::Configuration::A);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }
  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationUnbootableSlotA) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[0].priority = 2;
  abr_data.slot_data[0].tries_remaining = 3;
  abr_data.slot_data[0].successful_boot = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slot_data[0].priority = 0;
  abr_data.slot_data[0].tries_remaining = 0;
  abr_data.slot_data[0].successful_boot = 0;
  ComputeCrc(&abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  {
    auto result =
        boot_manager_->SetConfigurationUnbootable(::llcpp::fuchsia::paver::Configuration::A);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetConfigurationUnbootableSlotB) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[1].tries_remaining = 3;
  abr_data.slot_data[1].successful_boot = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slot_data[1].priority = 0;
  abr_data.slot_data[1].tries_remaining = 0;
  abr_data.slot_data[1].successful_boot = 0;
  ComputeCrc(&abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  {
    auto result =
        boot_manager_->SetConfigurationUnbootable(::llcpp::fuchsia::paver::Configuration::B);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetActiveConfigurationHealthy) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[1].tries_remaining = 3;
  abr_data.slot_data[1].successful_boot = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slot_data[1].tries_remaining = 0;
  abr_data.slot_data[1].successful_boot = 1;
  ComputeCrc(&abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  {
    auto result = boot_manager_->SetActiveConfigurationHealthy();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceSkipBlockTest, SetActiveConfigurationHealthyBothPriorityZero) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  AbrData abr_data = kAbrData;
  abr_data.slot_data[1].tries_remaining = 3;
  abr_data.slot_data[1].successful_boot = 0;
  abr_data.slot_data[1].priority = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  auto result = boot_manager_->SetActiveConfigurationHealthy();
  ASSERT_OK(result.status());
  ASSERT_NE(result->status, ZX_OK);
}

TEST_F(PaverServiceSkipBlockTest, BootManagerBuffered) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  fake_svc_.fake_boot_args().SetAstroSysConfigBufferedClient(true);
  AbrData abr_data = kAbrData;
  // Successful slot b, active slot a. Like what happen after a reboot following an OTA.
  abr_data.slot_data[0].tries_remaining = 3;
  abr_data.slot_data[0].successful_boot = 0;
  abr_data.slot_data[0].priority = 1;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  {
    auto result = boot_manager_->QueryActiveConfiguration();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->result.is_response());
    ASSERT_EQ(result->result.response().configuration, ::llcpp::fuchsia::paver::Configuration::A);
  }

  {
    auto result = boot_manager_->SetActiveConfigurationHealthy();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  {
    auto result =
        boot_manager_->SetConfigurationUnbootable(::llcpp::fuchsia::paver::Configuration::B);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  // haven't flushed yet, storage shall stay the same.
  auto abr = GetAbr();
  ASSERT_BYTES_EQ(&abr, &abr_data, sizeof(abr));

  {
    auto result = boot_manager_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  abr_data.slot_data[0].tries_remaining = 0;
  abr_data.slot_data[0].successful_boot = 1;
  abr_data.slot_data[0].priority = 1;
  abr_data.slot_data[1].tries_remaining = 0;
  abr_data.slot_data[1].successful_boot = 0;
  abr_data.slot_data[1].priority = 0;
  ComputeCrc(&abr_data);

  abr = GetAbr();
  ASSERT_BYTES_EQ(&abr, &abr_data, sizeof(abr));
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetKernelConfigA) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(2 * kPagesPerBlock, &payload);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->WriteAsset(::llcpp::fuchsia::paver::Configuration::A,
                                       ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ValidateWritten(8, 2);
  ValidateUnwritten(10, 4);
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetKernelConfigB) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(2 * kPagesPerBlock, &payload);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->WriteAsset(::llcpp::fuchsia::paver::Configuration::B,
                                       ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateUnwritten(8, 2);
  ValidateWritten(10, 2);
  ValidateUnwritten(12, 2);
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetKernelConfigRecovery) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(2 * kPagesPerBlock, &payload);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->WriteAsset(::llcpp::fuchsia::paver::Configuration::RECOVERY,
                                       ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateUnwritten(8, 4);
  ValidateWritten(12, 2);
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetVbMetaConfigA) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(32, &payload);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->WriteAsset(::llcpp::fuchsia::paver::Configuration::A,
                                       ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA,
                                       std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);

  auto sync_result = data_sink_->Flush();
  ASSERT_OK(sync_result.status());
  ASSERT_OK(sync_result.value().status);

  ValidateWrittenPages(14 * kPagesPerBlock + 32, 32);
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetVbMetaConfigB) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(32, &payload);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->WriteAsset(::llcpp::fuchsia::paver::Configuration::B,
                                       ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA,
                                       std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);

  auto sync_result = data_sink_->Flush();
  ASSERT_OK(sync_result.status());
  ASSERT_OK(sync_result.value().status);

  ValidateWrittenPages(14 * kPagesPerBlock + 64, 32);
}

TEST_F(PaverServiceSkipBlockTest, WriteAssetVbMetaConfigRecovery) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(32, &payload);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->WriteAsset(::llcpp::fuchsia::paver::Configuration::RECOVERY,
                                       ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA,
                                       std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);

  auto sync_result = data_sink_->Flush();
  ASSERT_OK(sync_result.status());
  ASSERT_OK(sync_result.value().status);

  ValidateWrittenPages(14 * kPagesPerBlock + 96, 32);
}

TEST_F(PaverServiceSkipBlockTest, AbrWearLevelingLayoutNotUpdated) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  // Enable write-caching + abr metadata wear-leveling
  fake_svc_.fake_boot_args().SetAstroSysConfigBufferedClient(true);
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
  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  {
    auto result = boot_manager_->QueryActiveConfiguration();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->result.is_response());
    ASSERT_EQ(result->result.response().configuration, ::llcpp::fuchsia::paver::Configuration::B);
  }

  {
    auto result = boot_manager_->SetActiveConfigurationHealthy();
    ASSERT_OK(result.status());
  }

  {
    // The query result will come from the cache as flushed is not called.
    // Validate that it is correct.
    auto result = boot_manager_->QueryActiveConfiguration();
    ASSERT_OK(result.status());
    ASSERT_EQ(result->result.response().configuration, ::llcpp::fuchsia::paver::Configuration::B);
  }

  {
    // Mark the old slot A as unbootable.
    auto set_unbootable_result =
        boot_manager_->SetConfigurationUnbootable(::llcpp::fuchsia::paver::Configuration::A);
    ASSERT_OK(set_unbootable_result.status());
  }

  // Haven't flushed yet. abr data in storage should stayed the same.
  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));

  {
    auto result_sync = boot_manager_->Flush();
    ASSERT_OK(result_sync.status());
    ASSERT_OK(result_sync->status);
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
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  // Enable write-caching + abr metadata wear-leveling
  fake_svc_.fake_boot_args().SetAstroSysConfigBufferedClient(true);
  fake_svc_.fake_boot_args().SetAstroSysConfigAbrWearLeveling(true);

  // Unbootable slot a, successful active slot b
  auto abr_data = GetAbrWearlevelingSupportingLayout();
  SetAbr(abr_data);

  // Layout will be updated. Since A/B state is one successful + one unbootable
  ASSERT_NO_FATAL_FAILURES(FindBootManager());

  {
    auto result = boot_manager_->QueryActiveConfiguration();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->result.is_response());
    ASSERT_EQ(result->result.response().configuration, ::llcpp::fuchsia::paver::Configuration::B);
  }

  {
    auto result = boot_manager_->SetConfigurationActive(::llcpp::fuchsia::paver::Configuration::A);
    ASSERT_OK(result.status());
  }

  {
    // The query result will come from the cache as we haven't flushed.
    // Validate that it is correct.
    auto result = boot_manager_->QueryActiveConfiguration();
    ASSERT_OK(result.status());
    ASSERT_EQ(result->result.response().configuration, ::llcpp::fuchsia::paver::Configuration::A);
  }

  // Haven't flushed yet. abr data in storage should stayed the same.
  // Since layout changed, use the updated layout to find abr.
  auto header = sysconfig::SyncClientAbrWearLeveling::GetAbrWearLevelingSupportedLayout();
  auto actual = GetAbrInWearLeveling(header, 0);
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));

  {
    auto result_sync = boot_manager_->Flush();
    ASSERT_OK(result_sync.status());
    ASSERT_OK(result_sync->status);
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
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  fake_svc_.fake_boot_args().SetAstroSysConfigBufferedClient(true);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  ::llcpp::fuchsia::paver::Configuration configs[] = {
      ::llcpp::fuchsia::paver::Configuration::A, ::llcpp::fuchsia::paver::Configuration::B,
      ::llcpp::fuchsia::paver::Configuration::RECOVERY};

  for (auto config : configs) {
    ::llcpp::fuchsia::mem::Buffer payload;
    CreatePayload(32, &payload);
    auto result = data_sink_->WriteAsset(
        config, ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA, std::move(payload));
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
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(2 * kPagesPerBlock, &payload);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  {
    auto result =
        data_sink_->WriteAsset(::llcpp::fuchsia::paver::Configuration::A,
                               ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload));
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    CreatePayload(2 * kPagesPerBlock, &payload);
    ValidateWritten(8, 2);
    ValidateUnwritten(10, 4);
  }
  {
    auto result =
        data_sink_->WriteAsset(::llcpp::fuchsia::paver::Configuration::A,
                               ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload));
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    ValidateWritten(8, 2);
    ValidateUnwritten(10, 4);
  }
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareConfigASupported) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(4 * kPagesPerBlock, &payload);
  auto result =
      data_sink_->WriteFirmware(::llcpp::fuchsia::paver::Configuration::A,
                                fidl::unowned_str(kFirmwareTypeBootloader), std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_status());
  ASSERT_OK(result->result.status());
  ValidateWritten(kBootloaderFirstBlock, 4);
  WriteData(kBootloaderFirstBlock, 4 * kPagesPerBlock, 0xff);
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareUnsupportedConfigBFallBackToA) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(4 * kPagesPerBlock, &payload);
  auto result =
      data_sink_->WriteFirmware(::llcpp::fuchsia::paver::Configuration::B,
                                fidl::unowned_str(kFirmwareTypeBootloader), std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_status());
  ASSERT_OK(result->result.status());
  ValidateWritten(kBootloaderFirstBlock, 4);
  WriteData(kBootloaderFirstBlock, 4 * kPagesPerBlock, 0xff);
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareUnsupportedConfigR) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(4 * kPagesPerBlock, &payload);
  auto result =
      data_sink_->WriteFirmware(::llcpp::fuchsia::paver::Configuration::RECOVERY,
                                fidl::unowned_str(kFirmwareTypeBootloader), std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_unsupported());
  ASSERT_TRUE(result->result.unsupported());
  ValidateUnwritten(kBootloaderFirstBlock, 4);
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareBl2ConfigASupported) {
  // BL2 special handling: we should always leave the first 4096 bytes intact.
  constexpr size_t kBl2StartByte = kBl2FirstBlock * kPageSize * kPagesPerBlock;
  constexpr size_t kBl2SkipLength = 4096;

  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  ASSERT_NO_FATAL_FAILURES(FindDataSink());

  WriteDataBytes(kBl2StartByte, kBl2SkipLength, 0xC6);
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(kBl2ImagePages, &payload);
  auto result = data_sink_->WriteFirmware(::llcpp::fuchsia::paver::Configuration::A,
                                          fidl::unowned_str(kFirmwareTypeBl2), std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_status());
  ASSERT_OK(result->result.status());
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareBl2UnsupportedConfigBFallBackToA) {
  // BL2 special handling: we should always leave the first 4096 bytes intact.
  constexpr size_t kBl2StartByte = kBl2FirstBlock * kPageSize * kPagesPerBlock;
  constexpr size_t kBl2SkipLength = 4096;

  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  WriteDataBytes(kBl2StartByte, kBl2SkipLength, 0xC6);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(kBl2ImagePages, &payload);
  auto result = data_sink_->WriteFirmware(::llcpp::fuchsia::paver::Configuration::B,
                                          fidl::unowned_str(kFirmwareTypeBl2), std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_status());
  ASSERT_OK(result->result.status());
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareBl2UnsupportedConfigR) {
  // BL2 special handling: we should always leave the first 4096 bytes intact.
  constexpr size_t kBl2StartByte = kBl2FirstBlock * kPageSize * kPagesPerBlock;
  constexpr size_t kBl2SkipLength = 4096;

  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());
  WriteDataBytes(kBl2StartByte, kBl2SkipLength, 0xC6);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(kBl2ImagePages, &payload);
  auto result = data_sink_->WriteFirmware(::llcpp::fuchsia::paver::Configuration::RECOVERY,
                                          fidl::unowned_str(kFirmwareTypeBl2), std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_unsupported());
  ASSERT_TRUE(result->result.unsupported());
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareUnsupportedType) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  constexpr ::llcpp::fuchsia::paver::Configuration kAllConfigs[] = {
      ::llcpp::fuchsia::paver::Configuration::A,
      ::llcpp::fuchsia::paver::Configuration::B,
      ::llcpp::fuchsia::paver::Configuration::RECOVERY,
  };

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  for (auto config : kAllConfigs) {
    ::llcpp::fuchsia::mem::Buffer payload;
    CreatePayload(4 * kPagesPerBlock, &payload);
    auto result = data_sink_->WriteFirmware(config, fidl::unowned_str(kFirmwareTypeUnsupported),
                                            std::move(payload));
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->result.is_unsupported());
    ASSERT_TRUE(result->result.unsupported());
    ValidateUnwritten(kBootloaderFirstBlock, 4);
    ValidateUnwritten(kBl2FirstBlock, 1);
  }
}

TEST_F(PaverServiceSkipBlockTest, WriteFirmwareError) {
  // Make a RAM NAND device without a visible "bootloader" partition so that
  // the partitioner initializes properly but then fails when trying to find it.
  fuchsia_hardware_nand_RamNandInfo info = kNandInfo;
  info.partition_map.partitions[1].hidden = true;
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand(info));

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(4 * kPagesPerBlock, &payload);
  auto result =
      data_sink_->WriteFirmware(::llcpp::fuchsia::paver::Configuration::A,
                                fidl::unowned_str(kFirmwareTypeBootloader), std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_status());
  ASSERT_NOT_OK(result->result.status());
  ValidateUnwritten(kBootloaderFirstBlock, 4);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetKernelConfigA) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  WriteData(8 * kPagesPerBlock, 2 * kPagesPerBlock, 0x4a);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->ReadAsset(::llcpp::fuchsia::paver::Configuration::A,
                                      ::llcpp::fuchsia::paver::Asset::KERNEL);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ValidateWritten(result->result.response().asset, 2 * kPagesPerBlock);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetKernelConfigB) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  WriteData(10 * kPagesPerBlock, 2 * kPagesPerBlock, 0x4a);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->ReadAsset(::llcpp::fuchsia::paver::Configuration::B,
                                      ::llcpp::fuchsia::paver::Asset::KERNEL);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ValidateWritten(result->result.response().asset, 2 * kPagesPerBlock);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetKernelConfigRecovery) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  WriteData(12 * kPagesPerBlock, 2 * kPagesPerBlock, 0x4a);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->ReadAsset(::llcpp::fuchsia::paver::Configuration::RECOVERY,
                                      ::llcpp::fuchsia::paver::Asset::KERNEL);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ValidateWritten(result->result.response().asset, 2 * kPagesPerBlock);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetVbMetaConfigA) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  WriteData(14 * kPagesPerBlock + 32, 32, 0x4a);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->ReadAsset(::llcpp::fuchsia::paver::Configuration::A,
                                      ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ValidateWritten(result->result.response().asset, 32);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetVbMetaConfigB) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  WriteData(14 * kPagesPerBlock + 64, 32, 0x4a);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->ReadAsset(::llcpp::fuchsia::paver::Configuration::B,
                                      ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ValidateWritten(result->result.response().asset, 32);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetVbMetaConfigRecovery) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  WriteData(14 * kPagesPerBlock + 96, 32, 0x4a);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->ReadAsset(::llcpp::fuchsia::paver::Configuration::RECOVERY,
                                      ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ValidateWritten(result->result.response().asset, 32);
}

TEST_F(PaverServiceSkipBlockTest, ReadAssetZbiSize) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  zbi_header_t container;
  container.type = ZBI_TYPE_CONTAINER;
  container.extra = ZBI_CONTAINER_MAGIC;
  container.magic = ZBI_ITEM_MAGIC;
  container.flags = ZBI_FLAG_VERSION;
  container.crc32 = ZBI_ITEM_NO_CRC32;
  container.length = sizeof(zbi_header_t);

  WriteDataBytes(8 * kPagesPerBlock * kPageSize, &container, sizeof(container));

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->ReadAsset(::llcpp::fuchsia::paver::Configuration::A,
                                      ::llcpp::fuchsia::paver::Asset::KERNEL);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_EQ(result->result.response().asset.size, sizeof(container));
}

TEST_F(PaverServiceSkipBlockTest, WriteBootloader) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(4 * kPagesPerBlock, &payload);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->WriteBootloader(std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWritten(4, 4);
}

// We prefill the bootloader partition with the expected data, leaving the last block as 0xFF.
// Normally the last page would be overwritten with 0s, but because the actual payload is identical,
// we don't actually pave the image, so the extra page stays as 0xFF.
TEST_F(PaverServiceSkipBlockTest, WriteBootloaderNotAligned) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(4 * kPagesPerBlock - 1, &payload);

  WriteData(4 * kPagesPerBlock, 4 * kPagesPerBlock - 1, 0x4a);
  WriteData(8 * kPagesPerBlock - 1, 1, 0xff);

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->WriteBootloader(std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWrittenPages(4 * kPagesPerBlock, 4 * kPagesPerBlock - 1);
  ValidateUnwrittenPages(8 * kPagesPerBlock - 1, 1);
}

TEST_F(PaverServiceSkipBlockTest, WriteDataFile) {
  // TODO(ZX-4007): Figure out a way to test this.
}

TEST_F(PaverServiceSkipBlockTest, WriteVolumes) {
  // TODO(ZX-4007): Figure out a way to test this.
}

TEST_F(PaverServiceSkipBlockTest, WipeVolumeEmptyFvm) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->WipeVolume();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_TRUE(result->result.response().volume);
}

void CheckGuid(const fbl::unique_fd& device, const uint8_t type[GPT_GUID_LEN]) {
  fdio_cpp::UnownedFdioCaller caller(device.get());
  auto result = partition::Partition::Call::GetTypeGuid(caller.channel());
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  auto* guid = result.value().guid.get();
  EXPECT_BYTES_EQ(type, guid, GPT_GUID_LEN);
}

TEST_F(PaverServiceSkipBlockTest, WipeVolumeCreatesFvm) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  constexpr size_t kBufferSize = 8192;
  char buffer[kBufferSize];
  memset(buffer, 'a', kBufferSize);
  EXPECT_EQ(kBufferSize, pwrite(fvm_.get(), buffer, kBufferSize, 0));

  ASSERT_NO_FATAL_FAILURES(FindDataSink());
  auto result = data_sink_->WipeVolume();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_TRUE(result->result.response().volume);

  EXPECT_EQ(kBufferSize, pread(fvm_.get(), buffer, kBufferSize, 0));
  EXPECT_BYTES_EQ(fvm_magic, buffer, sizeof(fvm_magic));

  zx::channel channel = std::move(result->result.mutable_response().volume);
  std::string path = GetTopologicalPath(channel);
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

  std::string data_path = path + "/minfs-p-2/block";
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
    ASSERT_TRUE(result->result.is_response());
    ASSERT_EQ(result->result.response().size, sysconfig_pages * kPageSize);
  }

  {
    ::llcpp::fuchsia::mem::Buffer payload;
    CreatePayload(sysconfig_pages, &payload);
    auto result = sysconfig_->Write(std::move(payload));
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    // Without flushing, data in the storage should remain unchanged.
    ASSERT_NO_FATAL_FAILURES(
        ValidateUnwrittenPages(14 * kPagesPerBlock + offset_in_pages, sysconfig_pages));
  }

  {
    auto result = sysconfig_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    ASSERT_NO_FATAL_FAILURES(
        ValidateWrittenPages(14 * kPagesPerBlock + offset_in_pages, sysconfig_pages));
  }

  {
    // Validate read.
    auto result = sysconfig_->Read();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->result.is_response());
    ASSERT_NO_FATAL_FAILURES(ValidateWritten(result->result.response().data, sysconfig_pages));
  }
}

TEST_F(PaverServiceSkipBlockTest, SysconfigWriteWithBufferredClientLayoutNotUpdated) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  // Enable write-caching + abr metadata wear-leveling
  fake_svc_.fake_boot_args().SetAstroSysConfigBufferedClient(true);
  fake_svc_.fake_boot_args().SetAstroSysConfigAbrWearLeveling(true);

  ASSERT_NO_FATAL_FAILURES(FindSysconfig());

  ASSERT_NO_FATAL_FAILURES(TestSysconfigWriteBufferedClient(0, 15 * 2));
}

TEST_F(PaverServiceSkipBlockTest, SysconfigWriteWithBufferredClientLayoutUpdated) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  // Enable write-caching + abr metadata wear-leveling
  fake_svc_.fake_boot_args().SetAstroSysConfigBufferedClient(true);
  fake_svc_.fake_boot_args().SetAstroSysConfigAbrWearLeveling(true);

  auto abr_data = GetAbrWearlevelingSupportingLayout();
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindSysconfig());

  ASSERT_NO_FATAL_FAILURES(TestSysconfigWriteBufferedClient(2, 5 * 2));
}

void PaverServiceSkipBlockTest::TestSysconfigWipeBufferedClient(uint32_t offset_in_pages,
                                                                uint32_t sysconfig_pages) {
  {
    auto result = sysconfig_->Wipe();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    // Without flushing, data in the storage should remain unchanged.
    ASSERT_NO_FATAL_FAILURES(
        ValidateUnwrittenPages(14 * kPagesPerBlock + offset_in_pages, sysconfig_pages));
  }

  {
    auto result = sysconfig_->Flush();
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    ASSERT_NO_FATAL_FAILURES(AssertContents(14 * kSkipBlockSize + offset_in_pages * kPageSize,
                                            sysconfig_pages * kPageSize, 0));
  }
}

TEST_F(PaverServiceSkipBlockTest, SysconfigWipeWithBufferredClientLayoutNotUpdated) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  // Enable write-caching + abr metadata wear-leveling
  fake_svc_.fake_boot_args().SetAstroSysConfigBufferedClient(true);
  fake_svc_.fake_boot_args().SetAstroSysConfigAbrWearLeveling(true);

  ASSERT_NO_FATAL_FAILURES(FindSysconfig());

  ASSERT_NO_FATAL_FAILURES(TestSysconfigWipeBufferedClient(0, 15 * 2));
}

TEST_F(PaverServiceSkipBlockTest, SysconfigWipeWithBufferredClientLayoutUpdated) {
  ASSERT_NO_FATAL_FAILURES(InitializeRamNand());

  // Enable write-caching + abr metadata wear-leveling
  fake_svc_.fake_boot_args().SetAstroSysConfigBufferedClient(true);
  fake_svc_.fake_boot_args().SetAstroSysConfigAbrWearLeveling(true);

  auto abr_data = GetAbrWearlevelingSupportingLayout();
  SetAbr(abr_data);

  ASSERT_NO_FATAL_FAILURES(FindSysconfig());

  ASSERT_NO_FATAL_FAILURES(TestSysconfigWipeBufferedClient(2, 5 * 2));
}

constexpr uint8_t kEmptyType[GPT_GUID_LEN] = GUID_EMPTY_VALUE;

#if defined(__x86_64__)
class PaverServiceBlockTest : public PaverServiceTest {
 public:
  PaverServiceBlockTest() { ASSERT_NO_FATAL_FAILURES(SpawnIsolatedDevmgr()); }

 protected:
  void SpawnIsolatedDevmgr() {
    devmgr_launcher::Args args;
    args.sys_device_driver = IsolatedDevmgr::kSysdevDriver;
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = false;
    args.path_prefix = "/pkg/";
    ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr_));

    // Forward the block watcher FIDL interface from the devmgr.
    fake_svc_.ForwardServiceTo(llcpp::fuchsia::fshost::BlockWatcher::Name,
                               devmgr_.fshost_outgoing_dir());

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
    static_cast<paver::Paver*>(provider_ctx_)->set_devfs_root(devmgr_.devfs_root().duplicate());
    static_cast<paver::Paver*>(provider_ctx_)->set_svc_root(std::move(fake_svc_.svc_chan()));
  }

  void UseBlockDevice(zx::channel block_device) {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));

    auto result = client_->UseBlockDevice(std::move(block_device), std::move(remote));
    ASSERT_OK(result.status());
    data_sink_.emplace(std::move(local));
  }

  IsolatedDevmgr devmgr_;
  std::optional<::llcpp::fuchsia::paver::DynamicDataSink::SyncClient> data_sink_;
};

TEST_F(PaverServiceBlockTest, DISABLED_InitializePartitionTables) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // 32GiB disk.
  constexpr uint64_t block_count = (32LU << 30) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count, &gpt_dev));

  zx::channel gpt_chan;
  ASSERT_OK(fdio_fd_clone(gpt_dev->fd(), gpt_chan.reset_and_get_address()));

  ASSERT_NO_FATAL_FAILURES(UseBlockDevice(std::move(gpt_chan)));

  auto result = data_sink_->InitializePartitionTables();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
}

TEST_F(PaverServiceBlockTest, DISABLED_InitializePartitionTablesMultipleDevices) {
  std::unique_ptr<BlockDevice> gpt_dev1, gpt_dev2;
  // 32GiB disk.
  constexpr uint64_t block_count = (32LU << 30) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count, &gpt_dev1));
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count, &gpt_dev2));

  zx::channel gpt_chan;
  ASSERT_OK(fdio_fd_clone(gpt_dev1->fd(), gpt_chan.reset_and_get_address()));

  ASSERT_NO_FATAL_FAILURES(UseBlockDevice(std::move(gpt_chan)));

  auto result = data_sink_->InitializePartitionTables();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
}

TEST_F(PaverServiceBlockTest, DISABLED_WipePartitionTables) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // 32GiB disk.
  constexpr uint64_t block_count = (32LU << 30) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count, &gpt_dev));

  zx::channel gpt_chan;
  ASSERT_OK(fdio_fd_clone(gpt_dev->fd(), gpt_chan.reset_and_get_address()));

  ASSERT_NO_FATAL_FAILURES(UseBlockDevice(std::move(gpt_chan)));

  auto result = data_sink_->InitializePartitionTables();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);

  auto wipe_result = data_sink_->WipePartitionTables();
  ASSERT_OK(wipe_result.status());
  ASSERT_OK(wipe_result->status);
}

TEST_F(PaverServiceBlockTest, DISABLED_WipeVolume) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // 32GiB disk.
  constexpr uint64_t block_count = (32LU << 30) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count, &gpt_dev));

  zx::channel gpt_chan;
  ASSERT_OK(fdio_fd_clone(gpt_dev->fd(), gpt_chan.reset_and_get_address()));

  ASSERT_NO_FATAL_FAILURES(UseBlockDevice(std::move(gpt_chan)));

  auto result = data_sink_->InitializePartitionTables();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);

  auto wipe_result = data_sink_->WipeVolume();
  ASSERT_OK(wipe_result.status());
  ASSERT_FALSE(wipe_result->result.is_err());
}
#endif

class PaverServiceGptDeviceTest : public PaverServiceTest {
 protected:
  void SpawnIsolatedDevmgr(const char* board_name) {
    driver_integration_test::IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = false;
    args.path_prefix = "/pkg/";
    args.board_name = board_name;
    ASSERT_OK(driver_integration_test::IsolatedDevmgr::Create(&args, &devmgr_));

    // Forward the block watcher FIDL interface from the devmgr.
    fake_svc_.ForwardServiceTo(llcpp::fuchsia::fshost::BlockWatcher::Name,
                               devmgr_.fshost_outgoing_dir());

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
    static_cast<paver::Paver*>(provider_ctx_)->set_dispatcher(loop_.dispatcher());
    static_cast<paver::Paver*>(provider_ctx_)->set_devfs_root(devmgr_.devfs_root().duplicate());
    zx::channel svc_root = GetSvcRoot();
    static_cast<paver::Paver*>(provider_ctx_)->set_svc_root(std::move(svc_root));
  }

  void InitializeGptDevice(const char* board_name, uint64_t block_count, uint32_t block_size) {
    SpawnIsolatedDevmgr(board_name);
    block_count_ = block_count;
    block_size_ = block_size;
    ASSERT_NO_FATAL_FAILURES(
        BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count, block_size, &gpt_dev_));
  }

  zx::channel GetSvcRoot() { return zx::channel(fdio_service_clone(fake_svc_.svc_chan().get())); }

  struct PartitionDescription {
    const char* name;
    const uint8_t* type;
    uint64_t start;
    uint64_t length;
  };

  void InitializeStartingGPTPartitions(const std::vector<PartitionDescription>& init_partitions) {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_OK(gpt::GptDevice::Create(gpt_dev_->fd(), gpt_dev_->block_size(),
                                     gpt_dev_->block_count(), &gpt));
    ASSERT_OK(gpt->Sync());

    for (const auto& part : init_partitions) {
      ASSERT_OK(
          gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
          "%s", part.name);
    }

    ASSERT_OK(gpt->Sync());

    fdio_cpp::UnownedFdioCaller caller(gpt_dev_->fd());
    auto result = ::llcpp::fuchsia::device::Controller::Call::Rebind(
        caller.channel(), fidl::StringView("/boot/driver/gpt.so"));
    ASSERT_TRUE(result.ok());
    ASSERT_FALSE(result->result.is_err());
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
  PaverServiceLuisTest() { ASSERT_NO_FATAL_FAILURES(InitializeGptDevice("luis", 0x748034, 512)); }

  void InitializeLuisGPTPartitions() {
    constexpr uint8_t kDummyType[GPT_GUID_LEN] = {0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
                                                  0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4};
    const std::vector<PartitionDescription> kLuisStartingPartitions = {
        {GPT_DURABLE_BOOT_NAME, kDummyType, 0x10400, 0x10000},
    };
    ASSERT_NO_FATAL_FAILURES(InitializeStartingGPTPartitions(kLuisStartingPartitions));
  }
};

TEST_F(PaverServiceLuisTest, CreateAbr) {
  ASSERT_NO_FATAL_FAILURES(InitializeLuisGPTPartitions());
  std::shared_ptr<paver::Context> context;
  zx::channel svc_root = GetSvcRoot();
  EXPECT_OK(
      abr::ClientFactory::Create(devmgr_.devfs_root().duplicate(), std::move(svc_root), context));
}

TEST_F(PaverServiceLuisTest, SysconfigNotSupportedAndFailWithPeerClosed) {
  ASSERT_NO_FATAL_FAILURES(InitializeLuisGPTPartitions());
  zx::channel sysconfig_local, sysconfig_remote;
  ASSERT_OK(zx::channel::create(0, &sysconfig_local, &sysconfig_remote));
  auto result = client_->FindSysconfig(std::move(sysconfig_remote));
  ASSERT_OK(result.status());

  ::llcpp::fuchsia::paver::Sysconfig::SyncClient sysconfig(std::move(sysconfig_local));
  auto wipe_result = sysconfig.Wipe();
  ASSERT_EQ(wipe_result.status(), ZX_ERR_PEER_CLOSED);
}

}  // namespace
