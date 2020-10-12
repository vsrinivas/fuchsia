// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConfigurationManager.h>
#include <Weave/Core/WeaveKeyIds.h>
#include "src/connectivity/weave/adaptation/configuration_manager_impl.h"
#include <Weave/Profiles/security/WeaveApplicationKeys.h>

#include <Weave/Core/WeaveVendorIdentifiers.hpp>
#include <Weave/DeviceLayer/internal/GenericConfigurationManagerImpl.ipp>
// clang-format on

#include <fuchsia/factory/cpp/fidl_test_base.h>
#include <fuchsia/hwinfo/cpp/fidl_test_base.h>
#include <fuchsia/io/cpp/fidl_test_base.h>
#include <fuchsia/weave/cpp/fidl_test_base.h>

#include <lib/syslog/cpp/macros.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/vmo_file.h>
#include <net/ethernet.h>

#include "src/lib/files/file.h"
#include "src/lib/fsl/vmo/strings.h"
#include "configuration_manager_delegate_impl.h"
#include "thread_stack_manager_delegate_impl.h"
#include "weave_test_fixture.h"

namespace nl::Weave::DeviceLayer::Internal {
namespace testing {
namespace {

using nl::Weave::DeviceLayer::ConfigurationManager;
using nl::Weave::DeviceLayer::ConfigurationManagerImpl;
using nl::Weave::DeviceLayer::Internal::EnvironmentConfig;
using nl::Weave::Profiles::DeviceDescription::WeaveDeviceDescriptor;

// Below expected values are from testdata JSON files and should be
// consistent with the file for the related tests to pass.
constexpr uint16_t kExpectedVendorId = 5050;
constexpr uint16_t kExpectedProductId = 60209;
constexpr uint64_t kExpectedDeviceId = 65535;
constexpr char kExpectedFirmwareRevision[] = "prerelease-1";
constexpr char kExpectedSerialNumber[] = "dummy_serial_number";
constexpr char kExpectedPairingCode[] = "PAIRDUMMY123";
constexpr uint16_t kMaxFirmwareRevisionSize = ConfigurationManager::kMaxFirmwareRevisionLength + 1;
constexpr uint16_t kMaxSerialNumberSize = ConfigurationManager::kMaxSerialNumberLength + 1;
constexpr uint16_t kMaxPairingCodeSize = ConfigurationManager::kMaxPairingCodeLength + 1;
const std::string kPkgDataPath = "/pkg/data/";
const std::string kDataPath = "/data/";

}  // namespace

// This fake class hosts device protocol in the backgroud thread
class FakeHwinfo : public fuchsia::hwinfo::testing::Device_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << __func__; }

  void GetInfo(fuchsia::hwinfo::Device::GetInfoCallback callback) override {
    fuchsia::hwinfo::DeviceInfo device_info;
    if (!disable_serial) {
      device_info.set_serial_number(kExpectedSerialNumber);
    }
    callback(std::move(device_info));
  }

  fidl::InterfaceRequestHandler<fuchsia::hwinfo::Device> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    dispatcher_ = dispatcher;
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::hwinfo::Device> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  void DisableSerialNum() {
    disable_serial = true;
  }

 private:
  fidl::Binding<fuchsia::hwinfo::Device> binding_{this};
  async_dispatcher_t* dispatcher_;
  bool disable_serial = false;
};

class FakeWeaveFactoryDataManager : public fuchsia::weave::testing::FactoryDataManager_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << __func__; }

  void GetPairingCode(GetPairingCodeCallback callback) override {
    constexpr char device_pairing_code[] = "PAIRCODE123";
    fuchsia::weave::FactoryDataManager_GetPairingCode_Result result;
    fuchsia::weave::FactoryDataManager_GetPairingCode_Response response((::std::vector<uint8_t>(
        std::begin(device_pairing_code), std::end(device_pairing_code) - 1)));
    result.set_response(response);
    callback(std::move(result));
  }

  fidl::InterfaceRequestHandler<fuchsia::weave::FactoryDataManager> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    dispatcher_ = dispatcher;
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::weave::FactoryDataManager> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

 private:
  fidl::Binding<fuchsia::weave::FactoryDataManager> binding_{this};
  async_dispatcher_t* dispatcher_;
};

class FakeDirectory {
 public:
  FakeDirectory() { root_ = std::make_unique<vfs::PseudoDir>(); }
  zx_status_t AddResource(std::string filename, const std::string& data) {
    return root_->AddEntry(filename, CreateVmoFile(data));
  }
  void Serve(fidl::InterfaceRequest<fuchsia::io::Directory> channel,
             async_dispatcher_t* dispatcher) {
    root_->Serve(fuchsia::io::OPEN_FLAG_DIRECTORY | fuchsia::io::OPEN_RIGHT_READABLE |
                     fuchsia::io::OPEN_FLAG_DESCRIBE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                 channel.TakeChannel(), dispatcher);
  }

 private:
  std::unique_ptr<vfs::VmoFile> CreateVmoFile(const std::string& data) {
    fsl::SizedVmo test_vmo;
    if (!VmoFromString(data, &test_vmo)) {
      return nullptr;
    }
    return std::make_unique<vfs::VmoFile>(zx::unowned_vmo(test_vmo.vmo()), 0, data.size(),
                                          vfs::VmoFile::WriteOption::WRITABLE,
                                          vfs::VmoFile::Sharing::CLONE_COW);
  }

 protected:
  std::unique_ptr<vfs::PseudoDir> root_;
};

class FakeWeaveFactoryStoreProvider
    : public fuchsia::factory::testing::WeaveFactoryStoreProvider_TestBase {
 public:
  FakeWeaveFactoryStoreProvider() = default;

  fidl::InterfaceRequestHandler<fuchsia::factory::WeaveFactoryStoreProvider> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    dispatcher_ = dispatcher;
    return [this, dispatcher](
               fidl::InterfaceRequest<fuchsia::factory::WeaveFactoryStoreProvider> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  ~FakeWeaveFactoryStoreProvider() override = default;
  FakeWeaveFactoryStoreProvider(const FakeWeaveFactoryStoreProvider&) = delete;
  FakeWeaveFactoryStoreProvider& operator=(const FakeWeaveFactoryStoreProvider&) = delete;

  void AttachDir(std::unique_ptr<FakeDirectory> fake_dir) { fake_dir_ = std::move(fake_dir); }

  void GetFactoryStore(::fidl::InterfaceRequest<::fuchsia::io::Directory> dir) override {
    if (!fake_dir_) {
      ADD_FAILURE();
    }

    fake_dir_->Serve(std::move(dir), dispatcher_);
  }

  void NotImplemented_(const std::string& name) final { ADD_FAILURE(); };

 private:
  fidl::Binding<fuchsia::factory::WeaveFactoryStoreProvider> binding_{this};
  std::unique_ptr<FakeDirectory> fake_dir_;
  async_dispatcher_t* dispatcher_;
};

class ConfigurationManagerTestDelegateImpl : public ConfigurationManagerDelegateImpl {
 public:
  zx_status_t ReadFactoryFile(const char* path, char* buf, size_t buf_size, size_t* out_len) {
    return ConfigurationManagerDelegateImpl::ReadFactoryFile(path, buf, buf_size, out_len);
  }
};

// Configuration manager delegate used to test IsFullyProvisioned
class CfgMgrProvisionStatusDelegate : public ConfigurationManagerTestDelegateImpl {
 public:
  bool IsPairedToAccount() override {
    return is_paired_to_account_;
  }

  bool IsMemberOfFabric() override {
    return is_member_of_fabric_;
  }

  CfgMgrProvisionStatusDelegate& SetPairedToAccount(bool value) {
    is_paired_to_account_ = value;
    return *this;
  }

  CfgMgrProvisionStatusDelegate& SetMemberOfFabric(bool value) {
    is_member_of_fabric_ = value;
    return *this;
  }
 private:
  bool is_paired_to_account_;
  bool is_member_of_fabric_;
};

// Thread stack delegate used to test IsFullyProvisioned
class ThreadStackMgrProvisionStatusDelegate : public ThreadStackManagerDelegateImpl {
 public:
  bool IsThreadProvisioned() {
    return is_thread_provisioned_;
  }

  ThreadStackMgrProvisionStatusDelegate& SetThreadProvisioned(bool value) {
    is_thread_provisioned_ = value;
    return *this;
  }
 private:
  bool is_thread_provisioned_;
};

class ConfigurationManagerTest : public WeaveTestFixture {
 public:
  ConfigurationManagerTest() {
    context_provider_.service_directory_provider()->AddService(
        fake_hwinfo_.GetHandler(dispatcher()));
    context_provider_.service_directory_provider()->AddService(
        fake_weave_factory_data_manager_.GetHandler(dispatcher()));
    context_provider_.service_directory_provider()->AddService(
        fake_weave_factory_store_provider_.GetHandler(dispatcher()));
  }

  void SetUp() {
    WeaveTestFixture::SetUp();
    WeaveTestFixture::RunFixtureLoop();
    PlatformMgrImpl().SetComponentContextForProcess(context_provider_.TakeContext());
    ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerDelegateImpl>());
    ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
    EXPECT_EQ(ConfigurationMgrImpl().GetDelegate()->Init(), WEAVE_NO_ERROR);
  }

  void TearDown() {
    WeaveTestFixture::StopFixtureLoop();
    WeaveTestFixture::TearDown();
  }

  bool CopyFileFromPkgToData(std::string filename) {
    std::string data;
    bool result = files::ReadFileToString(kPkgDataPath + filename, &data);
    int e = errno;
    if (!result) {
      FX_LOGS(ERROR) << "readfile failed for " << filename << ": " << strerror(e);
      return result;
    }
    result = files::WriteFile(kDataPath + filename, data);
    e = errno;
    if (!result) {
      FX_LOGS(ERROR) << "writefile failed for  " << filename << ": " << strerror(e);
      return result;
    }
    return result;
  }

  void DisableHwInfoSerialNum() {
    fake_hwinfo_.DisableSerialNum();
  }

 protected:
  FakeHwinfo fake_hwinfo_;
  FakeWeaveFactoryDataManager fake_weave_factory_data_manager_;
  FakeWeaveFactoryStoreProvider fake_weave_factory_store_provider_;

 private:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<sys::ComponentContext> context_;
};

TEST_F(ConfigurationManagerTest, SetAndGetFabricId) {
  const uint64_t fabric_id = 123456789U;
  uint64_t stored_fabric_id = 0U;
  EXPECT_EQ(ConfigurationMgr().StoreFabricId(fabric_id), WEAVE_NO_ERROR);
  EXPECT_EQ(ConfigurationMgr().GetFabricId(stored_fabric_id), WEAVE_NO_ERROR);
  EXPECT_EQ(stored_fabric_id, fabric_id);
}

TEST_F(ConfigurationManagerTest, GetDeviceId) {
  uint64_t device_id = 0U;
  EXPECT_EQ(ConfigurationMgr().GetDeviceId(device_id), WEAVE_NO_ERROR);
  EXPECT_EQ(device_id, kExpectedDeviceId);
}

TEST_F(ConfigurationManagerTest, GetVendorId) {
  uint16_t vendor_id;
  EXPECT_EQ(ConfigurationMgr().GetVendorId(vendor_id), WEAVE_NO_ERROR);
  EXPECT_EQ(vendor_id, kExpectedVendorId);
}

TEST_F(ConfigurationManagerTest, GetProductId) {
  uint16_t product_id;
  EXPECT_EQ(ConfigurationMgr().GetProductId(product_id), WEAVE_NO_ERROR);
  EXPECT_EQ(product_id, kExpectedProductId);
}

TEST_F(ConfigurationManagerTest, GetFirmwareRevision) {
  char firmware_revision[kMaxFirmwareRevisionSize];
  size_t out_len;
  EXPECT_EQ(
      ConfigurationMgr().GetFirmwareRevision(firmware_revision, sizeof(firmware_revision), out_len),
      WEAVE_NO_ERROR);
  EXPECT_EQ(strncmp(firmware_revision, kExpectedFirmwareRevision, out_len), 0);
}

TEST_F(ConfigurationManagerTest, GetSerialNumber) {
  char serial_num[kMaxSerialNumberSize];
  size_t serial_num_len;
  EXPECT_EQ(ConfigurationMgr().GetSerialNumber(serial_num, sizeof(serial_num), serial_num_len),
            WEAVE_NO_ERROR);
  EXPECT_STREQ(serial_num, kExpectedSerialNumber);
}

TEST_F(ConfigurationManagerTest, GetDeviceDescriptor) {
  ::nl::Weave::Profiles::DeviceDescription::WeaveDeviceDescriptor device_desc;
  EXPECT_EQ(ConfigurationMgr().GetDeviceDescriptor(device_desc), WEAVE_NO_ERROR);

  EXPECT_STREQ(device_desc.SerialNumber, kExpectedSerialNumber);
  EXPECT_EQ(device_desc.ProductId, kExpectedProductId);
  EXPECT_EQ(device_desc.VendorId, kExpectedVendorId);
}

TEST_F(ConfigurationManagerTest, GetPairingCode) {
  char pairing_code[kMaxPairingCodeSize];
  size_t pairing_code_len;
  EXPECT_EQ(ConfigurationMgr().GetPairingCode(pairing_code, sizeof(pairing_code), pairing_code_len),
            WEAVE_NO_ERROR);
  EXPECT_EQ(pairing_code_len,
            strnlen(kExpectedPairingCode, WeaveDeviceDescriptor::kMaxPairingCodeLength) + 1);
  EXPECT_STREQ(pairing_code, kExpectedPairingCode);
}

TEST_F(ConfigurationManagerTest, ReadFactoryFile) {
  constexpr size_t kBufSize = 32;
  constexpr char kFilename[] = "test_file";
  const std::string data = "test_file_contents";
  char buf[kBufSize] = {};
  size_t out_len;

  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerTestDelegateImpl>());

  ConfigurationManagerTestDelegateImpl* delegate =
      (ConfigurationManagerTestDelegateImpl*)ConfigurationMgrImpl().GetDelegate();
  EXPECT_EQ(delegate->Init(), WEAVE_NO_ERROR);

  auto fake_dir = std::make_unique<FakeDirectory>();
  EXPECT_EQ(ZX_OK, fake_dir->AddResource(kFilename, data));
  fake_weave_factory_store_provider_.AttachDir(std::move(fake_dir));

  EXPECT_EQ(delegate->ReadFactoryFile(kFilename, buf, kBufSize, &out_len), ZX_OK);

  EXPECT_EQ(out_len, data.length());
  EXPECT_EQ(std::string(buf, out_len), data);
}

TEST_F(ConfigurationManagerTest, ReadFactoryFileLargerThanExpected) {
  constexpr size_t kBufSize = 16;
  constexpr char kFilename[] = "test_file";
  const std::string data = "test_file_contents -- test_file_contents";
  char buf[kBufSize] = {};
  size_t out_len;

  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerTestDelegateImpl>());

  ConfigurationManagerTestDelegateImpl* delegate =
      (ConfigurationManagerTestDelegateImpl*)ConfigurationMgrImpl().GetDelegate();
  EXPECT_EQ(delegate->Init(), WEAVE_NO_ERROR);

  auto fake_dir = std::make_unique<FakeDirectory>();
  EXPECT_EQ(ZX_OK, fake_dir->AddResource(kFilename, data));
  fake_weave_factory_store_provider_.AttachDir(std::move(fake_dir));

  EXPECT_EQ(delegate->ReadFactoryFile(kFilename, buf, kBufSize, &out_len), ZX_ERR_BUFFER_TOO_SMALL);
}

TEST_F(ConfigurationManagerTest, SetAndGetDeviceId) {
  const std::string test_device_id_file("test_device_id");
  const std::string test_device_id_data("1234ABCD");
  uint64_t stored_weave_device_id = 0;

  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);

  auto fake_dir = std::make_unique<FakeDirectory>();
  EXPECT_EQ(ZX_OK, fake_dir->AddResource(test_device_id_file, test_device_id_data));
  fake_weave_factory_store_provider_.AttachDir(std::move(fake_dir));
  EXPECT_EQ(ConfigurationMgr().GetDeviceId(stored_weave_device_id), WEAVE_NO_ERROR);
  EXPECT_EQ(stored_weave_device_id, strtoull(test_device_id_data.c_str(), NULL, 16));

  // Show that even if the file is modified, it doesn't affect us as we read from
  // factory only once.
  stored_weave_device_id = 0;
  auto fake_dir2 = std::make_unique<FakeDirectory>();
  fake_weave_factory_store_provider_.AttachDir(std::move(fake_dir2));
  EXPECT_EQ(ConfigurationMgr().GetDeviceId(stored_weave_device_id), WEAVE_NO_ERROR);
  EXPECT_EQ(stored_weave_device_id, strtoull(test_device_id_data.c_str(), NULL, 16));
}

TEST_F(ConfigurationManagerTest, GetManufacturerDeviceCertificate) {
  constexpr char test_mfr_cert_file[] = "test_mfr_cert";
  const std::string test_mfr_cert_data("====Fake Certificate Data====");
  uint8_t mfr_cert_buf[UINT16_MAX] = {0};
  size_t cert_len;

  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);

  EXPECT_EQ(EnvironmentConfig::WriteConfigValue(EnvironmentConfig::kConfigKey_MfrDeviceCertAllowLocal, false), WEAVE_NO_ERROR);
  auto fake_dir = std::make_unique<FakeDirectory>();
  EXPECT_EQ(ZX_OK, fake_dir->AddResource(test_mfr_cert_file, test_mfr_cert_data));
  fake_weave_factory_store_provider_.AttachDir(std::move(fake_dir));
  EXPECT_EQ(ConfigurationMgr().GetManufacturerDeviceCertificate(mfr_cert_buf, sizeof(mfr_cert_buf),
                                                                cert_len),
            WEAVE_NO_ERROR);
  EXPECT_EQ(cert_len, test_mfr_cert_data.size());
  EXPECT_TRUE(std::equal(mfr_cert_buf, mfr_cert_buf + std::min(cert_len, sizeof(mfr_cert_buf)),
                         test_mfr_cert_data.begin(), test_mfr_cert_data.end()));

  // Show that after being read in once, modifying the  data has no effect
  std::memset(mfr_cert_buf, 0, sizeof(mfr_cert_buf));
  auto fake_dir2 = std::make_unique<FakeDirectory>();
  fake_weave_factory_store_provider_.AttachDir(std::move(fake_dir2));
  EXPECT_EQ(ConfigurationMgr().GetManufacturerDeviceCertificate(mfr_cert_buf, sizeof(mfr_cert_buf),
                                                                cert_len),
            WEAVE_NO_ERROR);
  EXPECT_EQ(cert_len, test_mfr_cert_data.size());
  EXPECT_TRUE(std::equal(mfr_cert_buf, mfr_cert_buf + std::min(cert_len, sizeof(mfr_cert_buf)),
                         test_mfr_cert_data.begin(), test_mfr_cert_data.end()));
}

TEST_F(ConfigurationManagerTest, CacheFlagsOnInit) {
  constexpr uint64_t kFabricId = 0;
  constexpr uint8_t kServiceConfig[] = {0};
  constexpr char kAccountId[] = "account-id";

  // Ensure that all service provisioning flags are off by default.
  EXPECT_FALSE(ConfigurationMgr().IsServiceProvisioned());
  EXPECT_FALSE(ConfigurationMgr().IsMemberOfFabric());
  EXPECT_FALSE(ConfigurationMgr().IsPairedToAccount());

  // Directly inject the service configuration data to the config. Don't use the
  // APIs, which wouldn't be called on a fresh init.
  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
  EXPECT_EQ(EnvironmentConfig::WriteConfigValue(EnvironmentConfig::kConfigKey_FabricId, kFabricId),
            WEAVE_NO_ERROR);
  EXPECT_EQ(EnvironmentConfig::WriteConfigValueBin(EnvironmentConfig::kConfigKey_ServiceConfig,
                                                   kServiceConfig, sizeof(kServiceConfig)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(EnvironmentConfig::WriteConfigValueStr(EnvironmentConfig::kConfigKey_PairedAccountId,
                                                   kAccountId, sizeof(kAccountId)),
            WEAVE_NO_ERROR);

  // Ensure that service provisioning flags are still off.
  EXPECT_FALSE(ConfigurationMgr().IsServiceProvisioned());
  EXPECT_FALSE(ConfigurationMgr().IsMemberOfFabric());
  EXPECT_FALSE(ConfigurationMgr().IsPairedToAccount());

  // Re-initialize the configuration manager and check that the flags are set.
  EXPECT_EQ(ConfigurationMgrImpl().GetDelegate()->Init(), WEAVE_NO_ERROR);
  EXPECT_TRUE(ConfigurationMgr().IsServiceProvisioned());
  EXPECT_TRUE(ConfigurationMgr().IsMemberOfFabric());
  EXPECT_TRUE(ConfigurationMgr().IsPairedToAccount());
}

TEST_F(ConfigurationManagerTest, IsFullyProvisioned) {
  auto cfg_mgr = new CfgMgrProvisionStatusDelegate();
  ConfigurationMgrImpl().SetDelegate(std::unique_ptr<ConfigurationManagerImpl::Delegate>(cfg_mgr));
  ASSERT_EQ(cfg_mgr->Init(), WEAVE_NO_ERROR);

  auto thread_mgr = new ThreadStackMgrProvisionStatusDelegate();
  ThreadStackMgrImpl().SetDelegate(std::unique_ptr<ThreadStackManagerImpl::Delegate>(thread_mgr));

  // All false
  cfg_mgr->SetPairedToAccount(false);
  cfg_mgr->SetMemberOfFabric(false);
  thread_mgr->SetThreadProvisioned(false);

  EXPECT_FALSE(ConfigurationMgr().IsFullyProvisioned());

  // Two false
  cfg_mgr->SetPairedToAccount(true);
  cfg_mgr->SetMemberOfFabric(false);
  thread_mgr->SetThreadProvisioned(false);

  EXPECT_FALSE(ConfigurationMgr().IsFullyProvisioned());

  cfg_mgr->SetPairedToAccount(false);
  cfg_mgr->SetMemberOfFabric(true);
  thread_mgr->SetThreadProvisioned(false);

  EXPECT_FALSE(ConfigurationMgr().IsFullyProvisioned());

  cfg_mgr->SetPairedToAccount(false);
  cfg_mgr->SetMemberOfFabric(false);
  thread_mgr->SetThreadProvisioned(true);

  EXPECT_FALSE(ConfigurationMgr().IsFullyProvisioned());

  // One false
  cfg_mgr->SetPairedToAccount(false);
  cfg_mgr->SetMemberOfFabric(true);
  thread_mgr->SetThreadProvisioned(true);

  EXPECT_FALSE(ConfigurationMgr().IsFullyProvisioned());

  cfg_mgr->SetPairedToAccount(true);
  cfg_mgr->SetMemberOfFabric(false);
  thread_mgr->SetThreadProvisioned(true);

  EXPECT_FALSE(ConfigurationMgr().IsFullyProvisioned());

  cfg_mgr->SetPairedToAccount(true);
  cfg_mgr->SetMemberOfFabric(true);
  thread_mgr->SetThreadProvisioned(false);

  // TODO(fxbug.dev/58252) Change to EXPECT_FALSE once ThreadStackManager is ready
  EXPECT_TRUE(ConfigurationMgr().IsFullyProvisioned());

  // No false
  cfg_mgr->SetPairedToAccount(true);
  cfg_mgr->SetMemberOfFabric(true);
  thread_mgr->SetThreadProvisioned(true);

  EXPECT_TRUE(ConfigurationMgr().IsFullyProvisioned());
}

TEST_F(ConfigurationManagerTest, GetPrivateKey) {
  std::vector<uint8_t> signing_key;
  std::string expected("ABC123\n");
  std::string filename("test_mfr_private_key");

  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);

  CopyFileFromPkgToData(filename);

  EXPECT_EQ(ConfigurationMgrImpl().GetPrivateKeyForSigning(&signing_key), ZX_OK);
  EXPECT_TRUE(std::equal(signing_key.begin(), signing_key.end(), expected.begin(), expected.end()));

  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
}

TEST_F(ConfigurationManagerTest, GetTestCert) {
  std::string testdata("FAKECERT\n");
  uint8_t mfr_cert_buf[testdata.size()+1];
  size_t cert_len;

  memset(mfr_cert_buf, 0, sizeof(mfr_cert_buf));
  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
  std::string filename("test_mfr_cert");
  CopyFileFromPkgToData(filename);

  EXPECT_EQ(ConfigurationMgr().GetManufacturerDeviceCertificate(mfr_cert_buf, sizeof(mfr_cert_buf),
                                                                cert_len), WEAVE_NO_ERROR);
  EXPECT_EQ(cert_len, testdata.size());
  EXPECT_TRUE(std::equal(mfr_cert_buf, mfr_cert_buf + std::min(cert_len, sizeof(mfr_cert_buf)),
                         testdata.begin(), testdata.end()));
}

TEST_F(ConfigurationManagerTest, GetLocalSerialNumber) {
  char serial_num[kMaxSerialNumberSize];
  size_t serial_num_len;
  std::string expected_serial("ABCD1234");
  DisableHwInfoSerialNum();
  // Create a new context and set a new delegate so that
  // the disablehwinwoserialnum takes effect.
  sys::testing::ComponentContextProvider context_provider;
  context_provider.service_directory_provider()->AddService(
        fake_hwinfo_.GetHandler(dispatcher()));
  context_provider.service_directory_provider()->AddService(
        fake_weave_factory_data_manager_.GetHandler(dispatcher()));
  context_provider.service_directory_provider()->AddService(
        fake_weave_factory_store_provider_.GetHandler(dispatcher()));
  PlatformMgrImpl().SetComponentContextForProcess(context_provider.TakeContext());
  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
  EXPECT_EQ(ConfigurationMgrImpl().GetDelegate()->Init(), WEAVE_NO_ERROR);
  EXPECT_EQ(ConfigurationMgr().GetSerialNumber(serial_num, sizeof(serial_num), serial_num_len),
            WEAVE_NO_ERROR);
  EXPECT_STREQ(serial_num, expected_serial.c_str());
}

}  // namespace testing
}  // namespace nl::Weave::DeviceLayer::Internal
