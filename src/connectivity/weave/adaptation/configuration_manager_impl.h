// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_IMPL_H_

// clang-format off
#include <Weave/DeviceLayer/WeaveDeviceConfig.h>
#include <Weave/DeviceLayer/internal/GenericConfigurationManagerImpl.h>
// clang-format on

#include <fuchsia/factory/cpp/fidl.h>
#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/weave/cpp/fidl.h>
#include <fuchsia/wlan/device/service/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "src/connectivity/weave/adaptation/environment_config.h"
#include "src/connectivity/weave/adaptation/weave_config_manager.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {
class NetworkProvisioningServerImpl;
class WeaveConfigReader;
namespace testing {
class ConfigurationManagerTest;
}  // namespace testing
}  // namespace Internal

/**
 * Concrete implementation of the ConfigurationManager singleton object for the Fuchsia platform.
 */
class ConfigurationManagerImpl final
    : public ConfigurationManager,
      public Internal::GenericConfigurationManagerImpl<ConfigurationManagerImpl>,
      private Internal::EnvironmentConfig {
  // Allow the ConfigurationManager interface class to delegate method calls to
  // the implementation methods provided by this class.
  friend class ConfigurationManager;

  // Allow the GenericConfigurationManagerImpl base class to access helper methods and types
  // defined on this class.
  friend class Internal::GenericConfigurationManagerImpl<ConfigurationManagerImpl>;

 private:
  // ===== Members that implement the ConfigurationManager public interface.

  WEAVE_ERROR _Init(void);
  WEAVE_ERROR _GetDeviceId(uint64_t& device_id);
  WEAVE_ERROR _GetFirmwareRevision(char* buf, size_t buf_size, size_t& out_len);
  WEAVE_ERROR _GetManufacturerDeviceCertificate(uint8_t* buf, size_t buf_size, size_t& out_len);
  WEAVE_ERROR _GetProductId(uint16_t& product_id);
  WEAVE_ERROR _GetPrimaryWiFiMACAddress(uint8_t* buf);
  WEAVE_ERROR _GetVendorId(uint16_t& vendor_id);

  ::nl::Weave::Profiles::Security::AppKeys::GroupKeyStoreBase* _GetGroupKeyStore(void);
  bool _CanFactoryReset(void);
  void _InitiateFactoryReset(void);

  WEAVE_ERROR _ReadPersistedStorageValue(::nl::Weave::Platform::PersistedStorage::Key key,
                                         uint32_t& value);
  WEAVE_ERROR _WritePersistedStorageValue(::nl::Weave::Platform::PersistedStorage::Key key,
                                          uint32_t value);

  // ===== Members for internal use by the following friends.

  friend class Internal::NetworkProvisioningServerImpl;
  friend class Internal::testing::ConfigurationManagerTest;
  friend ConfigurationManager& ConfigurationMgr(void);
  friend ConfigurationManagerImpl& ConfigurationMgrImpl(void);

  static ConfigurationManagerImpl sInstance;

  zx_status_t GetDeviceIdFromFactory(const char* path, uint64_t* factory_device_id);

  fuchsia::hwinfo::DeviceSyncPtr hwinfo_device_;
  fuchsia::weave::FactoryDataManagerSyncPtr weave_factory_data_manager_;
  fuchsia::wlan::device::service::DeviceServiceSyncPtr wlan_device_service_;
  std::unique_ptr<Internal::WeaveConfigReader> device_info_;
  fuchsia::factory::WeaveFactoryStoreProviderSyncPtr factory_store_provider_;

  WEAVE_ERROR GetAndStoreHWInfo();
  WEAVE_ERROR GetAndStorePairingCode();
  WEAVE_ERROR GetAndStoreMfrDeviceCert();

 public:
  ConfigurationManagerImpl();
  // Read a file from the factory partition into |buf| up to a maximum of
  // |buf_size| bytes. If not null, the total number of bytes read in is stored
  // in *|out_len|.
  zx_status_t ReadFactoryFile(const char* path, char* buf, size_t buf_size, size_t* out_len);

  // Reads the BLE device name prefix from the configuration data into |device_name_prefix|.
  //
  // |device_name_prefix_size| holds the size of |device_name_prefix| in bytes. If
  // |device_name_prefix| is too small to hold the BLE device name prefix, this method will
  // return |WEAVE_ERROR_BUFFER_TOO_SMALL|.
  //
  // When this method returns successfully, |out_len| will hold the number of bytes used in
  // |device_name_prefix| to store the BLE device name.
  WEAVE_ERROR GetBleDeviceNamePrefix(char* device_name_prefix, size_t device_name_prefix_size,
                                     size_t* out_len);
  bool IsWOBLEEnabled();
};

/**
 * Returns the public interface of the ConfigurationManager singleton object.
 *
 * Weave applications should use this to access features of the ConfigurationManager object
 * that are common to all platforms.
 */
inline ConfigurationManager& ConfigurationMgr(void) { return ConfigurationManagerImpl::sInstance; }

/**
 * Returns the platform-specific implementation of the ConfigurationManager singleton object.
 *
 * Weave applications can use this to gain access to features of the ConfigurationManager
 * that are specific to the Fuchsia platform.
 */
inline ConfigurationManagerImpl& ConfigurationMgrImpl(void) {
  return ConfigurationManagerImpl::sInstance;
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_IMPL_H_
