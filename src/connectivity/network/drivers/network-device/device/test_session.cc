// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_session.h"

namespace network {
namespace testing {

zx_status_t TestSession::Open(fidl::WireSyncClient<netdev::Device>& netdevice, const char* name,
                              netdev::wire::SessionFlags flags, uint16_t num_descriptors,
                              uint64_t buffer_size) {
  zx_status_t status;
  if ((status = Init(num_descriptors, buffer_size)) != ZX_OK) {
    return status;
  }
  zx::result info_status = GetInfo();
  if (info_status.is_error()) {
    return info_status.status_value();
  }
  netdev::wire::SessionInfo& info = info_status.value();
  info.set_options(flags);

  auto session_name = fidl::StringView::FromExternal(name);

  auto res = netdevice->OpenSession(std::move(session_name), std::move(info));
  if (res.status() != ZX_OK) {
    return res.status();
  }
  if (res->is_error()) {
    return res->error_value();
  }

  Setup(std::move(res->value()->session), std::move(res->value()->fifos));

  return ZX_OK;
}

zx_status_t TestSession::Init(uint16_t descriptor_count, uint64_t buffer_size) {
  zx_status_t status;
  if (descriptors_vmo_.is_valid() || data_vmo_.is_valid() || session_.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }

  if ((status = descriptors_.CreateAndMap(descriptor_count * sizeof(buffer_descriptor_t),
                                          ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                                          &descriptors_vmo_)) != ZX_OK) {
    return status;
  }

  if ((status = data_.CreateAndMap(descriptor_count * buffer_size,
                                   ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &data_vmo_)) !=
      ZX_OK) {
    return status;
  }
  descriptors_count_ = descriptor_count;
  buffer_length_ = buffer_size;
  return ZX_OK;
}

zx::result<netdev::wire::SessionInfo> TestSession::GetInfo() {
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
  info.set_data(std::move(data_vmo));
  info.set_descriptors(std::move(descriptors_vmo));
  info.set_descriptor_version(NETWORK_DEVICE_DESCRIPTOR_VERSION);
  info.set_descriptor_length(static_cast<uint8_t>(sizeof(buffer_descriptor_t) / sizeof(uint64_t)));
  info.set_descriptor_count(descriptors_count_);
  return zx::ok(std::move(info));
}

void TestSession::Setup(fidl::ClientEnd<netdev::Session> session, netdev::wire::Fifos fifos) {
  session_ = fidl::WireSyncClient<netdev::Session>(std::move(session));
  fifos_ = std::move(fifos);
}

zx_status_t TestSession::AttachPort(netdev::wire::PortId port_id,
                                    std::vector<netdev::wire::FrameType> frame_types) {
  fidl::WireResult wire_result = session_->Attach(
      port_id, fidl::VectorView<netdev::wire::FrameType>::FromExternal(frame_types));
  if (!wire_result.ok()) {
    return wire_result.status();
  }

  const auto* res = wire_result.Unwrap();
  if (res->is_error()) {
    return res->error_value();
  }
  return ZX_OK;
}

zx_status_t TestSession::DetachPort(netdev::wire::PortId port_id) {
  fidl::WireResult wire_result = session_->Detach(port_id);
  if (!wire_result.ok()) {
    return wire_result.status();
  }

  const auto* res = wire_result.Unwrap();
  if (res->is_error()) {
    return res->error_value();
  }
  return ZX_OK;
}

zx_status_t TestSession::Close() { return session_->Close().status(); }

zx_status_t TestSession::WaitClosed(zx::time deadline) {
  return session_.client_end().channel().wait_one(ZX_CHANNEL_PEER_CLOSED, deadline, nullptr);
}

buffer_descriptor_t& TestSession::ResetDescriptor(uint16_t index) {
  buffer_descriptor_t& desc = descriptor(index);
  desc = {
      .frame_type = static_cast<uint8_t>(netdev::wire::FrameType::kEthernet),
      .info_type = static_cast<uint32_t>(netdev::wire::InfoType::kNoInfo),
      .offset = canonical_offset(index),
      .data_length = static_cast<uint32_t>(buffer_length_),
  };
  return desc;
}

void TestSession::ZeroVmo() { memset(data_.start(), 0x00, buffer_length_ * descriptors_count_); }

buffer_descriptor_t& TestSession::descriptor(uint16_t index) {
  ZX_ASSERT_MSG(index < descriptors_count_, "descriptor %d out of bounds (count = %d)", index,
                descriptors_count_);
  return *(reinterpret_cast<buffer_descriptor_t*>(descriptors_.start()) + index);
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

zx_status_t TestSession::SendTxData(const netdev::wire::PortId& port_id, uint16_t descriptor_index,
                                    const std::vector<uint8_t>& data) {
  buffer_descriptor_t& desc = ResetDescriptor(descriptor_index);
  zx_status_t status;
  if ((status = data_vmo_.write(&data.at(0), desc.offset, data.size())) != ZX_OK) {
    return status;
  }
  desc.port_id = {
      .base = port_id.base,
      .salt = port_id.salt,
  };
  desc.data_length = static_cast<uint32_t>(data.size());
  return SendTx(descriptor_index);
}

}  // namespace testing
}  // namespace network
