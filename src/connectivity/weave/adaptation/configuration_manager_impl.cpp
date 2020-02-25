// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConfigurationManager.h>
#include <Weave/Core/WeaveKeyIds.h>
#include "configuration_manager_impl.h"
#include "fuchsia_config.h"
#include "group_key_store_impl.h"
#include <Weave/Profiles/security/WeaveApplicationKeys.h>

#include <Weave/Core/WeaveVendorIdentifiers.hpp>
#include <Weave/DeviceLayer/internal/GenericConfigurationManagerImpl.ipp>
#if WEAVE_DEVICE_CONFIG_ENABLE_FACTORY_PROVISIONING
#include <Weave/DeviceLayer/internal/FactoryProvisioning.ipp>
#endif  // WEAVE_DEVICE_CONFIG_ENABLE_FACTORY_PROVISIONING
// clang-format on

namespace nl {
namespace Weave {
namespace DeviceLayer {

using namespace ::nl::Weave::Profiles::Security::AppKeys;
using namespace ::nl::Weave::Profiles::DeviceDescription;
using namespace ::nl::Weave::DeviceLayer::Internal;

namespace {

// Singleton instance of Weave Group Key Store for the Fuchsia.
//
// NOTE: This is declared as a private global variable, rather than a static
// member of ConfigurationManagerImpl, to reduce the number of headers that
// must be included by the application when using the ConfigurationManager API.
//
GroupKeyStoreImpl gGroupKeyStore;

}  // unnamed namespace

/* Singleton instance of the ConfigurationManager implementation object for the Fuchsia. */
ConfigurationManagerImpl ConfigurationManagerImpl::sInstance;

WEAVE_ERROR ConfigurationManagerImpl::_Init() { return WEAVE_NO_ERROR; }

WEAVE_ERROR ConfigurationManagerImpl::_GetPrimaryWiFiMACAddress(uint8_t* buf) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ConfigurationManagerImpl::_GetDeviceDescriptor(
    ::nl::Weave::Profiles::DeviceDescription::WeaveDeviceDescriptor& deviceDesc) {
  return WEAVE_NO_ERROR;
}

::nl::Weave::Profiles::Security::AppKeys::GroupKeyStoreBase*
ConfigurationManagerImpl::_GetGroupKeyStore() {
  return &gGroupKeyStore;
}

bool ConfigurationManagerImpl::_CanFactoryReset() { return true; }

void ConfigurationManagerImpl::_InitiateFactoryReset() { FuchsiaConfig::FactoryResetConfig(); }

WEAVE_ERROR ConfigurationManagerImpl::_ReadPersistedStorageValue(
    ::nl::Weave::Platform::PersistedStorage::Key key, uint32_t& value) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ConfigurationManagerImpl::_WritePersistedStorageValue(
    ::nl::Weave::Platform::PersistedStorage::Key key, uint32_t value) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ConfigurationManagerImpl::GetWiFiStationSecurityType(
    Profiles::NetworkProvisioning::WiFiSecurityType& secType) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ConfigurationManagerImpl::UpdateWiFiStationSecurityType(
    Profiles::NetworkProvisioning::WiFiSecurityType secType) {
  return WEAVE_NO_ERROR;
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
