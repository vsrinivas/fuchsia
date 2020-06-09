// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/BLEManager.h>
// clang-format on

#include <lib/syslog/cpp/macros.h>

#if WEAVE_DEVICE_CONFIG_ENABLE_WOBLE

using namespace ::nl;
using namespace ::nl::Ble;

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

namespace {

/// UUID of weave service obtained from SIG, in canonical 8-4-4-4-12 string format.
constexpr char kServiceUuid[] = "0000FEAF-0000-1000-8000-00805F9B34FB";

}  // unnamed namespace

BLEManagerImpl BLEManagerImpl::sInstance;

BLEManagerImpl::BLEManagerImpl() : gatt_binding_(this) {}

WEAVE_ERROR BLEManagerImpl::_Init() {
  WEAVE_ERROR err;
  auto svc = PlatformMgrImpl().GetComponentContextForProcess()->svc();

  FX_CHECK(svc->Connect(gatt_server_.NewRequest()) == ZX_OK)
      << "Failed to connect to " << fuchsia::bluetooth::gatt::Server::Name_;
  FX_CHECK(svc->Connect(peripheral_.NewRequest()) == ZX_OK)
      << "Failed to connect to " << fuchsia::bluetooth::le::Peripheral::Name_;

  adv_handle_.set_error_handler(
      [](zx_status_t status) { FX_LOGS(INFO) << "LE advertising was stopped: " << status; });

  if (ConfigurationMgrImpl().IsWoBLEEnabled()) {
    service_mode_ = ConnectivityManager::kWoBLEServiceMode_Enabled;
  }

  memset(device_name_, 0, sizeof(device_name_));

  flags_ = kFlag_AdvertisingEnabled;

  // Initialize the Weave BleLayer.
  err = BleLayer::Init(this, this, &SystemLayer);
  if (err != BLE_NO_ERROR) {
    FX_LOGS(ERROR) << "BLE Layer init failed: " << ErrorStr(err);
    return err;
  }

  PlatformMgr().ScheduleWork(DriveBLEState, reinterpret_cast<intptr_t>(this));
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR BLEManagerImpl::_SetWoBLEServiceMode(WoBLEServiceMode service_mode) {
  if (service_mode == ConnectivityManager::kWoBLEServiceMode_NotSupported) {
    return WEAVE_ERROR_INVALID_ARGUMENT;
  }
  if (service_mode_ == ConnectivityManager::kWoBLEServiceMode_NotSupported) {
    return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
  }
  if (service_mode != service_mode_) {
    service_mode_ = service_mode;
    PlatformMgr().ScheduleWork(DriveBLEState, reinterpret_cast<intptr_t>(this));
  }
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR BLEManagerImpl::_SetAdvertisingEnabled(bool advertising_enable) {
  if (service_mode_ == ConnectivityManager::kWoBLEServiceMode_NotSupported) {
    return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
  }

  if (GetFlag(flags_, kFlag_AdvertisingEnabled) != advertising_enable) {
    SetFlag(flags_, kFlag_AdvertisingEnabled, advertising_enable);
    PlatformMgr().ScheduleWork(DriveBLEState, reinterpret_cast<intptr_t>(this));
  }
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR BLEManagerImpl::_SetFastAdvertisingEnabled(bool fast_advertising_enable) {
  if (service_mode_ == ConnectivityManager::kWoBLEServiceMode_NotSupported) {
    return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
  }
  // TODO(fxb/52734): Enable bluetooth fast advertising when needed
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR BLEManagerImpl::_GetDeviceName(char* device_name, size_t device_name_size) {
  if (strlen(device_name_) >= device_name_size) {
    return WEAVE_ERROR_BUFFER_TOO_SMALL;
  }
  strncpy(device_name, device_name_, device_name_size);
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR BLEManagerImpl::_SetDeviceName(const char* device_name) {
  if (service_mode_ == ConnectivityManager::kWoBLEServiceMode_NotSupported) {
    return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
  }
  if (device_name != NULL && device_name[0] != 0) {
    if (strlen(device_name) >= kMaxDeviceNameLength) {
      return WEAVE_ERROR_INVALID_ARGUMENT;
    }
    strcpy(device_name_, device_name);
    SetFlag(flags_, kFlag_UseCustomDeviceName);
  } else {
    device_name_[0] = 0;
    ClearFlag(flags_, kFlag_UseCustomDeviceName);
  }
  return WEAVE_NO_ERROR;
}

void BLEManagerImpl::_OnPlatformEvent(const WeaveDeviceEvent* event) {}

uint16_t BLEManagerImpl::_NumConnections(void) { return 0; }

bool BLEManagerImpl::SubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID* svcId,
                                             const WeaveBleUUID* charId) {
  return false;
}

bool BLEManagerImpl::UnsubscribeCharacteristic(BLE_CONNECTION_OBJECT conId,
                                               const WeaveBleUUID* svcId,
                                               const WeaveBleUUID* charId) {
  return false;
}

bool BLEManagerImpl::CloseConnection(BLE_CONNECTION_OBJECT conId) { return false; }

uint16_t BLEManagerImpl::GetMTU(BLE_CONNECTION_OBJECT conId) const { return 0; }

// TODO(fxb/52837): Implement GATT events for weave service
bool BLEManagerImpl::SendIndication(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID* svcId,
                                    const WeaveBleUUID* charId, PacketBuffer* data) {
  return false;
}

bool BLEManagerImpl::SendWriteRequest(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID* svcId,
                                      const WeaveBleUUID* charId, PacketBuffer* pBuf) {
  return false;
}

bool BLEManagerImpl::SendReadRequest(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID* svcId,
                                     const WeaveBleUUID* charId, PacketBuffer* pBuf) {
  return false;
}

bool BLEManagerImpl::SendReadResponse(BLE_CONNECTION_OBJECT conId,
                                      BLE_READ_REQUEST_CONTEXT requestContext,
                                      const WeaveBleUUID* svcId, const WeaveBleUUID* charId) {
  return false;
}

void BLEManagerImpl::NotifyWeaveConnectionClosed(BLE_CONNECTION_OBJECT conId) {}

void BLEManagerImpl::DriveBLEState() {
  if (service_mode_ != ConnectivityManager::kWoBLEServiceMode_Enabled) {
    if (GetFlag(flags_, kFlag_Advertising)) {
      adv_handle_.Unbind();
      ClearFlag(flags_, kFlag_Advertising);
    }
    if (GetFlag(flags_, kFlag_GATTServicePublished)) {
      service_->RemoveService();
      ClearFlag(flags_, kFlag_GATTServicePublished);
    }
    return;
  }

  if (!GetFlag(flags_, kFlag_GATTServicePublished)) {
    fuchsia::bluetooth::Status out_status;
    fuchsia::bluetooth::gatt::ServiceInfo gatt_service_info;
    gatt_service_info.primary = true;
    gatt_service_info.type = kServiceUuid;
    gatt_server_->PublishService(std::move(gatt_service_info), gatt_binding_.NewBinding(),
                                 service_.NewRequest(), &out_status);
    if (out_status.error) {
      FX_LOGS(ERROR) << "Failed to publish GATT service for Weave. Error: "
                     << (bool)out_status.error->error_code << " (" << out_status.error->description
                     << "). Disabling WoBLE service";
      service_mode_ = ConnectivityManager::kWoBLEServiceMode_Disabled;
      return;
    }
    FX_LOGS(INFO) << "Published GATT service for Weave with UUID: " << kServiceUuid;
    SetFlag(flags_, kFlag_GATTServicePublished);
  }

  if (GetFlag(flags_, kFlag_AdvertisingEnabled) && !GetFlag(flags_, kFlag_Advertising)) {
    fuchsia::bluetooth::le::AdvertisingData advertising_data;
    fuchsia::bluetooth::le::AdvertisingParameters advertising_parameters;
    fuchsia::bluetooth::le::Peripheral_StartAdvertising_Result advertising_result;
    fuchsia::bluetooth::Uuid uuid;
    std::copy(std::rbegin(WEAVE_BLE_SVC_ID.bytes), std::rend(WEAVE_BLE_SVC_ID.bytes),
              std::begin(uuid.value));

    // If a custom device name has not been specified, generate a name based on the
    // configured prefix and bottom digits of the Weave device id.
    if (!GetFlag(flags_, kFlag_UseCustomDeviceName)) {
      size_t out_len;
      char device_name_prefix[kMaxDeviceNameLength - 3] = "";
      WEAVE_ERROR err = ConfigurationMgrImpl().GetBleDeviceNamePrefix(
          device_name_prefix, kMaxDeviceNameLength - 4, &out_len);
      if (err != WEAVE_NO_ERROR) {
        FX_LOGS(ERROR) << "Failed to get BLE device name prefix";
      }
      snprintf(device_name_, sizeof(device_name_), "%s%04" PRIX32, device_name_prefix,
               (uint32_t)FabricState.LocalNodeId);
      device_name_[kMaxDeviceNameLength] = 0;
    }
    advertising_data.set_name(device_name_);
    advertising_data.set_service_uuids({{uuid}});

    advertising_parameters.set_connectable(true);
    advertising_parameters.set_data(std::move(advertising_data));
    advertising_parameters.set_mode_hint(fuchsia::bluetooth::le::AdvertisingModeHint::SLOW);

    peripheral_->StartAdvertising(std::move(advertising_parameters), adv_handle_.NewRequest(),
                                  &advertising_result);

    if (advertising_result.is_err()) {
      FX_LOGS(ERROR) << "Failed to advertise WoBLE service, Error: "
                     << (uint32_t)advertising_result.err() << ". Disabling WoBLE service";
      service_mode_ = ConnectivityManager::kWoBLEServiceMode_Disabled;
      return;
    }
    FX_LOGS(INFO) << "Advertising Weave service for device: " << device_name_;
    SetFlag(flags_, kFlag_Advertising);
  }

  if (!GetFlag(flags_, kFlag_AdvertisingEnabled) && GetFlag(flags_, kFlag_Advertising)) {
    adv_handle_.Unbind();
    ClearFlag(flags_, kFlag_Advertising);
  }
}

void BLEManagerImpl::DriveBLEState(intptr_t arg) {
  auto instance = reinterpret_cast<BLEManagerImpl*>(arg);
  if (!instance) {
    FX_LOGS(ERROR) << "DriveBLEState called with NULL";
    return;
  }
  instance->DriveBLEState();
}

// TODO(fxb/52837) Implement GATT events for weave service
void BLEManagerImpl::OnCharacteristicConfiguration(uint64_t characteristic_id, std::string peer_id,
                                                   bool notify, bool indicate) {}
void BLEManagerImpl::OnReadValue(uint64_t id, int32_t offset, OnReadValueCallback callback) {
  callback({}, fuchsia::bluetooth::gatt::ErrorCode::NOT_PERMITTED);
}
void BLEManagerImpl::OnWriteValue(uint64_t id, uint16_t offset, std::vector<uint8_t> value,
                                  OnWriteValueCallback callback) {}
void BLEManagerImpl::OnWriteWithoutResponse(uint64_t id, uint16_t offset,
                                            std::vector<uint8_t> value) {}

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // WEAVE_DEVICE_CONFIG_ENABLE_WOBLE
