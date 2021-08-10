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

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include "configuration_manager_delegate_impl.h"
#include "fake_buildinfo_provider.h"
#include "fake_factory_weave_factory_store_provider.h"
#include "fake_hwinfo_device.h"
#include "fake_hwinfo_product.h"
#include "fake_weave_factory_data_manager.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "test_configuration_manager.h"
#include "test_thread_stack_manager.h"
#include "weave_test_fixture.h"

namespace nl::Weave::DeviceLayer::Internal::testing {
namespace {

using weave::adaptation::testing::FakeBuildInfoProvider;
using weave::adaptation::testing::FakeDirectory;
using weave::adaptation::testing::FakeFactoryWeaveFactoryStoreProvider;
using weave::adaptation::testing::FakeHwinfoDevice;
using weave::adaptation::testing::FakeHwinfoProduct;
using weave::adaptation::testing::FakeWeaveFactoryDataManager;
using weave::adaptation::testing::TestConfigurationManager;
using weave::adaptation::testing::TestThreadStackManager;

using nl::Weave::WeaveKeyId;
using nl::Weave::DeviceLayer::ConfigurationManager;
using nl::Weave::DeviceLayer::ConfigurationManagerImpl;
using nl::Weave::DeviceLayer::Internal::EnvironmentConfig;
using nl::Weave::DeviceLayer::Internal::GroupKeyStoreImpl;
using nl::Weave::Profiles::DeviceDescription::WeaveDeviceDescriptor;
using nl::Weave::Profiles::Security::AppKeys::WeaveGroupKey;

// Below expected values are from testdata JSON files and should be
// consistent with the file for the related tests to pass.
constexpr uint16_t kExpectedVendorId = 5050;
constexpr char kExpectedVendorIdDescription[] = "Fuchsia Vendor";
constexpr uint16_t kExpectedProductId = 60209;
constexpr char kExpectedProductIdDescription[] = "Fuchsia Product";
constexpr uint64_t kExpectedDeviceId = 65535;
constexpr char kExpectedFirmwareRevision_DeviceInfo[] = "prerelease-1";
constexpr char kExpectedSerialNumberLocal[] = "ABCD1234";
constexpr char kExpectedPairingCode[] = "ABC123";
constexpr uint16_t kMaxSerialNumberSize = ConfigurationManager::kMaxSerialNumberLength + 1;
constexpr uint16_t kMaxPairingCodeSize = ConfigurationManager::kMaxPairingCodeLength + 1;
const std::string kPkgDataPath = "/pkg/data/";
const std::string kDataPath = "/data/";
const std::string kConfigDataPath = "/config/data/";

constexpr uint32_t kTestKeyId = WeaveKeyId::kFabricSecret + 1u;
constexpr uint8_t kWeaveAppGroupKeySize =
    nl::Weave::Profiles::Security::AppKeys::kWeaveAppGroupKeySize;

// The required size of a buffer supplied to GetPrimaryWiFiMACAddress.
constexpr size_t kWiFiMacAddressBufSize =
    sizeof(Profiles::DeviceDescription::WeaveDeviceDescriptor::PrimaryWiFiMACAddress);
// The required size of a buffer supplied to GetPrimary802154MACAddress.
constexpr size_t k802154MacAddressBufSize =
    sizeof(Profiles::DeviceDescription::WeaveDeviceDescriptor::Primary802154MACAddress);

WeaveGroupKey CreateGroupKey(uint32_t key_id, uint8_t key_byte = 0,
                             uint8_t key_len = kWeaveAppGroupKeySize, uint32_t start_time = 0) {
  WeaveGroupKey group_key{
      .KeyId = key_id,
      .KeyLen = key_len,
      .StartTime = start_time,
  };
  memset(group_key.Key, 0, sizeof(group_key.Key));
  memset(group_key.Key, key_byte, key_len);
  return group_key;
}

}  // namespace

class ConfigurationManagerTest : public WeaveTestFixture<> {
 public:
  ConfigurationManagerTest() {
    context_provider_.service_directory_provider()->AddService(
        fake_buildinfo_provider_.GetHandler(dispatcher()));
    context_provider_.service_directory_provider()->AddService(
        fake_hwinfo_device_.GetHandler(dispatcher()));
    context_provider_.service_directory_provider()->AddService(
        fake_hwinfo_product_.GetHandler(dispatcher()));
    context_provider_.service_directory_provider()->AddService(
        fake_weave_factory_data_manager_.GetHandler(dispatcher()));
    context_provider_.service_directory_provider()->AddService(
        fake_factory_weave_factory_store_provider_.GetHandler(dispatcher()));
  }

  void SetUp() override {
    WeaveTestFixture<>::SetUp();
    WeaveTestFixture<>::RunFixtureLoop();

    PlatformMgrImpl().SetComponentContextForProcess(context_provider_.TakeContext());
    ThreadStackMgrImpl().SetDelegate(std::make_unique<TestThreadStackManager>());
    ConfigurationMgrImpl().SetDelegate(std::make_unique<TestConfigurationManager>());

    // Enable thread for all tests, explicitly disabling them on the tests that
    // need to validate behavior when thread isn't supported.
    thread_delegate().set_is_thread_supported(true);
    EXPECT_EQ(delegate().Init(), WEAVE_NO_ERROR);
  }

  void TearDown() override {
    WeaveTestFixture<>::StopFixtureLoop();
    WeaveTestFixture<>::TearDown();
    ThreadStackMgrImpl().SetDelegate(nullptr);
    ConfigurationMgrImpl().SetDelegate(nullptr);
  }

  static bool CopyFile(const std::string& src, const std::string& dst) {
    std::string data;
    bool result = files::ReadFileToString(src, &data);
    int e = errno;
    if (!result) {
      FX_LOGS(ERROR) << "ReadFile failed for " << src << ": " << strerror(e);
      return result;
    }
    result = files::WriteFile(dst, data);
    e = errno;
    if (!result) {
      FX_LOGS(ERROR) << "WriteFile failed for  " << dst << ": " << strerror(e);
      return result;
    }
    return result;
  }

 protected:
  static bool CopyFileFromPkgToData(const std::string& filename) {
    return CopyFile(kPkgDataPath + filename, kDataPath + filename);
  }

  static bool CopyFileFromConfigToData(const std::string& filename) {
    return CopyFile(kConfigDataPath + filename, kDataPath + filename);
  }

  static bool CopyFileFromConfigToData(const std::string& src_filename,
                                       const std::string& dst_filename) {
    return CopyFile(kConfigDataPath + src_filename, kDataPath + dst_filename);
  }

  FakeBuildInfoProvider& fake_buildinfo_provider() { return fake_buildinfo_provider_; }
  FakeHwinfoDevice& fake_hwinfo_device() { return fake_hwinfo_device_; }
  FakeHwinfoProduct& fake_hwinfo_product() { return fake_hwinfo_product_; }

  FakeFactoryWeaveFactoryStoreProvider& fake_factory_weave_factory_store_provider() {
    return fake_factory_weave_factory_store_provider_;
  }

  FakeWeaveFactoryDataManager& fake_weave_factory_data_manager() {
    return fake_weave_factory_data_manager_;
  }

  TestConfigurationManager& delegate() {
    return *reinterpret_cast<TestConfigurationManager*>(ConfigurationMgrImpl().GetDelegate());
  }

  TestThreadStackManager& thread_delegate() {
    return *reinterpret_cast<TestThreadStackManager*>(ThreadStackMgrImpl().GetDelegate());
  }

 private:
  FakeBuildInfoProvider fake_buildinfo_provider_;
  FakeHwinfoDevice fake_hwinfo_device_;
  FakeHwinfoProduct fake_hwinfo_product_;
  FakeWeaveFactoryDataManager fake_weave_factory_data_manager_;
  FakeFactoryWeaveFactoryStoreProvider fake_factory_weave_factory_store_provider_;

  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(ConfigurationManagerTest, SetAndGetFabricId) {
  const uint64_t fabric_id = 123456789U;
  uint64_t stored_fabric_id = 0;
  EXPECT_EQ(ConfigurationMgr().StoreFabricId(fabric_id), WEAVE_NO_ERROR);
  EXPECT_EQ(ConfigurationMgr().GetFabricId(stored_fabric_id), WEAVE_NO_ERROR);
  EXPECT_EQ(stored_fabric_id, fabric_id);
}

TEST_F(ConfigurationManagerTest, GetDeviceId_DeviceInfo) {
  uint64_t device_id = 0;
  EXPECT_EQ(ConfigurationMgr().GetDeviceId(device_id), WEAVE_NO_ERROR);
  EXPECT_EQ(device_id, kExpectedDeviceId);
}

TEST_F(ConfigurationManagerTest, GetDeviceId_Factory) {
  constexpr char kFactoryDeviceIdFileName[] = "testdata_device_id";
  constexpr char kFactoryDeviceIdFileData[] = "1234ABCD";
  const uint64_t kFactoryDeviceId = strtoull(kFactoryDeviceIdFileData, nullptr, 16);
  uint64_t device_id = 0;

  // Erase the existing configuration to ensure the next read doesn't read a
  // cached value from the original initialization.
  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);

  // Set the file contents of the factory device ID.
  fake_factory_weave_factory_store_provider().directory().AddFile(kFactoryDeviceIdFileName,
                                                                  kFactoryDeviceIdFileData);

  // Confirm that the file contents match the expected ID.
  EXPECT_EQ(ConfigurationMgr().GetDeviceId(device_id), WEAVE_NO_ERROR);
  EXPECT_EQ(device_id, kFactoryDeviceId);

  // Remove the file contents, confirm that the cached ID is returned.
  fake_factory_weave_factory_store_provider().directory().RemoveFile(kFactoryDeviceIdFileName);

  // Confirm that the file contents match the expected ID.
  device_id = 0;
  EXPECT_EQ(ConfigurationMgr().GetDeviceId(device_id), WEAVE_NO_ERROR);
  EXPECT_EQ(device_id, kFactoryDeviceId);
}

TEST_F(ConfigurationManagerTest, GetVendorId) {
  uint16_t vendor_id = 0;
  EXPECT_EQ(ConfigurationMgr().GetVendorId(vendor_id), WEAVE_NO_ERROR);
  EXPECT_EQ(vendor_id, kExpectedVendorId);
}

TEST_F(ConfigurationManagerTest, GetVendorIdDescription) {
  char vendor_id_description[ConfigurationManager::kMaxVendorIdDescriptionLength] = {};
  size_t out_len = 0;
  EXPECT_EQ(ConfigurationMgr().GetVendorIdDescription(vendor_id_description,
                                                      sizeof(vendor_id_description), out_len),
            WEAVE_NO_ERROR);
  EXPECT_STREQ(vendor_id_description, kExpectedVendorIdDescription);
}

TEST_F(ConfigurationManagerTest, GetProductId) {
  uint16_t product_id = 0;
  EXPECT_EQ(ConfigurationMgr().GetProductId(product_id), WEAVE_NO_ERROR);
  EXPECT_EQ(product_id, kExpectedProductId);
}

TEST_F(ConfigurationManagerTest, GetProductIdDescription) {
  char product_id_description[ConfigurationManager::kMaxProductIdDescriptionLength] = {};
  size_t out_len = 0;
  EXPECT_EQ(ConfigurationMgr().GetProductIdDescription(product_id_description,
                                                       sizeof(product_id_description), out_len),
            WEAVE_NO_ERROR);
  EXPECT_STREQ(product_id_description, kExpectedProductIdDescription);
}

TEST_F(ConfigurationManagerTest, GetFirmwareRevision_BuildInfo) {
  char firmware_revision[ConfigurationManager::kMaxFirmwareRevisionLength] = {};
  size_t out_len = 0;
  EXPECT_EQ(
      ConfigurationMgr().GetFirmwareRevision(firmware_revision, sizeof(firmware_revision), out_len),
      WEAVE_NO_ERROR);
  EXPECT_STREQ(firmware_revision, FakeBuildInfoProvider::kVersion);
}

TEST_F(ConfigurationManagerTest, GetFirmwareRevision_DeviceInfo) {
  char firmware_revision[ConfigurationManager::kMaxFirmwareRevisionLength] = {};
  size_t out_len = 0;

  // An empty firmware revision will result in falling back to the revision
  // provided in the config-data.
  fake_buildinfo_provider().set_version("");
  ConfigurationMgrImpl().SetDelegate(nullptr);
  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
  EXPECT_EQ(ConfigurationMgrImpl().GetDelegate()->Init(), WEAVE_NO_ERROR);
  EXPECT_EQ(
      ConfigurationMgr().GetFirmwareRevision(firmware_revision, sizeof(firmware_revision), out_len),
      WEAVE_NO_ERROR);
  EXPECT_STREQ(firmware_revision, kExpectedFirmwareRevision_DeviceInfo);
}

TEST_F(ConfigurationManagerTest, GetSerialNumber) {
  char serial_num[kMaxSerialNumberSize];
  size_t out_len = 0;
  EXPECT_EQ(ConfigurationMgr().GetSerialNumber(serial_num, sizeof(serial_num), out_len),
            WEAVE_NO_ERROR);
  EXPECT_STREQ(serial_num, FakeHwinfoDevice::kSerialNumber);
}

TEST_F(ConfigurationManagerTest, GetDeviceDescriptor) {
  constexpr uint8_t expected_wifi_mac[kWiFiMacAddressBufSize] = {0xFF};
  constexpr uint8_t expected_802154_mac[k802154MacAddressBufSize] = {0xFF};

  ::nl::Weave::Profiles::DeviceDescription::WeaveDeviceDescriptor device_desc;
  EXPECT_EQ(ConfigurationMgr().GetDeviceDescriptor(device_desc), WEAVE_NO_ERROR);

  EXPECT_STREQ(device_desc.SerialNumber, FakeHwinfoDevice::kSerialNumber);
  EXPECT_EQ(device_desc.ProductId, kExpectedProductId);
  EXPECT_EQ(device_desc.VendorId, kExpectedVendorId);
  EXPECT_EQ(
      std::memcmp(expected_wifi_mac, device_desc.PrimaryWiFiMACAddress, kWiFiMacAddressBufSize), 0);
  EXPECT_EQ(std::memcmp(expected_802154_mac, device_desc.Primary802154MACAddress,
                        k802154MacAddressBufSize),
            0);
}

TEST_F(ConfigurationManagerTest, GetPairingCode) {
  char pairing_code[kMaxPairingCodeSize] = {};
  size_t out_len = 0;
  EXPECT_EQ(ConfigurationMgr().GetPairingCode(pairing_code, sizeof(pairing_code), out_len),
            WEAVE_NO_ERROR);
  EXPECT_STREQ(pairing_code, kExpectedPairingCode);
}

TEST_F(ConfigurationManagerTest, ReadFactoryFile) {
  constexpr char kFactoryFileName[] = "factory-file";
  constexpr char kFactoryFileData[] = "factory-data";
  char factory_file_data[32] = {};
  size_t out_len = 0;

  // Add simple file to the fake directory.
  fake_factory_weave_factory_store_provider().directory().AddFile(kFactoryFileName,
                                                                  kFactoryFileData);

  // Confirm that we were able to read the file contents back.
  EXPECT_EQ(delegate().ReadFactoryFile(kFactoryFileName, factory_file_data,
                                       sizeof(factory_file_data), &out_len),
            ZX_OK);
  EXPECT_STREQ(factory_file_data, kFactoryFileData);
}

TEST_F(ConfigurationManagerTest, ReadFactoryFile_BufferTooSmall) {
  constexpr char kFactoryFileName[] = "factory-file";
  constexpr char kFactoryFileData[] = "factory-data";
  char factory_file_data[32] = {};
  size_t out_len = 0;

  // Add simple file to the fake directory.
  fake_factory_weave_factory_store_provider().directory().AddFile(kFactoryFileName,
                                                                  kFactoryFileData);

  // Confirm that an insufficient buffer fails to read the file contents back.
  EXPECT_EQ(delegate().ReadFactoryFile(kFactoryFileName, factory_file_data,
                                       strlen(kFactoryFileData) - 1, &out_len),
            ZX_ERR_BUFFER_TOO_SMALL);
}

TEST_F(ConfigurationManagerTest, GetManufacturerDeviceCertificate_Factory) {
  constexpr char kFactoryManufacturerDeviceCertificateFileName[] = "test_mfr_cert";
  constexpr char kFactoryManufacturerDeviceCertificateFileData[] = "fake_certificate";
  uint8_t manufacturer_certificate[32] = {};
  size_t out_len = 0;

  // Ensure that the cached certificate is not used and disable local certs.
  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
  EXPECT_EQ(EnvironmentConfig::WriteConfigValue(
                EnvironmentConfig::kConfigKey_MfrDeviceCertAllowLocal, false),
            WEAVE_NO_ERROR);

  // Add the certificate to the fake directory.
  fake_factory_weave_factory_store_provider().directory().AddFile(
      kFactoryManufacturerDeviceCertificateFileName, kFactoryManufacturerDeviceCertificateFileData);

  // Confirm that we were able to read the file contents back.
  EXPECT_EQ(ConfigurationMgr().GetManufacturerDeviceCertificate(
                manufacturer_certificate, sizeof(manufacturer_certificate), out_len),
            WEAVE_NO_ERROR);
  EXPECT_TRUE(std::memcmp(manufacturer_certificate, kFactoryManufacturerDeviceCertificateFileData,
                          out_len) == 0);

  // Confirm that removing the file results in the cached value being used.
  fake_factory_weave_factory_store_provider().directory().RemoveFile(
      kFactoryManufacturerDeviceCertificateFileName);

  // Confirm that we were able to read the file contents back.
  EXPECT_EQ(ConfigurationMgr().GetManufacturerDeviceCertificate(
                manufacturer_certificate, sizeof(manufacturer_certificate), out_len),
            WEAVE_NO_ERROR);
  EXPECT_TRUE(std::memcmp(manufacturer_certificate, kFactoryManufacturerDeviceCertificateFileData,
                          out_len) == 0);
}

TEST_F(ConfigurationManagerTest, CacheFlagsOnInit) {
  constexpr uint64_t kFabricId = 0;
  constexpr uint8_t kServiceConfig[] = {};
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
  // Initial state is not fully provisioned.
  EXPECT_FALSE(ConfigurationMgr().IsFullyProvisioned());

  // Set thread enabled, which controls whether the thread stack manager state
  // is consulted when determining provisioning status.
  delegate().set_is_thread_enabled(true);

  for (size_t i = 0; i <= 1; i++) {
    for (size_t j = 0; j <= 1; j++) {
      for (size_t k = 0; k <= 1; k++) {
        bool is_paired_to_account = (i == 0);
        bool is_member_of_fabric = (j == 0);
        bool is_thread_provisioned = (k == 0);

        delegate()
            .set_is_paired_to_account(is_paired_to_account)
            .set_is_member_of_fabric(is_member_of_fabric);
        thread_delegate().set_is_thread_provisioned(is_thread_provisioned);

        EXPECT_EQ(ConfigurationMgr().IsFullyProvisioned(),
                  is_paired_to_account && is_member_of_fabric && is_thread_provisioned);
      }
    }
  }

  // If thread is disabled, the thread provisioning state should be ignored.
  delegate().set_is_thread_enabled(false);

  for (size_t i = 0; i <= 1; i++) {
    for (size_t j = 0; j <= 1; j++) {
      for (size_t k = 0; k <= 1; k++) {
        bool is_paired_to_account = (i == 0);
        bool is_member_of_fabric = (j == 0);
        bool is_thread_provisioned = (k == 0);

        delegate()
            .set_is_paired_to_account(is_paired_to_account)
            .set_is_member_of_fabric(is_member_of_fabric);
        thread_delegate().set_is_thread_provisioned(is_thread_provisioned);

        EXPECT_EQ(ConfigurationMgr().IsFullyProvisioned(),
                  is_paired_to_account && is_member_of_fabric);
      }
    }
  }
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

  files::DeletePath(kDataPath + filename, false);
}

TEST_F(ConfigurationManagerTest, GetTestCert) {
  std::string testdata("FAKECERT\n");
  uint8_t mfr_cert_buf[testdata.size() + 1];
  size_t cert_len;

  memset(mfr_cert_buf, 0, sizeof(mfr_cert_buf));
  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
  std::string filename("test_mfr_cert");
  CopyFileFromPkgToData(filename);

  EXPECT_EQ(ConfigurationMgr().GetManufacturerDeviceCertificate(mfr_cert_buf, sizeof(mfr_cert_buf),
                                                                cert_len),
            WEAVE_NO_ERROR);
  EXPECT_EQ(cert_len, testdata.size());
  EXPECT_TRUE(std::equal(mfr_cert_buf, mfr_cert_buf + std::min(cert_len, sizeof(mfr_cert_buf)),
                         testdata.begin(), testdata.end()));
  files::DeletePath(kDataPath + filename, false);
}

TEST_F(ConfigurationManagerTest, GetLocalSerialNumber) {
  char serial_num[kMaxSerialNumberSize] = {};
  size_t serial_num_len = 0;

  // A non-existent serial number field will result in falling back to the
  // serial number provided in config-data, if one is available.
  fake_hwinfo_device().set_serial_number(std::nullopt);
  ConfigurationMgrImpl().SetDelegate(nullptr);
  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());

  EXPECT_EQ(ConfigurationMgrImpl().GetDelegate()->Init(), WEAVE_NO_ERROR);
  EXPECT_EQ(ConfigurationMgr().GetSerialNumber(serial_num, sizeof(serial_num), serial_num_len),
            WEAVE_NO_ERROR);
  EXPECT_STREQ(serial_num, kExpectedSerialNumberLocal);
}

TEST_F(ConfigurationManagerTest, IsThreadEnabled) {
  EXPECT_TRUE(ConfigurationMgrImpl().IsThreadEnabled());
}

TEST_F(ConfigurationManagerTest, GetAppletsPathList) {
  std::vector<std::string> applet_paths = {"test1", "test2", "test3"};
  std::vector<std::string> expected_applet_paths;
  EXPECT_EQ(ConfigurationMgrImpl().GetAppletPathList(expected_applet_paths), WEAVE_NO_ERROR);
  EXPECT_TRUE(expected_applet_paths.size() == applet_paths.size());
  for (size_t i = 0; i < expected_applet_paths.size(); i++) {
    EXPECT_EQ(expected_applet_paths[i], applet_paths[i]);
  }
}

TEST_F(ConfigurationManagerTest, GetPrimaryWiFiMacAddress) {
  constexpr uint8_t expected[kWiFiMacAddressBufSize] = {0xFF};
  uint8_t mac_addr[kWiFiMacAddressBufSize];

  EXPECT_EQ(ConfigurationMgr().GetPrimaryWiFiMACAddress(mac_addr), WEAVE_NO_ERROR);
  EXPECT_EQ(0, std::memcmp(expected, mac_addr, kWiFiMacAddressBufSize));
}

TEST_F(ConfigurationManagerTest, GetThreadJoinableDuration) {
  constexpr uint32_t expected = 1234;
  uint32_t duration;

  EXPECT_EQ(ConfigurationMgrImpl().GetThreadJoinableDuration(&duration), WEAVE_NO_ERROR);
  EXPECT_EQ(duration, expected);
}

TEST_F(ConfigurationManagerTest, FactoryResetIfFailSafeArmed) {
  bool fail_safe_armed = true;
  WeaveGroupKey retrieved_key;
  GroupKeyStoreImpl group_key_store;
  const WeaveGroupKey test_key = CreateGroupKey(kTestKeyId, 0, kWeaveAppGroupKeySize, 0xABCDEF);

  // Store the fabric secret and set fail-safe-armed. Verify that erasing
  // weave data erases all of them from the environment.
  EXPECT_EQ(group_key_store.StoreGroupKey(test_key), WEAVE_NO_ERROR);
  EXPECT_EQ(EnvironmentConfig::WriteConfigValue(EnvironmentConfig::kConfigKey_FailSafeArmed, true),
            WEAVE_NO_ERROR);
  ConfigurationMgrImpl().SetDelegate(nullptr);
  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
  EXPECT_EQ(ConfigurationMgrImpl().GetDelegate()->Init(), WEAVE_NO_ERROR);
  EXPECT_EQ(EnvironmentConfig::ReadConfigValue(EnvironmentConfig::kConfigKey_FailSafeArmed,
                                               fail_safe_armed),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_EQ(group_key_store.RetrieveGroupKey(kTestKeyId, retrieved_key),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
}

TEST_F(ConfigurationManagerTest, SetConfiguration) {
  std::string from_filename("device_info_alt.json");
  std::string to_filename("device_info.json");
  uint16_t read_vendor_id, read_product_id;

  CopyFileFromConfigToData(from_filename, to_filename);

  ConfigurationMgrImpl().SetDelegate(nullptr);
  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
  EXPECT_EQ(ConfigurationMgrImpl().GetDelegate()->Init(), WEAVE_NO_ERROR);
  EXPECT_EQ(ConfigurationMgrImpl().GetVendorId(read_vendor_id), WEAVE_NO_ERROR);
  EXPECT_EQ(read_vendor_id, 1000);
  EXPECT_EQ(ConfigurationMgrImpl().GetProductId(read_product_id), WEAVE_NO_ERROR);
  EXPECT_EQ(read_product_id, 2000);

  files::DeletePath(kDataPath + to_filename, false);
}

TEST_F(ConfigurationManagerTest, GetManufacturingDate) {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;

  // Confirm happy path.
  EXPECT_EQ(ConfigurationMgr().GetManufacturingDate(year, month, day), WEAVE_NO_ERROR);
  EXPECT_EQ(year, FakeHwinfoProduct::kBuildDateYear);
  EXPECT_EQ(month, FakeHwinfoProduct::kBuildDateMonth);
  EXPECT_EQ(day, FakeHwinfoProduct::kBuildDateDay);
  year = month = day = 0;

  // Confirm config not found when build data is not supplied.
  fake_hwinfo_product().set_build_date(std::nullopt);
  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
  ConfigurationMgrImpl().SetDelegate(nullptr);
  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
  EXPECT_EQ(ConfigurationMgrImpl().GetDelegate()->Init(), WEAVE_NO_ERROR);
  EXPECT_EQ(ConfigurationMgr().GetManufacturingDate(year, month, day),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);

  EXPECT_EQ(year, 0);
  EXPECT_EQ(month, 0);
  EXPECT_EQ(day, 0);
}

}  // namespace nl::Weave::DeviceLayer::Internal::testing
