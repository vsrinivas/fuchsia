// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_BLE_MANAGER_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_BLE_MANAGER_IMPL_H_

#if WEAVE_DEVICE_CONFIG_ENABLE_WOBLE

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

/**
 * Concrete implementation of the NetworkProvisioningServer singleton object for the Fuchsia
 * platform.
 */
class BLEManagerImpl final : public BLEManager,
                             private ::nl::Ble::BleLayer,
                             private BlePlatformDelegate,
                             private BleApplicationDelegate {
  // Allow the BLEManager interface class to delegate method calls to
  // the implementation methods provided by this class.
  friend BLEManager;

  // ===== Members that implement the BLEManager internal interface.

  WEAVE_ERROR _Init(void);
  WoBLEServiceMode _GetWoBLEServiceMode(void);
  WEAVE_ERROR _SetWoBLEServiceMode(WoBLEServiceMode val);
  bool _IsAdvertisingEnabled(void);
  WEAVE_ERROR _SetAdvertisingEnabled(bool val);
  bool _IsFastAdvertisingEnabled(void);
  WEAVE_ERROR _SetFastAdvertisingEnabled(bool val);
  bool _IsAdvertising(void);
  WEAVE_ERROR _GetDeviceName(char *buf, size_t bufSize);
  WEAVE_ERROR _SetDeviceName(const char *deviceName);
  uint16_t _NumConnections(void);
  void _OnPlatformEvent(const WeaveDeviceEvent *event);
  ::nl::Ble::BleLayer *_GetBleLayer(void) const;

  // ===== Members that implement virtual methods on BlePlatformDelegate.

  bool SubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID *svcId,
                               const WeaveBleUUID *charId) override;
  bool UnsubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID *svcId,
                                 const WeaveBleUUID *charId) override;
  bool CloseConnection(BLE_CONNECTION_OBJECT conId) override;
  uint16_t GetMTU(BLE_CONNECTION_OBJECT conId) const override;
  bool SendIndication(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID *svcId,
                      const WeaveBleUUID *charId, PacketBuffer *pBuf) override;
  bool SendWriteRequest(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID *svcId,
                        const WeaveBleUUID *charId, PacketBuffer *pBuf) override;
  bool SendReadRequest(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID *svcId,
                       const WeaveBleUUID *charId, PacketBuffer *pBuf) override;
  bool SendReadResponse(BLE_CONNECTION_OBJECT conId, BLE_READ_REQUEST_CONTEXT requestContext,
                        const WeaveBleUUID *svcId, const WeaveBleUUID *charId) override;

  // ===== Members that implement virtual methods on BleApplicationDelegate.

  void NotifyWeaveConnectionClosed(BLE_CONNECTION_OBJECT conId) override;

  // ===== Members for internal use by the following friends.

  friend BLEManager &BLEMgr(void);
  friend BLEManagerImpl &BLEMgrImpl(void);

  static BLEManagerImpl sInstance;

  // ===== Private members reserved for use by this class only.

  enum { kMaxConnections = BLE_LAYER_NUM_BLE_ENDPOINTS, kMaxDeviceNameLength = 16 };

  struct WoBLEConState {
    PacketBuffer *PendingIndBuf;
    uint16_t ConId;
    uint16_t MTU : 10;
    uint16_t Allocated : 1;
    uint16_t Subscribed : 1;
    uint16_t Unused : 4;
  };

  WoBLEServiceMode mServiceMode;
};

/**
 * Returns a reference to the public interface of the BLEManager singleton object.
 *
 * Internal components should use this to access features of the BLEManager object
 * that are common to all platforms.
 */
inline BLEManager &BLEMgr(void) { return BLEManagerImpl::sInstance; }

/**
 * Returns the platform-specific implementation of the BLEManager singleton object.
 *
 * Internal components can use this to gain access to features of the BLEManager
 * that are specific to the Fuchsia platform.
 */
inline BLEManagerImpl &BLEMgrImpl(void) { return BLEManagerImpl::sInstance; }

inline ::nl::Ble::BleLayer *BLEManagerImpl::_GetBleLayer() const { return (BleLayer *)(this); }

inline BLEManager::WoBLEServiceMode BLEManagerImpl::_GetWoBLEServiceMode(void) {
  return mServiceMode;
}

inline bool BLEManagerImpl::_IsAdvertisingEnabled(void) { return false; }

inline bool BLEManagerImpl::_IsFastAdvertisingEnabled(void) { return false; }

inline bool BLEManagerImpl::_IsAdvertising(void) { return false; }

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // WEAVE_DEVICE_CONFIG_ENABLE_WOBLE

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_BLE_MANAGER_IMPL_H_
