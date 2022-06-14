// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_DELEGATE_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_DELEGATE_IMPL_H_

#include <fuchsia/buildinfo/cpp/fidl.h>
#include <fuchsia/factory/cpp/fidl.h>
#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/weave/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#pragma GCC diagnostic push
#include <Weave/DeviceLayer/ConfigurationManager.h>
#pragma GCC diagnostic pop

#include "src/connectivity/weave/adaptation/environment_config.h"
#include "src/connectivity/weave/adaptation/group_key_store_impl.h"
#include "src/connectivity/weave/adaptation/weave_config_manager.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {

/**
 * A concrete implementation of the delegate used by ConfigurationManagerImpl to
 * make the required platform calls needed to serve configuration data to Weave.
 */
class NL_DLL_EXPORT ConfigurationManagerDelegateImpl : public ConfigurationManagerImpl::Delegate {
 public:
  ConfigurationManagerDelegateImpl();

  // ConfigurationManagerImpl::Delegate APIs
  WEAVE_ERROR Init(void) override;
  WEAVE_ERROR GetDeviceId(uint64_t& device_id) override;
  WEAVE_ERROR GetFirmwareRevision(char* buf, size_t buf_size, size_t& out_len) override;
  WEAVE_ERROR GetManufacturerDeviceCertificate(uint8_t* buf, size_t buf_size,
                                               size_t& out_len) override;
  WEAVE_ERROR GetProductId(uint16_t& product_id) override;
  WEAVE_ERROR GetProductIdDescription(char* buf, size_t buf_size, size_t& out_len) override;
  WEAVE_ERROR GetVendorId(uint16_t& vendor_id) override;
  WEAVE_ERROR GetVendorIdDescription(char* buf, size_t buf_size, size_t& out_len) override;
  bool IsFullyProvisioned() override;
  bool IsPairedToAccount() override;
  bool IsMemberOfFabric() override;
  GroupKeyStoreBase* GetGroupKeyStore(void) override;
  bool CanFactoryReset(void) override;
  void InitiateFactoryReset(void) override;
  WEAVE_ERROR ReadPersistedStorageValue(Key key, uint32_t& value) override;
  WEAVE_ERROR WritePersistedStorageValue(Key key, uint32_t value) override;
  WEAVE_ERROR GetBleDeviceNamePrefix(char* device_name_prefix, size_t device_name_prefix_size,
                                     size_t* out_len) override;
  bool IsThreadEnabled() override;
  bool IsIPv6ForwardingEnabled() override;
  bool IsWoBLEEnabled() override;
  bool IsWoBLEAdvertisementEnabled() override;
  WEAVE_ERROR GetDeviceDescriptorTLV(uint8_t* buf, size_t buf_size, size_t& encoded_len) override;
  WEAVE_ERROR GetPrimaryWiFiMACAddress(uint8_t* mac_address) override;
  zx_status_t GetPrivateKeyForSigning(std::vector<uint8_t>* signing_key) override;
  // Reads the list of applets from the config file and populates |out| with the same.
  zx_status_t GetAppletPathList(std::vector<std::string>& out) override;
  WEAVE_ERROR GetThreadJoinableDuration(uint32_t* duration) override;
  WEAVE_ERROR GetFailSafeArmed(bool& fail_safe_armed) override;
  WEAVE_ERROR SetFailSafeArmed(bool fail_safe_armed) override;
  WEAVE_ERROR StoreFabricId(uint64_t fabric_id) override;
  WEAVE_ERROR StoreServiceProvisioningData(uint64_t service_id, const uint8_t* service_config,
                                           size_t service_config_len, const char* account_id,
                                           size_t account_id_len) override;
  WEAVE_ERROR StoreServiceConfig(const uint8_t* service_config, size_t service_config_len) override;
  WEAVE_ERROR StorePairedAccountId(const char* account_id, size_t account_id_len) override;

  // Read up to |buf_size| bytes from the file |path| in the factory partition
  // into |buf|. If not NULL, the number of bytes read is stored in |out_len|.
  zx_status_t ReadFactoryFile(const char* path, char* buf, size_t buf_size, size_t* out_len);

 private:
  using GroupKeyStoreBase = ::nl::Weave::Profiles::Security::AppKeys::GroupKeyStoreBase;
  using GroupKeyStoreImpl = ::nl::Weave::DeviceLayer::Internal::GroupKeyStoreImpl;
  using WeaveConfigReader = ::nl::Weave::DeviceLayer::Internal::WeaveConfigReader;
  using Key = ::nl::Weave::Platform::PersistedStorage::Key;

  // Stores firmware revision from fuchsia.buildinfo into the configuration store.
  WEAVE_ERROR GetAndStoreFirmwareRevision();
  // Stores serial number from fuchsia.hwinfo into the configuration store.
  WEAVE_ERROR GetAndStoreSerialNumber();
  // Stores the pairing code from fuchsia.factory into the configuration store.
  WEAVE_ERROR GetAndStorePairingCode();
  // Stores the manufacturer device cert into the configuration store.
  WEAVE_ERROR GetAndStoreMfrDeviceCert();
  // Stores the manufacturing date into the configuration store.
  WEAVE_ERROR GetAndStoreManufacturingDate();
  // Acquires the weave device ID from the file |path| in the factory partition
  // and stores it in |factory_device_id|.
  zx_status_t GetDeviceIdFromFactory(const char* path, uint64_t* factory_device_id);

  GroupKeyStoreImpl group_key_store_;
  std::string firmware_revision_;

  fuchsia::buildinfo::ProviderSyncPtr buildinfo_provider_;
  fuchsia::hwinfo::DeviceSyncPtr hwinfo_device_;
  fuchsia::hwinfo::ProductSyncPtr hwinfo_product_;
  fuchsia::factory::WeaveFactoryStoreProviderSyncPtr factory_store_provider_;
  fuchsia::weave::FactoryDataManagerSyncPtr weave_factory_data_manager_;

  std::unique_ptr<Internal::WeaveConfigManager> device_info_;
};

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_DELEGATE_IMPL_H_
