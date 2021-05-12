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

namespace network {
namespace testing {

constexpr uint16_t kRxDepth = 16;
constexpr uint16_t kTxDepth = 16;
constexpr uint16_t kDefaultDescriptorCount = 256;
constexpr uint64_t kDefaultBufferLength = ZX_PAGE_SIZE / 2;
constexpr uint64_t kAutoReturnRxLength = 512;

class RxReturnTransaction;
class TxReturnTransaction;

template <typename T>
class AnyBuffer {
 public:
  using VmoProvider = fit::function<zx::unowned_vmo(uint8_t)>;

  AnyBuffer() {
    memset(&buffer_, 0x00, sizeof(T));
    buffer_.data.parts_list = parts_.data();
  }

  explicit AnyBuffer(const T* buff) {
    buffer_ = *buff;
    for (size_t i = 0; i < buffer_.data.parts_count; i++) {
      parts_[i] = buffer_.data.parts_list[i];
    }
    buffer_.data.parts_list = parts_.data();
  }

  const T& buff() const { return buffer_; }

 protected:
  T buffer_{};
  internal::BufferParts parts_{};
};

class TxBuffer : public fbl::DoublyLinkedListable<std::unique_ptr<TxBuffer>>,
                 public AnyBuffer<tx_buffer_t> {
 public:
  explicit TxBuffer(const tx_buffer_t* b) : AnyBuffer<tx_buffer_t>(b), status_(ZX_OK) {}

  zx_status_t status() const { return status_; }

  void set_status(zx_status_t status) { status_ = status; }

  zx_status_t GetData(std::vector<uint8_t>* copy, const VmoProvider& vmo_provider);

 private:
  zx_status_t status_ = ZX_OK;
};

class RxBuffer : public fbl::DoublyLinkedListable<std::unique_ptr<RxBuffer>>,
                 public AnyBuffer<rx_space_buffer_t> {
 public:
  explicit RxBuffer(const rx_space_buffer_t* buff) : AnyBuffer<rx_space_buffer_t>(buff) {
    FillReturn();
  }

  ~RxBuffer() { free(info_); }

  rx_buffer_t& return_buffer() { return return_; }

  zx_status_t WriteData(const std::vector<uint8_t>& data, const VmoProvider& vmo_provider) {
    return WriteData(&data[0], data.size(), vmo_provider);
  }

  zx_status_t WriteData(const uint8_t* data, size_t len, const VmoProvider& vmo_provider);

 private:
  void FillReturn();

  rx_buffer_t return_{};
  void* info_ = nullptr;
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
  FakeNetworkPortImpl();
  ~FakeNetworkPortImpl();

  void NetworkPortGetInfo(port_info_t* out_info);
  void NetworkPortGetStatus(port_status_t* out_status);
  void NetworkPortSetActive(bool active);
  void NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc);
  void NetworkPortRemoved();

  port_info_t& port_info() { return port_info_; }
  const port_status_t& status() const { return status_; }
  void AddPort(uint8_t port_id, ddk::NetworkDeviceIfcProtocolClient* ifc_client);
  void SetMac(mac_addr_protocol_t proto) { mac_proto_ = proto; }

  network_port_protocol_t protocol() {
    return {
        .ops = &network_port_protocol_ops_,
        .ctx = this,
    };
  }

  bool active() const { return port_active_; }
  bool removed() const { return port_removed_; }

  const zx::event& events() const { return event_; }

 protected:
  friend FakeNetworkDeviceImpl;
  void SetStatus(port_status_t status) { status_ = status; }

 private:
  std::array<uint8_t, netdev::wire::kMaxFrameTypes> rx_types_;
  std::array<tx_support_t, netdev::wire::kMaxFrameTypes> tx_types_;
  mac_addr_protocol_t mac_proto_{};
  port_info_t port_info_{};
  std::atomic_bool port_active_ = false;
  port_status_t status_{};
  zx::event event_;
  bool port_removed_ = false;
  bool port_added_ = false;
};

class FakeNetworkDeviceImpl : public ddk::NetworkDeviceImplProtocol<FakeNetworkDeviceImpl> {
 public:
  // TODO(https://fxbug.dev/64310): Remove hard coded port 0.
  static constexpr uint8_t kPort0 = 0;
  FakeNetworkDeviceImpl();
  ~FakeNetworkDeviceImpl();

  zx::status<std::unique_ptr<NetworkDeviceInterface>> CreateChild(async_dispatcher_t* dispatcher);

  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface);
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie);
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie);
  void NetworkDeviceImplGetInfo(device_info_t* out_info);
  void NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list, size_t buf_count);
  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buf_list, size_t buf_count);
  void NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo) {
    zx::vmo& slot = vmos_[vmo_id];
    EXPECT_FALSE(slot.is_valid()) << "vmo " << static_cast<uint32_t>(vmo_id) << " already prepared";
    slot = std::move(vmo);
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

  FakeNetworkPortImpl& port0() { return port0_; }

  std::unique_ptr<RxBuffer> PopRxBuffer() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    return rx_buffers_.pop_front();
  }

  std::unique_ptr<TxBuffer> PopTxBuffer() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    return tx_buffers_.pop_front();
  }

  fbl::DoublyLinkedList<std::unique_ptr<TxBuffer>> TakeTxBuffers() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    fbl::DoublyLinkedList<std::unique_ptr<TxBuffer>> r;
    tx_buffers_.swap(r);
    return r;
  }

  fbl::DoublyLinkedList<std::unique_ptr<RxBuffer>> TakeRxBuffers() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    fbl::DoublyLinkedList<std::unique_ptr<RxBuffer>> r;
    rx_buffers_.swap(r);
    return r;
  }

  size_t rx_buffer_count() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    return rx_buffers_.size_slow();
  }

  size_t tx_buffer_count() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    return tx_buffers_.size_slow();
  }

  void set_auto_start(bool auto_start) { auto_start_ = auto_start; }

  void set_auto_stop(bool auto_stop) { auto_stop_ = auto_stop; }

  bool TriggerStart();
  bool TriggerStop();

  void SetOnline(bool online);
  void SetStatus(const port_status_t& status);

  network_device_impl_protocol_t proto() {
    return network_device_impl_protocol_t{.ops = &network_device_impl_protocol_ops_, .ctx = this};
  }

  void set_immediate_return_tx(bool auto_return) { immediate_return_tx_ = auto_return; }
  void set_immediate_return_rx(bool auto_return) { immediate_return_rx_ = auto_return; }

  ddk::NetworkDeviceIfcProtocolClient& client() { return device_client_; }

  fbl::Span<const zx::vmo> vmos() { return fbl::Span(vmos_.begin(), vmos_.end()); }

 private:
  fbl::Mutex lock_;
  FakeNetworkPortImpl port0_;
  std::array<zx::vmo, MAX_VMOS> vmos_;
  device_info_t info_{};
  ddk::NetworkDeviceIfcProtocolClient device_client_;
  fbl::DoublyLinkedList<std::unique_ptr<RxBuffer>> rx_buffers_ __TA_GUARDED(lock_);
  fbl::DoublyLinkedList<std::unique_ptr<TxBuffer>> tx_buffers_ __TA_GUARDED(lock_);
  zx::event event_;
  bool auto_start_ = true;
  bool auto_stop_ = true;
  bool immediate_return_tx_ = false;
  bool immediate_return_rx_ = false;
  bool device_started_ __TA_GUARDED(lock_) = false;
  fit::function<void()> pending_start_callback_ __TA_GUARDED(lock_);
  fit::function<void()> pending_stop_callback_ __TA_GUARDED(lock_);
};

class TestSession {
 public:
  static constexpr uint16_t kDefaultDescriptorCount = 256;
  static constexpr uint64_t kDefaultBufferLength = ZX_PAGE_SIZE / 2;

  TestSession() = default;

  zx_status_t Open(fidl::WireSyncClient<netdev::Device>& netdevice, const char* name,
                   netdev::wire::SessionFlags flags = netdev::wire::SessionFlags::kPrimary,
                   uint16_t num_descriptors = kDefaultDescriptorCount,
                   uint64_t buffer_size = kDefaultBufferLength,
                   fidl::VectorView<netdev::wire::FrameType> frame_types =
                       fidl::VectorView<netdev::wire::FrameType>());

  zx_status_t Init(uint16_t descriptor_count, uint64_t buffer_size);
  zx_status_t GetInfo(netdev::wire::SessionInfo* info);
  void Setup(fidl::ClientEnd<netdev::Session> session, netdev::wire::Fifos fifos);
  zx_status_t SetPaused(bool paused);
  zx_status_t Close();
  zx_status_t WaitClosed(zx::time deadline);
  void ZeroVmo();
  buffer_descriptor_t* ResetDescriptor(uint16_t index);
  buffer_descriptor_t* descriptor(uint16_t index);
  uint8_t* buffer(uint64_t offset);

  zx_status_t FetchRx(uint16_t* descriptors, size_t count, size_t* actual) const;
  zx_status_t FetchTx(uint16_t* descriptors, size_t count, size_t* actual) const;
  zx_status_t SendRx(const uint16_t* descriptor, size_t count, size_t* actual) const;
  zx_status_t SendTx(const uint16_t* descriptor, size_t count, size_t* actual) const;
  zx_status_t SendTxData(uint16_t descriptor_index, const std::vector<uint8_t>& data);

  zx_status_t FetchRx(uint16_t* descriptor) const {
    size_t actual;
    return FetchRx(descriptor, 1, &actual);
  }

  zx_status_t FetchTx(uint16_t* descriptor) const {
    size_t actual;
    return FetchTx(descriptor, 1, &actual);
  }

  zx_status_t SendRx(uint16_t descriptor) const {
    size_t actual;
    return SendRx(&descriptor, 1, &actual);
  }

  zx_status_t SendTx(uint16_t descriptor) const {
    size_t actual;
    return SendTx(&descriptor, 1, &actual);
  }

  fidl::ClientEnd<netdev::Session>& session() { return session_; }

  uint64_t canonical_offset(uint16_t index) const { return buffer_length_ * index; }

  const zx::fifo& tx_fifo() const { return fifos_.tx; }
  const zx::channel& channel() const { return session_.channel(); }

 private:
  uint16_t descriptors_count_{};
  uint64_t buffer_length_{};
  fidl::ClientEnd<netdev::Session> session_;
  zx::vmo data_vmo_;
  fzl::VmoMapper data_;
  zx::vmo descriptors_vmo_;
  fzl::VmoMapper descriptors_;
  netdev::wire::Fifos fifos_;
};

class RxReturnTransaction {
 public:
  explicit RxReturnTransaction(FakeNetworkDeviceImpl* impl) : client_(impl->client()) {}

  void Enqueue(std::unique_ptr<RxBuffer> buffer) {
    return_buffers_[count_++] = buffer->return_buffer();
    buffers_.push_back(std::move(buffer));
  }

  void EnqueueWithSize(std::unique_ptr<RxBuffer> buffer, uint64_t return_length) {
    buffer->return_buffer().length = return_length;
    Enqueue(std::move(buffer));
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
  fbl::DoublyLinkedList<std::unique_ptr<RxBuffer>> buffers_;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(RxReturnTransaction);
};

class TxReturnTransaction {
 public:
  explicit TxReturnTransaction(FakeNetworkDeviceImpl* impl) : client_(impl->client()) {}

  void Enqueue(std::unique_ptr<TxBuffer> buffer) {
    auto* b = &return_buffers_[count_++];
    b->status = buffer->status();
    b->id = buffer->buff().id;
  }

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
