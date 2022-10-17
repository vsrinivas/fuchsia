// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TEST_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TEST_UTIL_H_

#include <lib/zx/event.h>
#include <zircon/device/network.h>

#include <memory>
#include <vector>

#include <fbl/intrusive_double_list.h>
#include <gtest/gtest.h>

#include "definitions.h"
#include "device_interface.h"
#include "test_session.h"

namespace network {
namespace testing {

constexpr uint16_t kRxDepth = 16;
constexpr uint16_t kTxDepth = 16;
constexpr uint16_t kDefaultDescriptorCount = 256;
constexpr uint64_t kDefaultBufferLength = ZX_PAGE_SIZE / 2;
constexpr uint32_t kAutoReturnRxLength = 512;

class RxReturnTransaction;
class TxReturnTransaction;
using VmoProvider = fit::function<zx::unowned_vmo(uint8_t)>;

class TxBuffer : public fbl::DoublyLinkedListable<std::unique_ptr<TxBuffer>> {
 public:
  explicit TxBuffer(const tx_buffer_t& buffer) : buffer_(buffer) {
    for (size_t i = 0; i < buffer_.data_count; i++) {
      parts_[i] = buffer_.data_list[i];
    }
    buffer_.data_list = parts_.data();
  }

  zx_status_t status() const { return status_; }

  void set_status(zx_status_t status) { status_ = status; }

  zx::result<std::vector<uint8_t>> GetData(const VmoProvider& vmo_provider);

  tx_result_t result() {
    return {
        .id = buffer_.id,
        .status = status_,
    };
  }

  tx_buffer_t& buffer() { return buffer_; }

 private:
  tx_buffer_t buffer_{};
  internal::BufferParts<buffer_region_t> parts_{};
  zx_status_t status_ = ZX_OK;
};

class RxBuffer : public fbl::DoublyLinkedListable<std::unique_ptr<RxBuffer>> {
 public:
  explicit RxBuffer(const rx_space_buffer_t& space)
      : space_(space),
        return_part_(rx_buffer_part_t{
            .id = space.id,
        }) {}

  zx_status_t WriteData(const std::vector<uint8_t>& data, const VmoProvider& vmo_provider) {
    return WriteData(cpp20::span(data.data(), data.size()), vmo_provider);
  }

  zx_status_t WriteData(cpp20::span<const uint8_t> data, const VmoProvider& vmo_provider);

  rx_buffer_part_t& return_part() { return return_part_; }
  rx_space_buffer_t& space() { return space_; }

  void SetReturnLength(uint32_t length) { return_part_.length = length; }

 private:
  rx_space_buffer_t space_{};
  rx_buffer_part_t return_part_{};
};

class RxReturn : public fbl::DoublyLinkedListable<std::unique_ptr<RxReturn>> {
 public:
  RxReturn()
      : buffer_(rx_buffer_t{
            .meta =
                {
                    .info_type = static_cast<uint32_t>(netdev::wire::InfoType::kNoInfo),
                    .frame_type = static_cast<uint8_t>(netdev::wire::FrameType::kEthernet),
                },
            .data_list = parts_.begin(),
            .data_count = 0,
        }) {}
  // RxReturn can't be moved because it keeps pointers to the return buffer internally.
  RxReturn(RxReturn&&) = delete;
  RxReturn(std::unique_ptr<RxBuffer> buffer, uint8_t port_id) : RxReturn() {
    PushPart(std::move(buffer));
    buffer_.meta.port = port_id;
  }

  // Pushes buffer space into the return buffer.
  //
  // NB: We don't really need the unique pointer here, we just copy the information we need. But
  // requiring the unique pointer to be passed enforces the buffer ownership semantics. Also
  // RxBuffers usually sit in the available queue as a pointer already.
  void PushPart(std::unique_ptr<RxBuffer> buffer) {
    ZX_ASSERT(buffer_.data_count < parts_.size());
    parts_[buffer_.data_count++] = buffer->return_part();
  }

  const rx_buffer_t& buffer() const { return buffer_; }
  rx_buffer_t& buffer() { return buffer_; }

 private:
  internal::BufferParts<rx_buffer_part_t> parts_{};
  rx_buffer_t buffer_{};
};

constexpr zx_signals_t kEventStart = ZX_USER_SIGNAL_0;
constexpr zx_signals_t kEventStop = ZX_USER_SIGNAL_1;
constexpr zx_signals_t kEventTx = ZX_USER_SIGNAL_2;
constexpr zx_signals_t kEventSessionStarted = ZX_USER_SIGNAL_3;
constexpr zx_signals_t kEventRxAvailable = ZX_USER_SIGNAL_4;
constexpr zx_signals_t kEventPortRemoved = ZX_USER_SIGNAL_5;
constexpr zx_signals_t kEventPortActiveChanged = ZX_USER_SIGNAL_6;

class FakeNetworkDeviceImpl;

class FakeNetworkPortImpl : public ddk::NetworkPortProtocol<FakeNetworkPortImpl> {
 public:
  using OnSetActiveCallback = fit::function<void(bool)>;
  FakeNetworkPortImpl();
  ~FakeNetworkPortImpl();

  void NetworkPortGetInfo(port_info_t* out_info);
  void NetworkPortGetStatus(port_status_t* out_status);
  void NetworkPortSetActive(bool active);
  void NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc);
  void NetworkPortRemoved();

  port_info_t& port_info() { return port_info_; }
  const port_status_t& status() const { return status_; }
  void AddPort(uint8_t port_id, ddk::NetworkDeviceIfcProtocolClient ifc_client);
  void RemoveSync();
  void SetMac(mac_addr_protocol_t proto) { mac_proto_ = proto; }
  void SetOnSetActiveCallback(OnSetActiveCallback cb) { on_set_active_ = std::move(cb); }

  network_port_protocol_t protocol() {
    return {
        .ops = &network_port_protocol_ops_,
        .ctx = this,
    };
  }

  bool active() const { return port_active_; }
  bool removed() const { return port_removed_; }
  uint8_t id() const { return id_; }

  const zx::event& events() const { return event_; }

  void SetOnline(bool online);
  void SetStatus(const port_status_t& status);

 private:
  using OnRemovedCallback = fit::callback<void()>;

  DISALLOW_COPY_ASSIGN_AND_MOVE(FakeNetworkPortImpl);

  std::array<uint8_t, netdev::wire::kMaxFrameTypes> rx_types_;
  std::array<tx_support_t, netdev::wire::kMaxFrameTypes> tx_types_;
  ddk::NetworkDeviceIfcProtocolClient device_client_;
  OnRemovedCallback on_removed_;
  OnSetActiveCallback on_set_active_;
  uint8_t id_;
  mac_addr_protocol_t mac_proto_{};
  port_info_t port_info_{};
  std::atomic_bool port_active_ = false;
  port_status_t status_;
  zx::event event_;
  bool port_removed_ = false;
  bool port_added_ = false;
};

class FakeNetworkDeviceImpl : public ddk::NetworkDeviceImplProtocol<FakeNetworkDeviceImpl> {
 public:
  using PrepareVmoHandler =
      fit::function<void(uint8_t, const zx::vmo&, network_device_impl_prepare_vmo_callback, void*)>;
  FakeNetworkDeviceImpl();
  ~FakeNetworkDeviceImpl();

  zx::result<std::unique_ptr<NetworkDeviceInterface>> CreateChild(async_dispatcher_t* dispatcher);

  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface);
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie);
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie);
  void NetworkDeviceImplGetInfo(device_info_t* out_info);
  void NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list, size_t buf_count);
  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buf_list, size_t buf_count);
  void NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo,
                                   network_device_impl_prepare_vmo_callback callback,
                                   void* cookie) {
    zx::vmo& slot = vmos_[vmo_id];
    EXPECT_FALSE(slot.is_valid()) << "vmo " << static_cast<uint32_t>(vmo_id) << " already prepared";
    slot = std::move(vmo);
    if (prepare_vmo_handler_) {
      prepare_vmo_handler_(vmo_id, slot, callback, cookie);
    } else {
      callback(cookie, ZX_OK);
    }
  }
  void NetworkDeviceImplReleaseVmo(uint8_t vmo_id) {
    zx::vmo& slot = vmos_[vmo_id];
    EXPECT_TRUE(slot.is_valid()) << "vmo " << static_cast<uint32_t>(vmo_id) << " already released";
    slot.reset();
  }
  void NetworkDeviceImplSetSnoop(bool snoop) { /* do nothing , only auto-snooping is allowed */
  }

  fit::function<zx::unowned_vmo(uint8_t)> VmoGetter();

  const zx::event& events() const { return event_; }

  device_info_t& info() { return info_; }

  std::unique_ptr<RxBuffer> PopRxBuffer() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    return rx_buffers_.pop_front();
  }

  std::unique_ptr<TxBuffer> PopTxBuffer() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    return tx_buffers_.pop_front();
  }

  fbl::SizedDoublyLinkedList<std::unique_ptr<TxBuffer>> TakeTxBuffers() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    fbl::SizedDoublyLinkedList<std::unique_ptr<TxBuffer>> r;
    tx_buffers_.swap(r);
    return r;
  }

  fbl::SizedDoublyLinkedList<std::unique_ptr<RxBuffer>> TakeRxBuffers() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    fbl::SizedDoublyLinkedList<std::unique_ptr<RxBuffer>> r;
    rx_buffers_.swap(r);
    return r;
  }

  size_t rx_buffer_count() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    return rx_buffers_.size();
  }

  size_t tx_buffer_count() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    return tx_buffers_.size();
  }

  std::optional<uint8_t> first_vmo_id() {
    for (size_t i = 0; i < vmos_.size(); i++) {
      if (vmos_[i].is_valid()) {
        return i;
      }
    }
    return std::nullopt;
  }

  void set_auto_start(std::optional<zx_status_t> auto_start) { auto_start_ = auto_start; }

  void set_auto_stop(bool auto_stop) { auto_stop_ = auto_stop; }

  bool TriggerStart();
  bool TriggerStop();

  network_device_impl_protocol_t proto() {
    return network_device_impl_protocol_t{.ops = &network_device_impl_protocol_ops_, .ctx = this};
  }

  void set_immediate_return_tx(bool auto_return) { immediate_return_tx_ = auto_return; }
  void set_immediate_return_rx(bool auto_return) { immediate_return_rx_ = auto_return; }
  void set_prepare_vmo_handler(PrepareVmoHandler handler) {
    prepare_vmo_handler_ = std::move(handler);
  }

  ddk::NetworkDeviceIfcProtocolClient& client() { return device_client_; }

  cpp20::span<const zx::vmo> vmos() { return cpp20::span(vmos_.begin(), vmos_.end()); }

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(FakeNetworkDeviceImpl);

  fbl::Mutex lock_;
  std::array<zx::vmo, MAX_VMOS> vmos_;
  device_info_t info_{};
  ddk::NetworkDeviceIfcProtocolClient device_client_;
  fbl::SizedDoublyLinkedList<std::unique_ptr<RxBuffer>> rx_buffers_ __TA_GUARDED(lock_);
  fbl::SizedDoublyLinkedList<std::unique_ptr<TxBuffer>> tx_buffers_ __TA_GUARDED(lock_);
  zx::event event_;
  std::optional<zx_status_t> auto_start_ = ZX_OK;
  bool auto_stop_ = true;
  bool immediate_return_tx_ = false;
  bool immediate_return_rx_ = false;
  bool device_started_ __TA_GUARDED(lock_) = false;
  fit::function<void()> pending_start_callback_ __TA_GUARDED(lock_);
  fit::function<void()> pending_stop_callback_ __TA_GUARDED(lock_);
  PrepareVmoHandler prepare_vmo_handler_;
};

class RxReturnTransaction {
 public:
  explicit RxReturnTransaction(FakeNetworkDeviceImpl* impl) : client_(impl->client()) {}

  void Enqueue(std::unique_ptr<RxReturn> buffer) {
    return_buffers_[count_++] = buffer->buffer();
    buffers_.push_back(std::move(buffer));
  }

  void Enqueue(std::unique_ptr<RxBuffer> buffer, uint8_t port_id) {
    Enqueue(std::make_unique<RxReturn>(std::move(buffer), port_id));
  }

  void Commit() {
    client_.CompleteRx(return_buffers_, count_);
    count_ = 0;
    buffers_.clear();
  }

 private:
  rx_buffer_t return_buffers_[kRxDepth]{};
  size_t count_ = 0;
  ddk::NetworkDeviceIfcProtocolClient client_;
  fbl::DoublyLinkedList<std::unique_ptr<RxReturn>> buffers_;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(RxReturnTransaction);
};

class TxReturnTransaction {
 public:
  explicit TxReturnTransaction(FakeNetworkDeviceImpl* impl) : client_(impl->client()) {}

  void Enqueue(std::unique_ptr<TxBuffer> buffer) { return_buffers_[count_++] = buffer->result(); }

  void Commit() {
    client_.CompleteTx(return_buffers_, count_);
    count_ = 0;
  }

 private:
  tx_result_t return_buffers_[kRxDepth]{};
  size_t count_ = 0;
  ddk::NetworkDeviceIfcProtocolClient client_;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(TxReturnTransaction);
};

}  // namespace testing
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TEST_UTIL_H_
