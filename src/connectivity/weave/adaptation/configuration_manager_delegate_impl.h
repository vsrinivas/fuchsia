// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_DELEGATE_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_DELEGATE_IMPL_H_

#include <fuchsia/factory/cpp/fidl.h>
#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/weave/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <Weave/DeviceLayer/ConfigurationManager.h>

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
class ConfigurationManagerDelegateImpl : public ConfigurationManagerImpl::Delegate {
 public:
  ConfigurationManagerDelegateImpl();

  // ConfigurationManagerImpl::Delegate APIs
  WEAVE_ERROR Init(void) override;
  WEAVE_ERROR GetDeviceId(uint64_t& device_id) override;
  WEAVE_ERROR GetFirmwareRevision(char* buf, size_t buf_size, size_t& out_len) override;
  WEAVE_ERROR GetManufacturerDeviceCertificate(uint8_t* buf, size_t buf_size,
                                               size_t& out_len) override;
  WEAVE_ERROR GetProductId(uint16_t& product_id) override;
  WEAVE_ERROR GetVendorId(uint16_t& vendor_id) override;
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
  bool IsWoBLEEnabled() override;
  bool IsWoBLEAdvertisementEnabled() override;
  WEAVE_ERROR GetDeviceDescriptorTLV(uint8_t* buf, size_t buf_size, size_t& encoded_len) override;
  zx_status_t GetPrivateKeyForSigning(std::vector<uint8_t>* signing_key) override;

 protected:
  // Read up to |buf_size| bytes from the file |path| in the factory partition
  // into |buf|. If not NULL, the number of bytes read is stored in |out_len|.
  zx_status_t ReadFactoryFile(const char* path, char* buf, size_t buf_size, size_t* out_len);

 private:
  using GroupKeyStoreBase = ::nl::Weave::Profiles::Security::AppKeys::GroupKeyStoreBase;
  using GroupKeyStoreImpl = ::nl::Weave::DeviceLayer::Internal::GroupKeyStoreImpl;
  using WeaveConfigReader = ::nl::Weave::DeviceLayer::Internal::WeaveConfigReader;
  using Key = ::nl::Weave::Platform::PersistedStorage::Key;

  // Stores information from fuchsia.hwinfo into the configuration store.
  WEAVE_ERROR GetAndStoreHWInfo();
  // Stores the pairing code from fuchsia.factory into the configuration store.
  WEAVE_ERROR GetAndStorePairingCode();
  // Stores the manufacturer device cert into the configuration store.
  WEAVE_ERROR GetAndStoreMfrDeviceCert();
  // Acquires the weave device ID from the file |path| in the factory partition
  // and stores it in |factory_device_id|.
  zx_status_t GetDeviceIdFromFactory(const char* path, uint64_t* factory_device_id);

  GroupKeyStoreImpl group_key_store_;

  fuchsia::hwinfo::DeviceSyncPtr hwinfo_device_;
  fuchsia::factory::WeaveFactoryStoreProviderSyncPtr factory_store_provider_;
  fuchsia::weave::FactoryDataManagerSyncPtr weave_factory_data_manager_;

  std::unique_ptr<WeaveConfigReader> device_info_;
};

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_DELEGATE_IMPL_H_
