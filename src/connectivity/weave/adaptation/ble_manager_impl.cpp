// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/BLEManager.h>
// clang-format on

#if WEAVE_DEVICE_CONFIG_ENABLE_WOBLE

using namespace ::nl;
using namespace ::nl::Ble;

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

BLEManagerImpl BLEManagerImpl::sInstance;

WEAVE_ERROR BLEManagerImpl::_Init() { return WEAVE_NO_ERROR; }

WEAVE_ERROR BLEManagerImpl::_SetWoBLEServiceMode(WoBLEServiceMode val) { return WEAVE_NO_ERROR; }

WEAVE_ERROR BLEManagerImpl::_SetAdvertisingEnabled(bool val) { return WEAVE_NO_ERROR; }

WEAVE_ERROR BLEManagerImpl::_SetFastAdvertisingEnabled(bool val) { return WEAVE_NO_ERROR; }

WEAVE_ERROR BLEManagerImpl::_GetDeviceName(char* buf, size_t bufSize) { return WEAVE_NO_ERROR; }

WEAVE_ERROR BLEManagerImpl::_SetDeviceName(const char* deviceName) { return WEAVE_NO_ERROR; }

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

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // WEAVE_DEVICE_CONFIG_ENABLE_WOBLE
