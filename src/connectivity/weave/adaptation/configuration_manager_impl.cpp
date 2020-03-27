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

constexpr char kDeviceConfigKey_VendorId[] = "vendor-id";
constexpr char kDeviceConfigKey_ProductId[] = "product-id";
constexpr char kDeviceConfigKey_FirmwareRevision[] = "firmware-revision";
constexpr char kWeaveDeviceConfigPath[] = "/config/data/device_info.json";

}  // unnamed namespace

/* Singleton instance of the ConfigurationManager implementation object for the Fuchsia. */
ConfigurationManagerImpl ConfigurationManagerImpl::sInstance;

ConfigurationManagerImpl::ConfigurationManagerImpl()
    : config_data_reader_(WeaveConfigManager::CreateReadOnlyInstance(kWeaveDeviceConfigPath)) {}

ConfigurationManagerImpl::ConfigurationManagerImpl(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)),
      config_data_reader_(WeaveConfigManager::CreateReadOnlyInstance(kWeaveDeviceConfigPath)) {
  FXL_CHECK(_Init() == WEAVE_NO_ERROR) << "Failed to init configuration manager.";
}

WEAVE_ERROR ConfigurationManagerImpl::_Init() {
  WEAVE_ERROR err;
  if (!context_) {
    context_ = sys::ComponentContext::Create();
  }

  FXL_CHECK(context_->svc()->Connect(wlan_device_service_.NewRequest()) == ZX_OK)
      << "Failed to connect to wlan service.";
  FXL_CHECK(context_->svc()->Connect(hwinfo_device_ptr_.NewRequest()) == ZX_OK)
      << "Failed to connect to hwinfo device service.";

  err = ConfigurationManagerImpl::GetAndStoreHWInfo();
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ConfigurationManagerImpl::GetAndStoreHWInfo() {
  fuchsia::hwinfo::DeviceInfo device_info;
  FXL_CHECK(ZX_OK == hwinfo_device_ptr_->GetInfo(&device_info))
      << "Failed to get device information";
  if (device_info.has_serial_number()) {
    return StoreSerialNumber(device_info.serial_number().c_str(),
                             device_info.serial_number().length());
  }
  return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
}

WEAVE_ERROR ConfigurationManagerImpl::_GetVendorId(uint16_t& vendorId) {
  return config_data_reader_->ReadConfigValue(kDeviceConfigKey_VendorId, &vendorId);
}

WEAVE_ERROR ConfigurationManagerImpl::_GetProductId(uint16_t& productId) {
  return config_data_reader_->ReadConfigValue(kDeviceConfigKey_ProductId, &productId);
}

WEAVE_ERROR ConfigurationManagerImpl::_GetFirmwareRevision(char* buf, size_t bufSize,
                                                           size_t& outLen) {
  return config_data_reader_->ReadConfigValueStr(kDeviceConfigKey_FirmwareRevision, buf, bufSize,
                                                 &outLen);
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

::nl::Weave::Profiles::Security::AppKeys::GroupKeyStoreBase*
ConfigurationManagerImpl::_GetGroupKeyStore() {
  return &gGroupKeyStore;
}

bool ConfigurationManagerImpl::_CanFactoryReset() { return true; }

void ConfigurationManagerImpl::_InitiateFactoryReset() { FuchsiaConfig::FactoryResetConfig(); }

WEAVE_ERROR ConfigurationManagerImpl::_ReadPersistedStorageValue(
    ::nl::Weave::Platform::PersistedStorage::Key key, uint32_t& value) {
  WEAVE_ERROR err = ReadConfigValue(key, value);
  if (err == WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    err = WEAVE_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND;
  }
  return err;
}

WEAVE_ERROR ConfigurationManagerImpl::_WritePersistedStorageValue(
    ::nl::Weave::Platform::PersistedStorage::Key key, uint32_t value) {
  return WriteConfigValue(key, value);
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
