// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConfigurationManager.h>
#include <Weave/Core/WeaveKeyIds.h>
#include "src/connectivity/weave/adaptation/configuration_manager_impl.h"
#include "src/connectivity/weave/adaptation/group_key_store_impl.h"
#include <Weave/Profiles/security/WeaveApplicationKeys.h>

#include <Weave/Core/WeaveVendorIdentifiers.hpp>
#include <Weave/DeviceLayer/internal/GenericConfigurationManagerImpl.ipp>
// clang-format on

#include <net/ethernet.h>
#include <src/lib/fxl/logging.h>

namespace nl {
namespace Weave {
namespace DeviceLayer {

namespace {

using Internal::GroupKeyStoreImpl;
using Internal::WeaveConfigManager;

// Singleton instance of Weave Group Key Store for the Fuchsia.
//
// NOTE: This is declared as a private global variable, rather than a static
// member of ConfigurationManagerImpl, to reduce the number of headers that
// must be included by the application when using the ConfigurationManager API.
//
GroupKeyStoreImpl gGroupKeyStore;

constexpr char kDeviceInfoStorePath[] = "/config/data/device_info.json";
constexpr char kDeviceInfoConfigKey_FirmwareRevision[] = "firmware-revision";
constexpr char kDeviceInfoConfigKey_ProductId[] = "product-id";
constexpr char kDeviceInfoConfigKey_VendorId[] = "vendor-id";

}  // unnamed namespace

/* Singleton instance of the ConfigurationManager implementation object for the Fuchsia. */
ConfigurationManagerImpl ConfigurationManagerImpl::sInstance;

ConfigurationManagerImpl::ConfigurationManagerImpl()
    : device_info_(WeaveConfigManager::CreateReadOnlyInstance(kDeviceInfoStorePath)) {}

ConfigurationManagerImpl::ConfigurationManagerImpl(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)),
      device_info_(WeaveConfigManager::CreateReadOnlyInstance(kDeviceInfoStorePath)) {
  FXL_CHECK(_Init() == WEAVE_NO_ERROR) << "Failed to init configuration manager.";
}

WEAVE_ERROR ConfigurationManagerImpl::_Init() {
  WEAVE_ERROR err;
  if (!context_) {
    context_ = sys::ComponentContext::Create();
  }

  FXL_CHECK(context_->svc()->Connect(wlan_device_service_.NewRequest()) == ZX_OK)
      << "Failed to connect to wlan service.";
  FXL_CHECK(context_->svc()->Connect(hwinfo_device_.NewRequest()) == ZX_OK)
      << "Failed to connect to hwinfo device service.";
  FXL_CHECK(context_->svc()->Connect(weave_factory_data_manager_.NewRequest()) == ZX_OK)
      << "Failed to connect to weave factory data manager service.";

  err = ConfigurationManagerImpl::GetAndStoreHWInfo();
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  err = ConfigurationManagerImpl::GetAndStorePairingCode();
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ConfigurationManagerImpl::GetAndStoreHWInfo() {
  fuchsia::hwinfo::DeviceInfo device_info;
  if (ZX_OK == hwinfo_device_->GetInfo(&device_info) && device_info.has_serial_number()) {
    return StoreSerialNumber(device_info.serial_number().c_str(),
                             device_info.serial_number().length());
  }
  return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
}

WEAVE_ERROR ConfigurationManagerImpl::GetAndStorePairingCode() {
  fuchsia::weave::FactoryDataManager_GetPairingCode_Result pairing_code_result;
  fuchsia::weave::FactoryDataManager_GetPairingCode_Response pairing_code_response;
  std::string pairing_code;
  char read_value[kMaxPairingCodeLength + 1];
  size_t read_value_size = 0;
  WEAVE_ERROR err;

  zx_status_t status = weave_factory_data_manager_->GetPairingCode(&pairing_code_result);
  if (ZX_OK != status || !pairing_code_result.is_response()) {
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }

  pairing_code_response = pairing_code_result.response();
  if (pairing_code_response.pairing_code.size() > kMaxPairingCodeLength) {
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }

  err = StorePairingCode((const char*)pairing_code_response.pairing_code.data(),
                         pairing_code_response.pairing_code.size());

  if (WEAVE_NO_ERROR != err) {
    return err;
  }

  // Device pairing code can be overridden with configured value for testing.
  // Current unit tests only look for this configured value. To ensure code coverage
  // in unit tests device pairing code is read and stored even if a pairing code
  // is configured for test. TODO: fxb/49671
  err = device_info_->ReadConfigValueStr(kConfigKey_PairingCode, read_value,
                                         kMaxPairingCodeLength + 1, &read_value_size);
  if (err == WEAVE_NO_ERROR) {
    return StorePairingCode(read_value, read_value_size);
  }

  // return no error, will continue to use device pairing code
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ConfigurationManagerImpl::_GetVendorId(uint16_t& vendor_id) {
  return device_info_->ReadConfigValue(kDeviceInfoConfigKey_VendorId, &vendor_id);
}

WEAVE_ERROR ConfigurationManagerImpl::_GetProductId(uint16_t& product_id) {
  return device_info_->ReadConfigValue(kDeviceInfoConfigKey_ProductId, &product_id);
}

WEAVE_ERROR ConfigurationManagerImpl::_GetFirmwareRevision(char* buf, size_t buf_size,
                                                           size_t& out_len) {
  return device_info_->ReadConfigValueStr(kDeviceInfoConfigKey_FirmwareRevision, buf, buf_size,
                                          &out_len);
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

void ConfigurationManagerImpl::_InitiateFactoryReset() { EnvironmentConfig::FactoryResetConfig(); }

WEAVE_ERROR ConfigurationManagerImpl::_ReadPersistedStorageValue(
    ::nl::Weave::Platform::PersistedStorage::Key key, uint32_t& value) {
  WEAVE_ERROR err = ReadConfigValue(key, value);
  return (err == WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND)
             ? WEAVE_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND
             : err;
}

WEAVE_ERROR ConfigurationManagerImpl::_WritePersistedStorageValue(
    ::nl::Weave::Platform::PersistedStorage::Key key, uint32_t value) {
  WEAVE_ERROR err = WriteConfigValue(key, value);
  return (err != WEAVE_NO_ERROR) ? WEAVE_ERROR_PERSISTED_STORAGE_FAIL : err;
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
