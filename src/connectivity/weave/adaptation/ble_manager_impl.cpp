// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/BLEManager.h>
// clang-format on

#include <lib/syslog/cpp/macros.h>

#if WEAVE_DEVICE_CONFIG_ENABLE_WOBLE

#define MAX_CHARACTERISTIC_UUID_SIZE 40

using namespace ::nl;
using namespace ::nl::Ble;

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

namespace {

/// UUID of weave service obtained from SIG, in canonical 8-4-4-4-12 string format.
constexpr char kServiceUuid[] = "0000FEAF-0000-1000-8000-00805F9B34FB";

// Define structure that holds both the canonical and WeaveBLEUUID format.
struct BLECharUUID {
  const char canonical_uuid[MAX_CHARACTERISTIC_UUID_SIZE];
  const WeaveBleUUID weave_uuid;
};

// Offsets into |kWeaveBleChars| for specific characteristic.
enum WeaveBleChar {
  // Weave service characteristic C1(write)
  kWeaveBleCharWrite = 0,
  // Weave service characteristic C2(indicate)
  kWeaveBleCharIndicate = 1,
};

// An array that holds the UUID for each |WeaveBleChar|
constexpr BLECharUUID kWeaveBleChars[] = {
    // UUID for |kWeaveBleCharWrite|
    {"18EE2EF5-263D-4559-959F-4F9C429F9D11",
     {{0x18, 0xEE, 0x2E, 0xF5, 0x26, 0x3D, 0x45, 0x59, 0x95, 0x9F, 0x4F, 0x9C, 0x42, 0x9F, 0x9D,
       0x11}}},
    // UUID for |kWeaveBleCharIndicate|
    {"18EE2EF5-263D-4559-959F-4F9C429F9D12",
     {{0x18, 0xEE, 0x2E, 0xF5, 0x26, 0x3D, 0x45, 0x59, 0x95, 0x9F, 0x4F, 0x9C, 0x42, 0x9F, 0x9D,
       0x12}}}};

}  // unnamed namespace

BLEManagerImpl BLEManagerImpl::sInstance;

BLEManagerImpl::BLEManagerImpl() : woble_connection_(this), gatt_binding_(this) {
  // BleLayer does not initialize this callback, set it NULL to avoid accessing location pointed by
  // garbage value when not set explicitly.
  OnWeaveBleConnectReceived = NULL;
}

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

  flags_ = 0;
  if (ConfigurationMgrImpl().IsWoBLEAdvertisementEnabled()) {
    flags_ = kFlag_AdvertisingEnabled;
  }

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
  // TODO(fxbug.dev/52734): Enable bluetooth fast advertising when needed
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

void BLEManagerImpl::_OnPlatformEvent(const WeaveDeviceEvent* event) {
  BLEManagerImpl* instance;
  WoBLEConState* connection_state;
  if (event == NULL) {
    // Ignore null weave device event.
    return;
  }
  switch (event->Type) {
    case DeviceEventType::kWoBLESubscribe:
      connection_state = static_cast<WoBLEConState*>(event->WoBLESubscribe.ConId);
      ZX_ASSERT_MSG(connection_state != NULL,
                    "Received WoBLE subscribe event without connection state");
      instance = connection_state->instance;
      ZX_ASSERT_MSG(instance != NULL, "Received WoBLE subscribe event with NULL instance");
      instance->HandleSubscribeReceived(
          event->WoBLESubscribe.ConId, &WEAVE_BLE_SVC_ID,
          &kWeaveBleChars[WeaveBleChar::kWeaveBleCharIndicate].weave_uuid);

      // Post a WoBLEConnectionEstablished event to the DeviceLayer
      WeaveDeviceEvent connection_established_event;
      connection_established_event.Type = DeviceEventType::kWoBLEConnectionEstablished;
      PlatformMgr().PostEvent(&connection_established_event);

      break;

    case DeviceEventType::kWoBLEUnsubscribe:
      connection_state = static_cast<WoBLEConState*>(event->WoBLEUnsubscribe.ConId);
      ZX_ASSERT_MSG(connection_state != NULL,
                    "Received WoBLE unsubscribe event without connection state");
      instance = connection_state->instance;
      ZX_ASSERT_MSG(instance != NULL, "Received WoBLE unsubscribe event with NULL instance");
      instance->HandleUnsubscribeReceived(
          event->WoBLEUnsubscribe.ConId, &WEAVE_BLE_SVC_ID,
          &kWeaveBleChars[WeaveBleChar::kWeaveBleCharIndicate].weave_uuid);
      break;
    case DeviceEventType::kWoBLEWriteReceived:
      connection_state = static_cast<WoBLEConState*>(event->WoBLEWriteReceived.ConId);
      ZX_ASSERT_MSG(connection_state != NULL,
                    "Received WoBLE write event without connection state");
      instance = connection_state->instance;
      ZX_ASSERT_MSG(instance != NULL, "Received WoBLE write event with NULL instance");
      instance->HandleWriteReceived(event->WoBLEWriteReceived.ConId, &WEAVE_BLE_SVC_ID,
                                    &kWeaveBleChars[WeaveBleChar::kWeaveBleCharWrite].weave_uuid,
                                    event->WoBLEWriteReceived.Data);
      break;
    case DeviceEventType::kWoBLEIndicateConfirm:
      connection_state = static_cast<WoBLEConState*>(event->WoBLEIndicateConfirm.ConId);
      ZX_ASSERT_MSG(connection_state != NULL,
                    "Received WoBLE indication confirmation event without connection state");
      instance = connection_state->instance;
      ZX_ASSERT_MSG(instance != NULL,
                    "Received WoBLE indication confirmation event with NULL instance");
      instance->HandleIndicationConfirmation(
          event->WoBLEIndicateConfirm.ConId, &WEAVE_BLE_SVC_ID,
          &kWeaveBleChars[WeaveBleChar::kWeaveBleCharIndicate].weave_uuid);
      break;
    case DeviceEventType::kWoBLEConnectionError:
      connection_state = static_cast<WoBLEConState*>(event->WoBLEConnectionError.ConId);
      ZX_ASSERT(connection_state != NULL);
      instance = connection_state->instance;
      ZX_ASSERT(instance != NULL);
      instance->HandleConnectionError(event->WoBLEConnectionError.ConId,
                                      event->WoBLEConnectionError.Reason);
      break;
    default:
      // Ignore events not intended for BLEManager.
      break;
  }
}

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

bool BLEManagerImpl::SendIndication(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID* svcId,
                                    const WeaveBleUUID* charId, PacketBuffer* data) {
  auto connection_state = static_cast<WoBLEConState*>(conId);
  if (!connection_state) {
    PacketBuffer::Free(data);
    return false;
  }

  std::vector<uint8_t> value(data->DataLength());
  std::copy(data->Start(), data->Start() + data->DataLength(), value.begin());

  WEAVE_ERROR err =
      service_->NotifyValue(kWeaveBleCharIndicate, connection_state->peer_id, value, true);
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "SendIndication failed: " << err;
    PacketBuffer::Free(data);
    return false;
  }

  // Save a reference to the buffer until we get a indication for the notification.
  connection_state->pending_ind_buf = data;
  data = NULL;

  // TODO(fxbug.dev/53070, fxbug.dev/53966): The peer confirmation currently isn't returned to the caller.
  // Proceed as if the confirmation is received, to avoid closing the connection. When the bug
  // is fixed, block until the confirmation is received and handle it.
  PacketBuffer::Free(connection_state->pending_ind_buf);
  // If the confirmation was successful...
  // Post an event to the Weave queue to process the indicate confirmation.
  WeaveDeviceEvent event;
  event.Type = DeviceEventType::kWoBLEIndicateConfirm;
  event.WoBLESubscribe.ConId = conId;
  PlatformMgr().PostEvent(&event);

  return true;
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

    fuchsia::bluetooth::gatt::Characteristic weave_characteristic_c1;
    weave_characteristic_c1.id = WeaveBleChar::kWeaveBleCharWrite;
    weave_characteristic_c1.type = kWeaveBleChars[WeaveBleChar::kWeaveBleCharWrite].canonical_uuid;
    weave_characteristic_c1.properties = fuchsia::bluetooth::gatt::kPropertyWrite;
    weave_characteristic_c1.permissions = fuchsia::bluetooth::gatt::AttributePermissions::New();
    weave_characteristic_c1.permissions->write =
        fuchsia::bluetooth::gatt::SecurityRequirements::New();

    fuchsia::bluetooth::gatt::Characteristic weave_characteristic_c2;
    weave_characteristic_c2.id = WeaveBleChar::kWeaveBleCharIndicate;
    weave_characteristic_c2.type =
        kWeaveBleChars[WeaveBleChar::kWeaveBleCharIndicate].canonical_uuid;
    weave_characteristic_c2.properties =
        fuchsia::bluetooth::gatt::kPropertyRead | fuchsia::bluetooth::gatt::kPropertyIndicate;
    weave_characteristic_c2.permissions = fuchsia::bluetooth::gatt::AttributePermissions::New();
    weave_characteristic_c2.permissions->read =
        fuchsia::bluetooth::gatt::SecurityRequirements::New();
    weave_characteristic_c2.permissions->update =
        fuchsia::bluetooth::gatt::SecurityRequirements::New();

    std::vector<fuchsia::bluetooth::gatt::Characteristic> characteristics;
    characteristics.push_back(std::move(weave_characteristic_c1));
    characteristics.push_back(std::move(weave_characteristic_c2));

    gatt_service_info.primary = true;
    gatt_service_info.type = kServiceUuid;
    gatt_service_info.characteristics = std::move(characteristics);
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
        FX_LOGS(ERROR) << "Failed to get BLE device name prefix: " << err;
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

void BLEManagerImpl::OnCharacteristicConfiguration(uint64_t characteristic_id, std::string peer_id,
                                                   bool notify, bool indicate) {
  FX_LOGS(INFO) << "CharacteristicConfiguration on peer " << peer_id << " (notify: " << notify
                << ", indicate: " << indicate << ") characteristic id " << characteristic_id;

  // Post an event to the Weave queue to process either a WoBLE Subscribe or Unsubscribe based on
  // whether the client is enabling or disabling indications.
  WeaveDeviceEvent event;
  event.Type = (indicate) ? DeviceEventType::kWoBLESubscribe : DeviceEventType::kWoBLEUnsubscribe;
  woble_connection_.peer_id = peer_id;
  event.WoBLESubscribe.ConId = static_cast<void*>(&woble_connection_);
  PlatformMgr().PostEvent(&event);
}

void BLEManagerImpl::OnReadValue(uint64_t id, int32_t offset, OnReadValueCallback callback) {
  callback({}, fuchsia::bluetooth::gatt::ErrorCode::NOT_PERMITTED);
}

void BLEManagerImpl::OnWriteValue(uint64_t id, uint16_t offset, std::vector<uint8_t> value,
                                  OnWriteValueCallback callback) {
  PacketBuffer* buf = NULL;
  FX_LOGS(INFO) << "Write request received for characteristic id " << id << " at offset " << offset
                << " length " << value.size();
  if (id != kWeaveBleCharWrite) {
    FX_LOGS(WARNING) << "Ignoring writes to characteristic other than weave characteristic "
                        "C1(write). Expected characteristic: "
                     << kWeaveBleCharWrite;
    callback(fuchsia::bluetooth::gatt::ErrorCode::NOT_PERMITTED);
    return;
  }
  if (offset != 0) {
    FX_LOGS(ERROR) << "No offset expected on write to control point";
    callback(fuchsia::bluetooth::gatt::ErrorCode::INVALID_OFFSET);
    return;
  }

  // Copy the data to a PacketBuffer.
  buf = PacketBuffer::New(0);
  if (!buf || buf->AvailableDataLength() < value.size()) {
    FX_LOGS(ERROR) << "Buffer not available";
    callback(fuchsia::bluetooth::gatt::ErrorCode::INVALID_VALUE_LENGTH);
    PacketBuffer::Free(buf);
    return;
  }

  std::copy(value.begin(), value.end(), buf->Start());
  buf->SetDataLength(value.size());

  // Post an event to the Weave queue to deliver the data into the Weave stack.
  WeaveDeviceEvent event;
  event.Type = DeviceEventType::kWoBLEWriteReceived;
  event.WoBLEWriteReceived.ConId = static_cast<void*>(&woble_connection_);
  event.WoBLEWriteReceived.Data = buf;
  PlatformMgr().PostEvent(&event);
  buf = NULL;

  callback(fuchsia::bluetooth::gatt::ErrorCode::NO_ERROR);
}

void BLEManagerImpl::OnWriteWithoutResponse(uint64_t id, uint16_t offset,
                                            std::vector<uint8_t> value) {}

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // WEAVE_DEVICE_CONFIG_ENABLE_WOBLE
