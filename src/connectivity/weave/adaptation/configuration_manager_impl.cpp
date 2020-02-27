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

#include <fuchsia/wlan/device/service/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <net/ethernet.h>
#include <src/lib/fxl/logging.h>

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

ConfigurationManagerImpl::ConfigurationManagerImpl(std::unique_ptr<sys::ComponentContext> context) {
  context_ = std::move(context);
  _Init();
}

WEAVE_ERROR ConfigurationManagerImpl::_Init() {
  if (!context_) {
    context_ = sys::ComponentContext::Create();
  }
  zx_status_t status = context_->svc()->Connect(wlan_device_service_.NewRequest());
  FXL_CHECK(status == ZX_OK) << "Failed to connect to wlan service.";
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ConfigurationManagerImpl::_GetPrimaryWiFiMACAddress(uint8_t* buf) {
  fuchsia::wlan::device::service::ListPhysResponse phy_list_resp;
  if (ZX_OK != wlan_device_service_->ListPhys(&phy_list_resp)) {
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }
  for (auto phy : phy_list_resp.phys) {
    fuchsia::wlan::device::service::QueryPhyRequest req;
    int32_t out_status;
    std::unique_ptr<fuchsia::wlan::device::service::QueryPhyResponse> phy_resp;

    req.phy_id = phy.phy_id;
    if (ZX_OK != wlan_device_service_->QueryPhy(std::move(req), &out_status, &phy_resp) ||
        0 != out_status) {
      continue;
    }

    for (auto role : phy_resp->info.mac_roles) {
      if (role == fuchsia::wlan::device::MacRole::CLIENT) {
        memcpy(buf, phy_resp->info.hw_mac_address.data(), ETH_ALEN);
        return WEAVE_NO_ERROR;
      }
    }
  }
  return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
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
