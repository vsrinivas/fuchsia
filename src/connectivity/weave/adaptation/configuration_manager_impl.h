// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONFIGURATION_MANAGER_IMPL_H_

#include <Weave/DeviceLayer/WeaveDeviceConfig.h>
#include <Weave/DeviceLayer/internal/GenericConfigurationManagerImpl.h>

#include "fuchsia_config.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {

namespace Internal {
class NetworkProvisioningServerImpl;
}

/**
 * Concrete implementation of the ConfigurationManager singleton object for the Fuchsia platform.
 */
class ConfigurationManagerImpl final
    : public ConfigurationManager,
      public Internal::GenericConfigurationManagerImpl<ConfigurationManagerImpl>,
      private Internal::FuchsiaConfig {
  // Allow the ConfigurationManager interface class to delegate method calls to
  // the implementation methods provided by this class.
  friend class ConfigurationManager;

  // Allow the GenericConfigurationManagerImpl base class to access helper methods and types
  // defined on this class.
  friend class Internal::GenericConfigurationManagerImpl<ConfigurationManagerImpl>;

 private:
  // ===== Members that implement the ConfigurationManager public interface.

  WEAVE_ERROR _Init(void);
  WEAVE_ERROR _GetPrimaryWiFiMACAddress(uint8_t* buf);
  WEAVE_ERROR _GetDeviceDescriptor(
      ::nl::Weave::Profiles::DeviceDescription::WeaveDeviceDescriptor& deviceDesc);
  ::nl::Weave::Profiles::Security::AppKeys::GroupKeyStoreBase* _GetGroupKeyStore(void);
  bool _CanFactoryReset(void);
  void _InitiateFactoryReset(void);
  WEAVE_ERROR _ReadPersistedStorageValue(::nl::Weave::Platform::PersistedStorage::Key key,
                                         uint32_t& value);
  WEAVE_ERROR _WritePersistedStorageValue(::nl::Weave::Platform::PersistedStorage::Key key,
                                          uint32_t value);

  // NOTE: Other public interface methods are implemented by GenericConfigurationManagerImpl<>.

  // ===== Members for internal use by the following friends.

  friend class Internal::NetworkProvisioningServerImpl;
  friend ConfigurationManager& ConfigurationMgr(void);
  friend ConfigurationManagerImpl& ConfigurationMgrImpl(void);

  static ConfigurationManagerImpl sInstance;

  WEAVE_ERROR GetWiFiStationSecurityType(
      ::nl::Weave::Profiles::NetworkProvisioning::WiFiSecurityType& secType);
  WEAVE_ERROR UpdateWiFiStationSecurityType(
      ::nl::Weave::Profiles::NetworkProvisioning::WiFiSecurityType secType);
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
