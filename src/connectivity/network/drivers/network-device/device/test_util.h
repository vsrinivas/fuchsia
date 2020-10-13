// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TEST_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TEST_UTIL_H_

#include <memory>
#include <vector>

#include <fbl/intrusive_double_list.h>

#include "definitions.h"
#include "device_interface.h"

namespace network {
namespace testing {

constexpr uint16_t kRxDepth = 16;
constexpr uint16_t kTxDepth = 16;
constexpr uint16_t kDefaultDescriptorCount = 256;
constexpr uint64_t kDefaultBufferLength = ZX_PAGE_SIZE / 2;

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

  zx_status_t status() { return status_; }

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

  bool has_data() const { return return_.total_length != 0; }

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

class FakeNetworkDeviceImpl : public ddk::NetworkDeviceImplProtocol<FakeNetworkDeviceImpl> {
 public:
  FakeNetworkDeviceImpl();
  ~FakeNetworkDeviceImpl();

  zx_status_t CreateChild(async_dispatcher_t* dispatcher,
                          std::unique_ptr<NetworkDeviceInterface>* out);

  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface);
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie);
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie);
  void NetworkDeviceImplGetInfo(device_info_t* out_info);
  void NetworkDeviceImplGetStatus(status_t* out_status);
  void NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list, size_t buf_count);
  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buf_list, size_t buf_count);
  void NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo) { vmos_[vmo_id] = std::move(vmo); }
  void NetworkDeviceImplReleaseVmo(uint8_t vmo_id) { vmos_[vmo_id].reset(); }
  void NetworkDeviceImplSetSnoop(bool snoop) { /* do nothing , only auto-snooping is allowed */
  }

  fit::function<zx::unowned_vmo(uint8_t)> VmoGetter();
  void ReturnAllTx();

  zx::event& events() { return event_; }

  device_info_t& info() { return info_; }

  const status_t& status() const { return status_; }

  fbl::DoublyLinkedList<std::unique_ptr<RxBuffer>>& rx_buffers() { return rx_buffers_; }

  fbl::DoublyLinkedList<std::unique_ptr<TxBuffer>>& tx_buffers() { return tx_buffers_; }

  void set_auto_start(bool auto_start) { auto_start_ = auto_start; }

  void set_auto_stop(bool auto_stop) { auto_stop_ = auto_stop; }

  bool TriggerStart();
  bool TriggerStop();

  void SetOnline(bool online);
  void SetStatus(const status_t& status);

  network_device_impl_protocol_t proto() {
    return network_device_impl_protocol_t{.ops = &network_device_impl_protocol_ops_, .ctx = this};
  }

  void set_auto_return_tx(bool auto_return) { auto_return_tx_ = auto_return; }

  ddk::NetworkDeviceIfcProtocolClient& client() { return device_client_; }

 private:
  std::array<zx::vmo, MAX_VMOS> vmos_;
  std::array<uint8_t, netdev::MAX_FRAME_TYPES> rx_types_;
  std::array<tx_support_t, netdev::MAX_FRAME_TYPES> tx_types_;
  device_info_t info_{};
  status_t status_{};
  ddk::NetworkDeviceIfcProtocolClient device_client_;
  fbl::DoublyLinkedList<std::unique_ptr<RxBuffer>> rx_buffers_;
  fbl::DoublyLinkedList<std::unique_ptr<TxBuffer>> tx_buffers_;
  zx::event event_;
  bool auto_start_ = true;
  bool auto_stop_ = true;
  bool auto_return_tx_ = false;
  fit::function<void()> pending_start_callback_;
  fit::function<void()> pending_stop_callback_;
};

class TestSession {
 public:
  static constexpr uint16_t kDefaultDescriptorCount = 256;
  static constexpr uint64_t kDefaultBufferLength = ZX_PAGE_SIZE / 2;

  TestSession() = default;

  zx_status_t Open(
      zx::unowned_channel netdevice, const char* name,
      netdev::SessionFlags flags = netdev::SessionFlags::PRIMARY,
      uint16_t num_descriptors = kDefaultDescriptorCount,
      uint64_t buffer_size = kDefaultBufferLength,
      fidl::VectorView<netdev::FrameType> frame_types = fidl::VectorView<netdev::FrameType>());

  zx_status_t Init(uint16_t descriptor_count, uint64_t buffer_size);
  zx_status_t GetInfo(netdev::SessionInfo* info);
  void Setup(zx::channel session, netdev::Fifos fifos);
  zx_status_t SetPaused(bool paused);
  zx_status_t Close();
  zx_status_t WaitClosed(zx::time deadline);
  void ZeroVmo();
  buffer_descriptor_t* ResetDescriptor(uint16_t index);
  buffer_descriptor_t* descriptor(uint16_t index);
  uint8_t* buffer(uint64_t offset);

  zx_status_t FetchRx(uint16_t* descriptors, size_t count, size_t* actual);
  zx_status_t FetchTx(uint16_t* descriptors, size_t count, size_t* actual);
  zx_status_t SendRx(const uint16_t* descriptor, size_t count, size_t* actual);
  zx_status_t SendTx(const uint16_t* descriptor, size_t count, size_t* actual);
  zx_status_t SendTxData(uint16_t descriptor_index, const std::vector<uint8_t>& data);

  zx_status_t FetchRx(uint16_t* descriptor) {
    size_t actual;
    return FetchRx(descriptor, 1, &actual);
  }

  zx_status_t FetchTx(uint16_t* descriptor) {
    size_t actual;
    return FetchTx(descriptor, 1, &actual);
  }

  zx_status_t SendRx(uint16_t descriptor) {
    size_t actual;
    return SendRx(&descriptor, 1, &actual);
  }

  zx_status_t SendTx(uint16_t descriptor) {
    size_t actual;
    return SendTx(&descriptor, 1, &actual);
  }

  zx::channel& channel() { return session_; }

  uint64_t canonical_offset(uint16_t index) { return buffer_length_ * index; }

 private:
  uint16_t descriptors_count_{};
  uint64_t buffer_length_{};
  zx::channel session_;
  zx::vmo data_vmo_;
  fzl::VmoMapper data_;
  zx::vmo descriptors_vmo_;
  fzl::VmoMapper descriptors_;
  netdev::Fifos fifos_;
};

class RxReturnTransaction {
 public:
 public:
  explicit RxReturnTransaction(FakeNetworkDeviceImpl* impl) : client_(impl->client()) {}

  void Enqueue(std::unique_ptr<RxBuffer> buffer) {
    return_buffers_[count_++] = buffer->return_buffer();
    buffers_.push_back(std::move(buffer));
  }

  void EnqueueWithSize(std::unique_ptr<RxBuffer> buffer, uint64_t return_length) {
    buffer->return_buffer().total_length = return_length;
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
