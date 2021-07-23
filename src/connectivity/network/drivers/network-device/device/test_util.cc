// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_util.h"

#include <iostream>

#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"

namespace network {
namespace testing {

zx::status<std::vector<uint8_t>> TxBuffer::GetData(const VmoProvider& vmo_provider) {
  if (!vmo_provider) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  // We don't support copying chained buffers.
  if (buffer_.data_count != 1) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  const buffer_region_t& region = buffer_.data_list[0];
  zx::unowned_vmo vmo = vmo_provider(region.vmo);
  if (!vmo->is_valid()) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  std::vector<uint8_t> copy;
  copy.resize(region.length);
  zx_status_t status = vmo->read(copy.data(), region.offset, region.length);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(copy));
}

zx_status_t RxBuffer::WriteData(fbl::Span<const uint8_t> data, const VmoProvider& vmo_provider) {
  if (!vmo_provider) {
    return ZX_ERR_INTERNAL;
  }
  if (data.size() > space_.region.length) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx::unowned_vmo vmo = vmo_provider(space_.region.vmo);
  return_part_.length = static_cast<uint32_t>(data.size());
  return vmo->write(data.begin(), space_.region.offset, data.size());
}

FakeNetworkPortImpl::FakeNetworkPortImpl()
    : port_info_({
          .port_class = static_cast<uint8_t>(netdev::wire::DeviceClass::kEthernet),
          .rx_types_list = rx_types_.data(),
          .rx_types_count = 1,
          .tx_types_list = tx_types_.data(),
          .tx_types_count = 1,
      }) {
  rx_types_[0] = static_cast<uint8_t>(netdev::wire::FrameType::kEthernet);
  tx_types_[0] = {
      .type = static_cast<uint8_t>(netdev::wire::FrameType::kEthernet),
      .features = netdev::wire::kFrameFeaturesRaw,
      .supported_flags = 0,
  };
  EXPECT_OK(zx::event::create(0, &event_));
}

FakeNetworkPortImpl::~FakeNetworkPortImpl() {
  if (port_added_) {
    EXPECT_TRUE(port_removed_) << "port was added but remove was not called";
  }
}

void FakeNetworkPortImpl::NetworkPortGetInfo(port_info_t* out_info) { *out_info = port_info_; }

void FakeNetworkPortImpl::NetworkPortGetStatus(port_status_t* out_status) { *out_status = status_; }

void FakeNetworkPortImpl::NetworkPortSetActive(bool active) {
  port_active_ = active;
  if (on_set_active_) {
    on_set_active_(active);
  }
  ASSERT_OK(event_.signal(0, kEventPortActiveChanged));
}

void FakeNetworkPortImpl::NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc) {
  *out_mac_ifc = mac_proto_;
}

void FakeNetworkPortImpl::NetworkPortRemoved() {
  EXPECT_FALSE(port_removed_) << "removed same port twice";
  port_removed_ = true;
  if (on_removed_) {
    on_removed_();
  }
}

void FakeNetworkPortImpl::AddPort(uint8_t port_id, ddk::NetworkDeviceIfcProtocolClient ifc_client) {
  ASSERT_FALSE(port_added_) << "can't add the same port object twice";
  id_ = port_id;
  port_added_ = true;
  device_client_ = ifc_client;
  ifc_client.AddPort(port_id, this, &network_port_protocol_ops_);
}

void FakeNetworkPortImpl::RemoveSync() {
  // Already removed.
  if (!port_added_ || port_removed_) {
    return;
  }
  sync_completion_t signal;
  on_removed_ = [&signal]() { sync_completion_signal(&signal); };
  device_client_.RemovePort(id_);
  sync_completion_wait(&signal, zx::time::infinite().get());
}

void FakeNetworkPortImpl::SetOnline(bool online) {
  port_status_t status = status_;
  status.flags = static_cast<uint32_t>(online ? netdev::wire::StatusFlags::kOnline
                                              : netdev::wire::StatusFlags());
  SetStatus(status);
}

void FakeNetworkPortImpl::SetStatus(const port_status_t& status) {
  status_ = status;
  if (device_client_.is_valid()) {
    device_client_.PortStatusChanged(id_, &status);
  }
}

FakeNetworkDeviceImpl::FakeNetworkDeviceImpl()
    : ddk::NetworkDeviceImplProtocol<FakeNetworkDeviceImpl>(),
      info_({
          .tx_depth = kTxDepth,
          .rx_depth = kRxDepth,
          .rx_threshold = kRxDepth / 2,
          .max_buffer_length = ZX_PAGE_SIZE / 2,
          .buffer_alignment = ZX_PAGE_SIZE,
      }) {
  EXPECT_OK(zx::event::create(0, &event_));
}

FakeNetworkDeviceImpl::~FakeNetworkDeviceImpl() {
  // ensure that all VMOs were released
  for (auto& vmo : vmos_) {
    ZX_ASSERT(!vmo.is_valid());
  }
}

zx_status_t FakeNetworkDeviceImpl::NetworkDeviceImplInit(
    const network_device_ifc_protocol_t* iface) {
  device_client_ = ddk::NetworkDeviceIfcProtocolClient(iface);
  return ZX_OK;
}

void FakeNetworkDeviceImpl::NetworkDeviceImplStart(network_device_impl_start_callback callback,
                                                   void* cookie) {
  fbl::AutoLock lock(&lock_);
  EXPECT_FALSE(device_started_) << "called start on already started device";
  device_started_ = true;
  if (auto_start_) {
    callback(cookie);
  } else {
    ZX_ASSERT(!(pending_start_callback_ || pending_stop_callback_));
    pending_start_callback_ = [cookie, callback]() { callback(cookie); };
  }
  EXPECT_OK(event_.signal(0, kEventStart));
}

void FakeNetworkDeviceImpl::NetworkDeviceImplStop(network_device_impl_stop_callback callback,
                                                  void* cookie) {
  fbl::AutoLock lock(&lock_);
  EXPECT_TRUE(device_started_) << "called stop on already stopped device";
  device_started_ = false;
  zx_signals_t clear;
  if (auto_stop_) {
    RxReturnTransaction rx_return(this);
    while (!rx_buffers_.is_empty()) {
      std::unique_ptr rx_buffer = rx_buffers_.pop_front();
      // Return unfulfilled buffers with zero length and an invalid port number.
      // Zero length buffers are returned to the pool and the port metadata is ignored.
      rx_buffer->return_part().length = 0;
      rx_return.Enqueue(std::move(rx_buffer), MAX_PORTS);
    }
    rx_return.Commit();

    TxReturnTransaction tx_return(this);
    while (!tx_buffers_.is_empty()) {
      std::unique_ptr tx_buffer = tx_buffers_.pop_front();
      tx_buffer->set_status(ZX_ERR_UNAVAILABLE);
      tx_return.Enqueue(std::move(tx_buffer));
    }
    tx_return.Commit();
    callback(cookie);
    // Must clear the queue signals if we're clearing the queues automatically.
    clear = kEventTx | kEventRxAvailable;
  } else {
    ZX_ASSERT(!(pending_start_callback_ || pending_stop_callback_));
    pending_stop_callback_ = [cookie, callback]() { callback(cookie); };
    clear = 0;
  }
  EXPECT_OK(event_.signal(clear, kEventStop));
}

void FakeNetworkDeviceImpl::NetworkDeviceImplGetInfo(device_info_t* out_info) { *out_info = info_; }

void FakeNetworkDeviceImpl::NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list,
                                                     size_t buf_count) {
  EXPECT_NE(buf_count, 0u);
  ASSERT_TRUE(device_client_.is_valid());

  fbl::AutoLock lock(&lock_);
  fbl::Span buffers(buf_list, buf_count);
  if (immediate_return_tx_ || !device_started_) {
    const zx_status_t return_status = device_started_ ? ZX_OK : ZX_ERR_UNAVAILABLE;
    ASSERT_TRUE(buf_count < kTxDepth);
    std::array<tx_result_t, kTxDepth> results;
    auto r = results.begin();
    for (const tx_buffer_t& buff : buffers) {
      *r++ = {
          .id = buff.id,
          .status = return_status,
      };
    }
    device_client_.CompleteTx(results.data(), buf_count);
    return;
  }

  for (const tx_buffer_t& buff : buffers) {
    auto back = std::make_unique<TxBuffer>(buff);
    tx_buffers_.push_back(std::move(back));
  }
  EXPECT_OK(event_.signal(0, kEventTx));
}

void FakeNetworkDeviceImpl::NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buf_list,
                                                          size_t buf_count) {
  ASSERT_TRUE(device_client_.is_valid());

  fbl::AutoLock lock(&lock_);
  fbl::Span buffers(buf_list, buf_count);
  if (immediate_return_rx_ || !device_started_) {
    const uint32_t length = device_started_ ? kAutoReturnRxLength : 0;
    ASSERT_TRUE(buf_count < kTxDepth);
    std::array<rx_buffer_t, kTxDepth> results;
    std::array<rx_buffer_part_t, kTxDepth> parts;
    auto r = results.begin();
    auto p = parts.begin();
    for (const rx_space_buffer_t& space : buffers) {
      rx_buffer_part_t& part = *p++;
      rx_buffer_t& rx_buffer = *r++;
      part = {
          .id = space.id,
          .length = length,
      };
      rx_buffer = {
          .meta =
              {
                  .frame_type =
                      static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet),
              },
          .data_list = &part,
          .data_count = 1,
      };
    }
    device_client_.CompleteRx(results.data(), buf_count);
    return;
  }

  for (const rx_space_buffer_t& buff : buffers) {
    auto back = std::make_unique<RxBuffer>(buff);
    rx_buffers_.push_back(std::move(back));
  }
  EXPECT_OK(event_.signal(0, kEventRxAvailable));
}

fit::function<zx::unowned_vmo(uint8_t)> FakeNetworkDeviceImpl::VmoGetter() {
  return [this](uint8_t id) { return zx::unowned_vmo(vmos_[id]); };
}

bool FakeNetworkDeviceImpl::TriggerStart() {
  fbl::AutoLock lock(&lock_);
  auto cb = std::move(pending_start_callback_);
  lock.release();

  if (cb) {
    cb();
    return true;
  }
  return false;
}

bool FakeNetworkDeviceImpl::TriggerStop() {
  fbl::AutoLock lock(&lock_);
  auto cb = std::move(pending_stop_callback_);
  lock.release();

  if (cb) {
    cb();
    return true;
  }
  return false;
}

zx::status<std::unique_ptr<NetworkDeviceInterface>> FakeNetworkDeviceImpl::CreateChild(
    async_dispatcher_t* dispatcher) {
  auto protocol = proto();
  zx::status device = internal::DeviceInterface::Create(
      dispatcher, ddk::NetworkDeviceImplProtocolClient(&protocol), "FakeImpl");
  if (device.is_error()) {
    return device.take_error();
  }

  auto& value = device.value();
  value->evt_session_started = [this](const char* session) {
    event_.signal(0, kEventSessionStarted);
  };
  return zx::ok(std::move(value));
}

zx_status_t TestSession::Open(fidl::WireSyncClient<netdev::Device>& netdevice, const char* name,
                              netdev::wire::SessionFlags flags, uint16_t num_descriptors,
                              uint64_t buffer_size) {
  netdev::wire::FrameType supported_frames[1];
  supported_frames[0] = netdev::wire::FrameType::kEthernet;
  zx_status_t status;
  if ((status = Init(num_descriptors, buffer_size)) != ZX_OK) {
    return status;
  }
  zx::status info_status = GetInfo();
  if (info_status.is_error()) {
    return info_status.status_value();
  }
  netdev::wire::SessionInfo& info = info_status.value();
  info.set_options(alloc_, flags);

  auto session_name = fidl::StringView::FromExternal(name);

  auto res = netdevice.OpenSession(std::move(session_name), std::move(info));
  if (res.status() != ZX_OK) {
    std::cout << "OpenSession FIDL failure: " << res.error() << std::endl;
    return res.status();
  }
  if (res.value().result.is_err()) {
    std::cout << "OpenSession failed: " << zx_status_get_string(res.value().result.err())
              << std::endl;
    return res.value().result.err();
  }

  Setup(std::move(res.value().result.mutable_response().session),
        std::move(res.value().result.mutable_response().fifos));

  return ZX_OK;
}

zx_status_t TestSession::Init(uint16_t descriptor_count, uint64_t buffer_size) {
  zx_status_t status;
  if (descriptors_vmo_.is_valid() || data_vmo_.is_valid() || session_.channel().is_valid()) {
    return ZX_ERR_BAD_STATE;
  }

  if ((status = descriptors_.CreateAndMap(descriptor_count * sizeof(buffer_descriptor_t),
                                          ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                                          &descriptors_vmo_)) != ZX_OK) {
    std::cout << "ERROR: failed to create descriptors map" << std::endl;
    return status;
  }

  if ((status = data_.CreateAndMap(descriptor_count * buffer_size,
                                   ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &data_vmo_)) !=
      ZX_OK) {
    std::cout << "ERROR: failed to create data map" << std::endl;
    return status;
  }
  descriptors_count_ = descriptor_count;
  buffer_length_ = buffer_size;
  return ZX_OK;
}

zx::status<netdev::wire::SessionInfo> TestSession::GetInfo() {
  if (!data_vmo_.is_valid() || !descriptors_vmo_.is_valid()) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  zx::vmo data_vmo;
  if (zx_status_t status = data_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &data_vmo); status != ZX_OK) {
    return zx::error(status);
  }
  zx::vmo descriptors_vmo;
  if (zx_status_t status = descriptors_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &descriptors_vmo);
      status != ZX_OK) {
    return zx::error(status);
  }
  netdev::wire::SessionInfo info(alloc_);
  info.set_data(alloc_, std::move(data_vmo));
  info.set_descriptors(alloc_, std::move(descriptors_vmo));
  info.set_descriptor_version(alloc_, NETWORK_DEVICE_DESCRIPTOR_VERSION);
  info.set_descriptor_length(alloc_, sizeof(buffer_descriptor_t) / sizeof(uint64_t));
  info.set_descriptor_count(alloc_, descriptors_count_);
  return zx::ok(std::move(info));
}

void TestSession::Setup(fidl::ClientEnd<netdev::Session> session, netdev::wire::Fifos fifos) {
  session_ = fidl::WireSyncClient<netdev::Session>(std::move(session));
  fifos_ = std::move(fifos);
}

zx_status_t TestSession::AttachPort(uint8_t port_id,
                                    std::vector<netdev::wire::FrameType> frame_types) {
  fidl::WireResult wire_result = session_.Attach(
      port_id, fidl::VectorView<netdev::wire::FrameType>::FromExternal(frame_types));
  if (!wire_result.ok()) {
    return wire_result.status();
  }
  const auto& result = wire_result.value().result;
  switch (result.which()) {
    case netdev::wire::SessionAttachResult::Tag::kResponse:
      return ZX_OK;
    case netdev::wire::SessionAttachResult::Tag::kErr:
      return result.err();
  }
}

zx_status_t TestSession::AttachPort(FakeNetworkPortImpl& impl) {
  std::vector<netdev::wire::FrameType> rx_types;
  for (uint8_t frame_type :
       fbl::Span(impl.port_info().rx_types_list, impl.port_info().rx_types_count)) {
    rx_types.push_back(static_cast<netdev::wire::FrameType>(frame_type));
  }
  return AttachPort(impl.id(), std::move(rx_types));
}

zx_status_t TestSession::DetachPort(uint8_t port_id) {
  fidl::WireResult wire_result = session_.Detach(port_id);
  if (!wire_result.ok()) {
    return wire_result.status();
  }
  const auto& result = wire_result.value().result;
  switch (result.which()) {
    case netdev::wire::SessionDetachResult::Tag::kResponse:
      return ZX_OK;
    case netdev::wire::SessionDetachResult::Tag::kErr:
      return result.err();
  }
}

zx_status_t TestSession::DetachPort(FakeNetworkPortImpl& impl) { return DetachPort(impl.id()); }

zx_status_t TestSession::Close() { return session_.Close().status(); }

zx_status_t TestSession::WaitClosed(zx::time deadline) {
  return session_.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, deadline, nullptr);
}

buffer_descriptor_t* TestSession::ResetDescriptor(uint16_t index) {
  buffer_descriptor_t* desc = descriptor(index);
  *desc = {
      .frame_type = static_cast<uint8_t>(netdev::wire::FrameType::kEthernet),
      .info_type = static_cast<uint32_t>(netdev::wire::InfoType::kNoInfo),
      .offset = canonical_offset(index),
      .data_length = static_cast<uint32_t>(buffer_length_),
  };
  return desc;
}

void TestSession::ZeroVmo() { memset(data_.start(), 0x00, buffer_length_ * descriptors_count_); }

buffer_descriptor_t* TestSession::descriptor(uint16_t index) {
  if (index < descriptors_count_) {
    return reinterpret_cast<buffer_descriptor_t*>(reinterpret_cast<uint8_t*>(descriptors_.start()) +
                                                  (index * sizeof(buffer_descriptor_t)));
  }
  return nullptr;
}

uint8_t* TestSession::buffer(uint64_t offset) {
  return reinterpret_cast<uint8_t*>(data_.start()) + offset;
}

zx_status_t TestSession::FetchRx(uint16_t* descriptors, size_t count, size_t* actual) const {
  return fifos_.rx.read(sizeof(uint16_t), descriptors, count, actual);
}

zx_status_t TestSession::FetchTx(uint16_t* descriptors, size_t count, size_t* actual) const {
  return fifos_.tx.read(sizeof(uint16_t), descriptors, count, actual);
}

zx_status_t TestSession::SendRx(const uint16_t* descriptor, size_t count, size_t* actual) const {
  return fifos_.rx.write(sizeof(uint16_t), descriptor, count, actual);
}

zx_status_t TestSession::SendTx(const uint16_t* descriptor, size_t count, size_t* actual) const {
  return fifos_.tx.write(sizeof(uint16_t), descriptor, count, actual);
}

zx_status_t TestSession::SendTxData(uint8_t port_id, uint16_t descriptor_index,
                                    const std::vector<uint8_t>& data) {
  auto* desc = ResetDescriptor(descriptor_index);
  zx_status_t status;
  if ((status = data_vmo_.write(&data.at(0), desc->offset, data.size())) != ZX_OK) {
    return status;
  }
  desc->port_id = port_id;
  desc->data_length = static_cast<uint32_t>(data.size());
  return SendTx(descriptor_index);
}

}  // namespace testing
}  // namespace network
