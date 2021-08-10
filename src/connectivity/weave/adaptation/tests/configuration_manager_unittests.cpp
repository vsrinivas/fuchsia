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

#include <src/lib/files/file.h>
#include <src/lib/files/path.h>

#include "configuration_manager_delegate_impl.h"
#include "fake_buildinfo_provider.h"
#include "fake_factory_weave_factory_store_provider.h"
#include "fake_hwinfo_device.h"
#include "fake_hwinfo_product.h"
#include "fake_weave_factory_data_manager.h"
#include "test_config.h"
#include "test_configuration_manager.h"
#include "test_thread_stack_manager.h"
#include "weave_test_fixture.h"

namespace weave::adaptation::testing {
namespace {

using nl::Weave::WeaveKeyId;
using nl::Weave::DeviceLayer::ConfigurationManager;
using nl::Weave::DeviceLayer::ConfigurationManagerDelegateImpl;
using nl::Weave::DeviceLayer::ConfigurationManagerImpl;
using nl::Weave::DeviceLayer::ConfigurationMgr;
using nl::Weave::DeviceLayer::ConfigurationMgrImpl;
using nl::Weave::DeviceLayer::PlatformMgrImpl;
using nl::Weave::DeviceLayer::ThreadStackMgrImpl;
using nl::Weave::DeviceLayer::Internal::EnvironmentConfig;
using nl::Weave::DeviceLayer::Internal::GroupKeyStoreImpl;
using nl::Weave::DeviceLayer::Internal::testing::WeaveTestFixture;
using nl::Weave::Profiles::DeviceDescription::WeaveDeviceDescriptor;
using nl::Weave::Profiles::Security::AppKeys::WeaveGroupKey;

constexpr size_t kWiFiMACAddressBufSize = sizeof(WeaveDeviceDescriptor::PrimaryWiFiMACAddress);
constexpr size_t k802154MACAddressBufSize = sizeof(WeaveDeviceDescriptor::Primary802154MACAddress);

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

    // Simulate the default state for thread by enabling support but disabling
    // provisioning. Tests will explicitly disable them to validate behavior
    // when thread is either not supported or not provisioned.
    thread_delegate().set_is_thread_supported(true);
    thread_delegate().set_is_thread_provisioned(false);
    EXPECT_EQ(delegate().Init(), WEAVE_NO_ERROR);
  }

  void TearDown() override {
    WeaveTestFixture<>::StopFixtureLoop();
    WeaveTestFixture<>::TearDown();
    ThreadStackMgrImpl().SetDelegate(nullptr);
    ConfigurationMgrImpl().SetDelegate(nullptr);
    DeleteFiles();
  }

 protected:
  // Copy the file from /pkg/data to /data.
  void CopyFileFromPkgToData(const std::string filename) {
    return CopyFile(files::JoinPath(EnvironmentConfig::kPackageDataPath, filename),
                    files::JoinPath(EnvironmentConfig::kDataPath, filename));
  }

  // Copy the file from /config/data to /data.
  void CopyFileFromConfigToData(const std::string filename) {
    return CopyFileFromConfigToData(filename, filename);
  }

  // Copy the file from /config/data to /data, with a new filename.
  void CopyFileFromConfigToData(const std::string source, const std::string destination) {
    return CopyFile(files::JoinPath(EnvironmentConfig::kConfigDataPath, source),
                    files::JoinPath(EnvironmentConfig::kDataPath, destination));
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
  // Copy a file from the provided source to the provided destination.
  void CopyFile(const std::string source, const std::string destination) {
    std::string file_data;
    ASSERT_TRUE(files::ReadFileToString(source, &file_data));
    ASSERT_TRUE(files::WriteFile(destination, file_data));
    copied_files_.push_back(destination);
  }

  // Deletes all files that were copied during the test.
  void DeleteFiles() {
    for (std::string filename : copied_files_) {
      ASSERT_TRUE(files::DeletePath(filename, false));
    }
  }

  FakeBuildInfoProvider fake_buildinfo_provider_;
  FakeHwinfoDevice fake_hwinfo_device_;
  FakeHwinfoProduct fake_hwinfo_product_;
  FakeWeaveFactoryDataManager fake_weave_factory_data_manager_;
  FakeFactoryWeaveFactoryStoreProvider fake_factory_weave_factory_store_provider_;

  sys::testing::ComponentContextProvider context_provider_;
  std::vector<std::string> copied_files_;
};

TEST_F(ConfigurationManagerTest, GetFabricId) {
  const uint64_t fabric_id = 123456789U;
  uint64_t stored_fabric_id = 0;
  EXPECT_EQ(ConfigurationMgr().StoreFabricId(fabric_id), WEAVE_NO_ERROR);
  EXPECT_EQ(ConfigurationMgr().GetFabricId(stored_fabric_id), WEAVE_NO_ERROR);
  EXPECT_EQ(stored_fabric_id, fabric_id);
}

TEST_F(ConfigurationManagerTest, GetDeviceId_DeviceInfo) {
  uint64_t device_id = 0;
  EXPECT_EQ(ConfigurationMgr().GetDeviceId(device_id), WEAVE_NO_ERROR);
  EXPECT_EQ(device_id, testdata::kTestDataDeviceId);
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
  EXPECT_EQ(vendor_id, testdata::kTestDataVendorId);
}

TEST_F(ConfigurationManagerTest, GetVendorIdDescription) {
  char vendor_id_description[ConfigurationManager::kMaxVendorIdDescriptionLength] = {};
  size_t out_len = 0;
  EXPECT_EQ(ConfigurationMgr().GetVendorIdDescription(vendor_id_description,
                                                      sizeof(vendor_id_description), out_len),
            WEAVE_NO_ERROR);
  ASSERT_EQ(out_len, strlen(testdata::kTestDataVendorIdDescription));
  EXPECT_STREQ(vendor_id_description, testdata::kTestDataVendorIdDescription);
}

TEST_F(ConfigurationManagerTest, GetProductId) {
  uint16_t product_id = 0;
  EXPECT_EQ(ConfigurationMgr().GetProductId(product_id), WEAVE_NO_ERROR);
  EXPECT_EQ(product_id, testdata::kTestDataProductId);
}

TEST_F(ConfigurationManagerTest, GetProductIdDescription) {
  char product_id_description[ConfigurationManager::kMaxProductIdDescriptionLength] = {};
  size_t out_len = 0;
  EXPECT_EQ(ConfigurationMgr().GetProductIdDescription(product_id_description,
                                                       sizeof(product_id_description), out_len),
            WEAVE_NO_ERROR);
  ASSERT_EQ(out_len, strlen(testdata::kTestDataProductIdDescription));
  EXPECT_STREQ(product_id_description, testdata::kTestDataProductIdDescription);
}

TEST_F(ConfigurationManagerTest, GetFirmwareRevision_BuildInfo) {
  char firmware_revision[ConfigurationManager::kMaxFirmwareRevisionLength] = {};
  size_t out_len = 0;
  EXPECT_EQ(
      ConfigurationMgr().GetFirmwareRevision(firmware_revision, sizeof(firmware_revision), out_len),
      WEAVE_NO_ERROR);
  ASSERT_EQ(out_len, strlen(FakeBuildInfoProvider::kVersion));
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
  EXPECT_EQ(delegate().Init(), WEAVE_NO_ERROR);
  EXPECT_EQ(
      ConfigurationMgr().GetFirmwareRevision(firmware_revision, sizeof(firmware_revision), out_len),
      WEAVE_NO_ERROR);
  ASSERT_EQ(out_len, strlen(testdata::kTestDataFirmwareRevision));
  EXPECT_STREQ(firmware_revision, testdata::kTestDataFirmwareRevision);
}

TEST_F(ConfigurationManagerTest, GetSerialNumber) {
  char serial_num[ConfigurationManager::kMaxSerialNumberLength] = {};
  size_t out_len = 0;
  EXPECT_EQ(ConfigurationMgr().GetSerialNumber(serial_num, sizeof(serial_num), out_len),
            WEAVE_NO_ERROR);
  ASSERT_EQ(out_len, strlen(FakeHwinfoDevice::kSerialNumber));
  EXPECT_STREQ(serial_num, FakeHwinfoDevice::kSerialNumber);
}

TEST_F(ConfigurationManagerTest, GetDeviceDescriptor) {
  constexpr uint8_t expected_wifi_mac[kWiFiMACAddressBufSize] = {0xFF};
  constexpr uint8_t expected_802154_mac[k802154MACAddressBufSize] = {0xFF};

  ::nl::Weave::Profiles::DeviceDescription::WeaveDeviceDescriptor device_desc;
  EXPECT_EQ(ConfigurationMgr().GetDeviceDescriptor(device_desc), WEAVE_NO_ERROR);

  EXPECT_STREQ(device_desc.SerialNumber, FakeHwinfoDevice::kSerialNumber);
  EXPECT_EQ(device_desc.ProductId, testdata::kTestDataProductId);
  EXPECT_EQ(device_desc.VendorId, testdata::kTestDataVendorId);
  EXPECT_EQ(
      std::memcmp(expected_wifi_mac, device_desc.PrimaryWiFiMACAddress, kWiFiMACAddressBufSize), 0);
  EXPECT_EQ(std::memcmp(expected_802154_mac, device_desc.Primary802154MACAddress,
                        k802154MACAddressBufSize),
            0);
}

TEST_F(ConfigurationManagerTest, GetPairingCode) {
  char pairing_code[ConfigurationManager::kMaxPairingCodeLength] = {};
  size_t out_len = 0;
  EXPECT_EQ(ConfigurationMgr().GetPairingCode(pairing_code, sizeof(pairing_code), out_len),
            WEAVE_NO_ERROR);
  ASSERT_EQ(out_len, strlen(testdata::kTestDataPairingCode));
  EXPECT_STREQ(pairing_code, testdata::kTestDataPairingCode);
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
  ASSERT_EQ(out_len, strlen(kFactoryFileData));
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
  EXPECT_EQ(out_len, 0u);
}

TEST_F(ConfigurationManagerTest, GetManufacturerDeviceCertificate_Factory) {
  constexpr char kFactoryCertificateFileData[] = "fake_certificate";
  uint8_t manufacturer_certificate[32] = {};
  size_t out_len = 0;

  // Ensure that the cached certificate is not used and disable local certs.
  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
  EXPECT_EQ(EnvironmentConfig::WriteConfigValue(
                EnvironmentConfig::kConfigKey_MfrDeviceCertAllowLocal, false),
            WEAVE_NO_ERROR);

  // Add the certificate to the fake directory.
  fake_factory_weave_factory_store_provider().directory().AddFile(
      testdata::kTestDataCertificateFileName, kFactoryCertificateFileData);

  // Confirm that we were able to read the file contents back.
  EXPECT_EQ(ConfigurationMgr().GetManufacturerDeviceCertificate(
                manufacturer_certificate, sizeof(manufacturer_certificate), out_len),
            WEAVE_NO_ERROR);
  ASSERT_EQ(out_len, strlen(kFactoryCertificateFileData));
  EXPECT_EQ(std::memcmp(manufacturer_certificate, kFactoryCertificateFileData, out_len), 0);

  // Confirm that removing the file results in the cached value being used.
  fake_factory_weave_factory_store_provider().directory().RemoveFile(
      testdata::kTestDataCertificateFileName);

  // Confirm that we were able to read the file contents back.
  out_len = 0;
  EXPECT_EQ(ConfigurationMgr().GetManufacturerDeviceCertificate(
                manufacturer_certificate, sizeof(manufacturer_certificate), out_len),
            WEAVE_NO_ERROR);
  ASSERT_EQ(out_len, strlen(kFactoryCertificateFileData));
  EXPECT_EQ(std::memcmp(manufacturer_certificate, kFactoryCertificateFileData, out_len), 0);
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
  EXPECT_EQ(delegate().Init(), WEAVE_NO_ERROR);
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

TEST_F(ConfigurationManagerTest, GetPrivateKey_Data) {
  std::vector<uint8_t> signing_key;

  CopyFileFromPkgToData(testdata::kTestDataPrivateKeyFileName);

  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
  EXPECT_EQ(ConfigurationMgrImpl().GetPrivateKeyForSigning(&signing_key), ZX_OK);
  ASSERT_EQ(signing_key.size(), strlen(testdata::kTestDataPrivateKeyFileData));
  EXPECT_EQ(
      std::memcmp(signing_key.data(), testdata::kTestDataPrivateKeyFileData, signing_key.size()),
      0);
}

TEST_F(ConfigurationManagerTest, GetManufacturerDeviceCertificate_Data) {
  uint8_t certificate[sizeof(testdata::kTestDataCertificateFileData) + 1] = {};
  size_t out_len = 0;

  CopyFileFromPkgToData(testdata::kTestDataCertificateFileName);

  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
  EXPECT_EQ(ConfigurationMgr().GetManufacturerDeviceCertificate(certificate, sizeof(certificate),
                                                                out_len),
            WEAVE_NO_ERROR);
  ASSERT_EQ(out_len, strlen(testdata::kTestDataCertificateFileData));
  EXPECT_EQ(std::memcmp(certificate, testdata::kTestDataCertificateFileData, out_len), 0);
}

TEST_F(ConfigurationManagerTest, GetSerialNumber_DeviceInfo) {
  char serial_num[ConfigurationManager::kMaxSerialNumberLength] = {};
  size_t out_len = 0;

  // A non-existent serial number field will result in falling back to the
  // serial number provided in config-data, if one is available.
  fake_hwinfo_device().set_serial_number(std::nullopt);
  ConfigurationMgrImpl().SetDelegate(nullptr);
  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());

  EXPECT_EQ(delegate().Init(), WEAVE_NO_ERROR);
  EXPECT_EQ(ConfigurationMgr().GetSerialNumber(serial_num, sizeof(serial_num), out_len),
            WEAVE_NO_ERROR);
  ASSERT_EQ(out_len, strlen(testdata::kTestDataSerialNumber));
  EXPECT_STREQ(serial_num, testdata::kTestDataSerialNumber);
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
  constexpr uint8_t expected[kWiFiMACAddressBufSize] = {0xFF};
  uint8_t mac_addr[kWiFiMACAddressBufSize] = {};

  EXPECT_EQ(ConfigurationMgr().GetPrimaryWiFiMACAddress(mac_addr), WEAVE_NO_ERROR);
  EXPECT_EQ(std::memcmp(expected, mac_addr, kWiFiMACAddressBufSize), 0);
}

TEST_F(ConfigurationManagerTest, GetThreadJoinableDuration) {
  uint32_t duration = 0;

  EXPECT_EQ(ConfigurationMgrImpl().GetThreadJoinableDuration(&duration), WEAVE_NO_ERROR);
  EXPECT_EQ(duration, testdata::kTestThreadJoinableDuration);
}

TEST_F(ConfigurationManagerTest, FactoryResetIfFailSafeArmed) {
  bool fail_safe_armed = false;
  GroupKeyStoreImpl group_key_store;
  WeaveGroupKey retrieved_key;
  WeaveGroupKey test_key = {
      .KeyId = WeaveKeyId::kFabricSecret + 1u,
      .KeyLen = nl::Weave::Profiles::Security::AppKeys::kWeaveAppGroupKeySize,
      .Key = {},
      .StartTime = 0xFFFF,
  };
  std::memset(test_key.Key, 0xA, nl::Weave::Profiles::Security::AppKeys::kWeaveAppGroupKeySize);

  // Store the fabric secret and read the key back.
  EXPECT_EQ(group_key_store.StoreGroupKey(test_key), WEAVE_NO_ERROR);
  EXPECT_EQ(group_key_store.RetrieveGroupKey(test_key.KeyId, retrieved_key), WEAVE_NO_ERROR);
  EXPECT_EQ(retrieved_key.KeyId, test_key.KeyId);
  EXPECT_EQ(retrieved_key.StartTime, test_key.StartTime);
  ASSERT_EQ(retrieved_key.KeyLen, test_key.KeyLen);
  EXPECT_EQ(std::memcmp(retrieved_key.Key, test_key.Key, retrieved_key.KeyLen), 0);

  // Arm the fail-safe then re-init the stack. This should cause all weave data
  // to be erased, as this simulates a restart while the fail-safe is armed.
  EXPECT_EQ(EnvironmentConfig::WriteConfigValue(EnvironmentConfig::kConfigKey_FailSafeArmed, true),
            WEAVE_NO_ERROR);
  EXPECT_EQ(delegate().Init(), WEAVE_NO_ERROR);

  // Confirm that the fail-safe is not configured and that the group key is no
  // longer present.
  EXPECT_EQ(EnvironmentConfig::ReadConfigValue(EnvironmentConfig::kConfigKey_FailSafeArmed,
                                               fail_safe_armed),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_EQ(group_key_store.RetrieveGroupKey(test_key.KeyId, retrieved_key),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
}

TEST_F(ConfigurationManagerTest, SetConfiguration) {
  uint16_t vendor_id = 0;
  uint16_t product_id = 0;

  CopyFileFromConfigToData(testdata::kTestAltConfigFileName, testdata::kTestConfigFileName);

  EXPECT_EQ(delegate().Init(), WEAVE_NO_ERROR);
  EXPECT_EQ(ConfigurationMgrImpl().GetVendorId(vendor_id), WEAVE_NO_ERROR);
  EXPECT_EQ(vendor_id, testdata::kTestAltDataVendorId);
  EXPECT_EQ(ConfigurationMgrImpl().GetProductId(product_id), WEAVE_NO_ERROR);
  EXPECT_EQ(product_id, testdata::kTestAltDataProductId);
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

  // Disable the hwinfo FIDL from returning a build date.
  year = month = day = 0;
  fake_hwinfo_product().set_build_date(std::nullopt);
  EXPECT_EQ(EnvironmentConfig::FactoryResetConfig(), WEAVE_NO_ERROR);
  EXPECT_EQ(delegate().Init(), WEAVE_NO_ERROR);

  // Confirm no date is read if the build data is not supplied.
  EXPECT_EQ(ConfigurationMgr().GetManufacturingDate(year, month, day),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
  EXPECT_EQ(year, 0);
  EXPECT_EQ(month, 0);
  EXPECT_EQ(day, 0);
}

}  // namespace weave::adaptation::testing
