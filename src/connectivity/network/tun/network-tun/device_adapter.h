// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_DEVICE_ADAPTER_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_DEVICE_ADAPTER_H_

#include <fuchsia/hardware/network/llcpp/fidl.h>

#include <array>
#include <queue>

#include <fbl/mutex.h>

#include "buffer.h"
#include "config.h"
#include "mac_adapter.h"
#include "src/connectivity/network/drivers/network-device/device/public/network_device.h"

namespace network {
namespace tun {

class DeviceAdapter;

// An abstract DeviceAdapter parent.
//
// This abstract class allows the owner of a `DeviceAdapter` to change its behavior and be notified
// of important events.
class DeviceAdapterParent : public MacAdapterParent {
 public:
  // Gets the DeviceAdapter's configuration.
  virtual const BaseConfig& config() const = 0;
  // Called when the device's `has_session` state changes.
  virtual void OnHasSessionsChanged(DeviceAdapter* device) = 0;
  // Called when transmit buffers become available.
  virtual void OnTxAvail(DeviceAdapter* device) = 0;
  // Called when receive buffers become available.
  virtual void OnRxAvail(DeviceAdapter* device) = 0;
};

// An entity that instantiates a `NetworkDeviceInterface` and provides an implementations of
// `fuchsia.hardware.network.device.NetworkDeviceImpl` that grants access to the buffers exchanged
// with the interface.
//
// `DeviceAdapter` is used to provide the business logic of virtual NetworkDevice implementations
// both for `tun.Device` and `tun.DevicePair` device classes.
// `DeviceAdapter` maintains the buffer nomenclature used by the DeviceInterface, that is: A "Tx"
// buffer is a buffer that contains data that is expected to be sent over a link, and an "Rx" buffer
// is free space that can be used to write data received over a link and push it back to
// applications.
class DeviceAdapter : public ddk::NetworkDeviceImplProtocol<DeviceAdapter>,
                      public ddk::NetworkPortProtocol<DeviceAdapter> {
 public:
  // TODO(http://fxbug.dev/64310): Do not automatically install ports once tun FIDL supports it.
  static constexpr uint8_t kPort0 = 0;
  // Creates a new `DeviceAdapter` with  `parent`, that will serve its requests through
  // `dispatcher`.
  // If `online` is true, the device starts with its virtual link in the online status.
  // If `mac` is provided, the device will provide a MAC adderessing layer.
  static zx::status<std::unique_ptr<DeviceAdapter>> Create(
      async_dispatcher_t* dispatcher, DeviceAdapterParent* parent, bool online,
      std::optional<fuchsia_net::wire::MacAddress> mac);

  // Binds `req` to this adapter's `NetworkDeviceInterface`.
  zx_status_t Bind(fidl::ServerEnd<netdev::Device> req);

  // Tears down this adapter and calls `callback` when teardown is finished.
  // Tearing down causes all client channels to be closed.
  // There are no guarantees over which thread `callback` is called.
  // It is invalid to attempt to tear down a device that is already tearing down or is already torn
  // down.
  void Teardown(fit::function<void()> callback);
  // Same as `Teardown`, but blocks until teardown is complete.
  void TeardownSync();

  // NetworkDeviceImpl protocol:
  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface);
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie);
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie);
  void NetworkDeviceImplGetInfo(device_info_t* out_info);
  void NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list, size_t buf_count);
  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buf_list, size_t buf_count);
  void NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo);
  void NetworkDeviceImplReleaseVmo(uint8_t vmo_id);
  void NetworkDeviceImplSetSnoop(bool snoop) { /* do nothing , only auto-snooping is allowed */
  }

  // NetworkPort protocol:
  void NetworkPortGetInfo(port_info_t* out_info);
  void NetworkPortGetStatus(port_status_t* out_status);
  void NetworkPortSetActive(bool active);
  void NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc);
  void NetworkPortRemoved();

  // Sets this device's emulated `online` status.
  void SetOnline(bool online);
  // Returns `true` if the device has at least one active session.
  bool HasSession();
  // Attempts to get a pending transmit buffer containing data expected to reach the network from
  // the pool of pending buffers.
  // The second argument given to `callback` is the number of remaining pending buffers (not
  // including the one given to it).
  // Returns `true` if a buffer was successfully allocated. The buffer given to `callback` is
  // discarded from the list of pending buffers and marked as pending for return.
  bool TryGetTxBuffer(fit::callback<zx_status_t(Buffer*, size_t)> callback);
  // Attempts to write `data` and `meta` into an available rx buffer and return it to the
  // `NetworkDeviceInterface`.
  // Returns the number of remaining available buffers.
  // Returns `ZX_ERR_BAD_STATE` if the device is offline, or `ZX_ERR_SHOULD_WAIT` if there are no
  // buffers available to write `data` into
  zx::status<size_t> WriteRxFrame(fuchsia_hardware_network::wire::FrameType frame_type,
                                  const uint8_t* data, size_t count,
                                  const std::optional<fuchsia_net_tun::wire::FrameMetadata>& meta);
  zx::status<size_t> WriteRxFrame(fuchsia_hardware_network::wire::FrameType frame_type,
                                  const fidl::VectorView<uint8_t>& data,
                                  const std::optional<fuchsia_net_tun::wire::FrameMetadata>& meta);
  zx::status<size_t> WriteRxFrame(fuchsia_hardware_network::wire::FrameType frame_type,
                                  const std::vector<uint8_t>& data,
                                  const std::optional<fuchsia_net_tun::wire::FrameMetadata>& meta);
  // Copies all pending tx buffers from `this` consuming any available rx buffers from `other`.
  // If `return_failed_buffers` is `true`, all buffers from `this` that couldn't be immediately
  // copied into available buffers from `other` will be returned to applications in a failure state,
  // otherwise buffers from `this` will remain in the available buffer pool.
  void CopyTo(DeviceAdapter* other, bool return_failed_buffers);

  // Gets the MAC adapter associated with the device.
  //
  // May be null if the device was created without a MAC layer.
  const std::unique_ptr<MacAdapter>& mac() const { return mac_; }

 private:
  DeviceAdapter(DeviceAdapterParent* parent, bool online);

  // Enqueues a single fulfilled rx frame.
  void EnqueueRx(fuchsia_hardware_network::wire::FrameType frame_type, uint32_t buffer_id,
                 uint32_t length, const std::optional<fuchsia_net_tun::wire::FrameMetadata>& meta)
      __TA_REQUIRES(rx_lock_);
  // Commits all pending rx buffers, returning them to the `NetworkDeviceInterface`.
  void CommitRx() __TA_REQUIRES(rx_lock_);
  // Enqueues a single consumed tx frame.
  // `status` indicates the success or failure when consuming the frame, as dictated by the
  // `NetworkDeviceInterface` contract.
  void EnqueueTx(uint32_t id, zx_status_t status) __TA_REQUIRES(tx_lock_);
  // Commits all pending tx frames, returning them to the `NetworkDeviceInterface`.
  void CommitTx() __TA_REQUIRES(tx_lock_);

  std::unique_ptr<NetworkDeviceInterface> device_;
  DeviceAdapterParent* const parent_;  // pointer to parent, not owned.
  fbl::Mutex state_lock_;
  bool has_sessions_ __TA_GUARDED(state_lock_) = false;
  bool online_ __TA_GUARDED(state_lock_) = false;

  fbl::Mutex rx_lock_;
  fbl::Mutex tx_lock_;
  // NOTE: VmoStore is not thread safe in itself. Our concurrency guarantees come from the
  // `NetworkDeviceInterface` contract, where VMOs are released once we've returned all the buffers.
  // If we wrap it in a lock, or add thread safety to `VmoStore`, we end up with unnecessary added
  // complexity and also we lose the benefit of being able to operate on rx and tx frames without
  // shared locks between them.
  VmoStore vmos_;
  std::array<uint8_t, fuchsia_hardware_network::wire::kMaxFrameTypes> rx_types_{};
  std::array<tx_support_t, fuchsia_hardware_network::wire::kMaxFrameTypes> tx_types_{};
  std::queue<Buffer> tx_buffers_ __TA_GUARDED(tx_lock_);
  bool tx_available_ __TA_GUARDED(tx_lock_) = false;
  std::queue<Buffer> rx_buffers_ __TA_GUARDED(rx_lock_);
  bool rx_available_ __TA_GUARDED(rx_lock_) = false;
  std::vector<rx_buffer_t> return_rx_list_ __TA_GUARDED(rx_lock_);
  std::vector<tx_result_t> return_tx_list_ __TA_GUARDED(tx_lock_);
  ddk::NetworkDeviceIfcProtocolClient device_iface_;
  const device_info_t device_info_;
  // TODO(https://fxbug.dev/64310): Remove this field once network-tun supports dynamic ports.
  const port_info_t port_info_;
  std::unique_ptr<MacAdapter> mac_;
};
}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_DEVICE_ADAPTER_H_
