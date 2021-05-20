// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_BLE_MANAGER_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_BLE_MANAGER_IMPL_H_

#if WEAVE_DEVICE_CONFIG_ENABLE_WOBLE

#include <fuchsia/bluetooth/gatt/cpp/fidl.h>
#include <fuchsia/bluetooth/le/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

namespace testing {
class BLEManagerTest;
}  // namespace testing

namespace {
// Maximum length of device name - To avoid advertisement failure due to large advertise data.
constexpr int kMaxDeviceNameLength = 23;

// The application has enabled WoBLE advertising.
constexpr int kFlag_AdvertisingEnabled = 0x0001;
// The application has enabled fast advertising.
constexpr int kFlag_FastAdvertisingEnabled = 0x0002;
// The system is currently WoBLE advertising.
constexpr int kFlag_Advertising = 0x0004;
// The application has configured a custom BLE device name.
constexpr int kFlag_UseCustomDeviceName = 0x0008;
// The application has published Weave GATT service
constexpr int kFlag_GATTServicePublished = 0x0010;
}  // namespace

/**
 * Concrete implementation of the NetworkProvisioningServer singleton object for the Fuchsia
 * platform.
 */
class BLEManagerImpl final : public BLEManager,
                             private ::nl::Ble::BleLayer,
                             private BlePlatformDelegate,
                             private BleApplicationDelegate,
                             private fuchsia::bluetooth::gatt::LocalServiceDelegate {
  // Allow the BLEManager interface class to delegate method calls to
  // the implementation methods provided by this class.
  friend BLEManager;
  friend class testing::BLEManagerTest;

  // ===== Members that implement the BLEManager internal interface.

  WEAVE_ERROR _Init();
  WoBLEServiceMode _GetWoBLEServiceMode();
  WEAVE_ERROR _SetWoBLEServiceMode(WoBLEServiceMode service_mode);
  bool _IsAdvertisingEnabled();
  WEAVE_ERROR _SetAdvertisingEnabled(bool advertising_enable);
  bool _IsFastAdvertisingEnabled();
  WEAVE_ERROR _SetFastAdvertisingEnabled(bool fast_advertising_enable);
  bool _IsAdvertising();
  WEAVE_ERROR _GetDeviceName(char *device_name, size_t device_name_size);
  WEAVE_ERROR _SetDeviceName(const char *device_name);
  uint16_t _NumConnections() { return 0; }
  static void _OnPlatformEvent(const WeaveDeviceEvent *event);
  ::nl::Ble::BleLayer *_GetBleLayer() const;

  // ===== Members that implement virtual methods on BlePlatformDelegate.

  bool SubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID *svcId,
                               const WeaveBleUUID *charId) override {
    return false;
  }
  bool UnsubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID *svcId,
                                 const WeaveBleUUID *charId) override {
    return false;
  }
  bool CloseConnection(BLE_CONNECTION_OBJECT conId) override { return false; }
  uint16_t GetMTU(BLE_CONNECTION_OBJECT conId) const override { return 0; }
  bool SendIndication(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID *svcId,
                      const WeaveBleUUID *charId, PacketBuffer *data) override;
  bool SendWriteRequest(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID *svcId,
                        const WeaveBleUUID *charId, PacketBuffer *pBuf) override {
    return false;
  }
  bool SendReadRequest(BLE_CONNECTION_OBJECT conId, const WeaveBleUUID *svcId,
                       const WeaveBleUUID *charId, PacketBuffer *pBuf) override {
    return false;
  }
  bool SendReadResponse(BLE_CONNECTION_OBJECT conId, BLE_READ_REQUEST_CONTEXT requestContext,
                        const WeaveBleUUID *svcId, const WeaveBleUUID *charId) override {
    return false;
  };

  // ===== Members that implement virtual methods on BleApplicationDelegate.

  void NotifyWeaveConnectionClosed(BLE_CONNECTION_OBJECT conId) override {}

  // ===== Members that implement virtual methods on LocalServiceDelegate

  void OnCharacteristicConfiguration(uint64_t characteristic_id, std::string peer_id, bool notify,
                                     bool indicate) override;
  void OnReadValue(uint64_t id, int32_t offset, OnReadValueCallback callback) override;
  void OnWriteValue(uint64_t id, uint16_t offset, std::vector<uint8_t> value,
                    OnWriteValueCallback callback) override;
  void OnWriteWithoutResponse(uint64_t id, uint16_t offset, std::vector<uint8_t> value) override {}

  // Drives the global |BLEManagerImpl| instance's BLE state.
  // This method will be scheduled on and called from platform manager's event loop.
  // |arg| holds pointer to BLEManagerImpl instance for which this method is scheduled.
  static void DriveBLEState(intptr_t arg);

  // Drives the weave BLE service.
  // This method publishes and advertises weave service if configured to do so.
  void DriveBLEState(void);

  // ===== Members for internal use by the following friends.

  friend BLEManager &BLEMgr(void);
  friend BLEManagerImpl &BLEMgrImpl(void);

  static BLEManagerImpl sInstance;

  // ===== Private members reserved for use by this class only.

  struct WoBLEConState {
    WoBLEConState(BLEManagerImpl *connection_instance) : instance(connection_instance) {}
    BLEManagerImpl *instance;
    PacketBuffer *pending_ind_buf;
    std::string peer_id;
  };

  WoBLEConState woble_connection_;

  char device_name_[kMaxDeviceNameLength + 1];
  uint16_t flags_;
  WoBLEServiceMode service_mode_;

  // Proxy to the gatt.Server service.
  fuchsia::bluetooth::gatt::ServerSyncPtr gatt_server_;
  fidl::Binding<fuchsia::bluetooth::gatt::LocalServiceDelegate> gatt_binding_;
  fuchsia::bluetooth::gatt::LocalServiceSyncPtr service_;
  // Proxy to the le.Peripheral service which we use for advertising to solicit connections.
  fuchsia::bluetooth::le::PeripheralSyncPtr peripheral_;
  fidl::InterfacePtr<fuchsia::bluetooth::le::AdvertisingHandle> adv_handle_;

 public:
  BLEManagerImpl();
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
  return service_mode_;
}

inline bool BLEManagerImpl::_IsAdvertisingEnabled(void) {
  return GetFlag(flags_, kFlag_AdvertisingEnabled);
}

inline bool BLEManagerImpl::_IsFastAdvertisingEnabled(void) { return false; }

inline bool BLEManagerImpl::_IsAdvertising(void) { return GetFlag(flags_, kFlag_Advertising); }

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // WEAVE_DEVICE_CONFIG_ENABLE_WOBLE

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_BLE_MANAGER_IMPL_H_
