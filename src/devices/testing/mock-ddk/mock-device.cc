// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/testing/mock-ddk/mock-device.h"

#include <zircon/syscalls/log.h>

MockDevice::MockDevice(device_add_args_t* args, MockDevice* parent)
    : parent_(parent), ops_(args->ops), ctx_(args->ctx), name_(args->name) {
  if (args->proto_id && args->proto_ops) {
    AddProtocol(args->proto_id, args->proto_ops, ctx_);
  }
}

// static
std::shared_ptr<MockDevice> MockDevice::FakeRootParent() {
  // Using `new` to access a non-public constructor.
  return std::shared_ptr<MockDevice>(new MockDevice());
}

// Static member function.
zx_status_t MockDevice::Create(device_add_args_t* args, MockDevice* parent, MockDevice** out_dev) {
  // We only check the minimum requirements to make sure the mock does not crash:
  if (!parent || !args || !args->name) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Using `new` to access a non-public constructor.
  auto new_device = std::shared_ptr<MockDevice>(new MockDevice(args, parent));
  *out_dev = new_device.get();
  parent->children_.emplace_back(std::move(new_device));
  // PropagateMetadata to last child:
  for (const auto& [key, value] : parent->metadata_) {
    parent->children().back()->metadata_[key] = value;
  }
  parent->children().back()->PropagateMetadata();
  return ZX_OK;
}

MockDevice* MockDevice::GetLatestChild() {
  if (child_count()) {
    return children_.back().get();
  }
  return nullptr;
}

// Templates that dispatch the protocol operations if they were set.
// If they were not set, the second parameter is returned to the caller
// (usually ZX_ERR_NOT_SUPPORTED)
template <typename RetType, typename... ArgTypes>
RetType Dispatch(void* ctx, RetType (*op)(void* ctx, ArgTypes...), RetType fallback,
                 ArgTypes... args) {
  return op ? (*op)(ctx, args...) : fallback;
}

template <typename... ArgTypes>
void Dispatch(void* ctx, void (*op)(void* ctx, ArgTypes...), ArgTypes... args) {
  if (op) {
    (*op)(ctx, args...);
  }
}

void MockDevice::InitOp() { Dispatch(ctx_, ops_->init); }

zx_status_t MockDevice::OpenOp(zx_device_t** dev_out, uint32_t flags) {
  return Dispatch(ctx_, ops_->open, ZX_OK, dev_out, flags);
}

zx_status_t MockDevice::CloseOp(uint32_t flags) {
  return Dispatch(ctx_, ops_->close, ZX_OK, flags);
}

void MockDevice::UnbindOp() { Dispatch(ctx_, ops_->unbind); }

void MockDevice::ReleaseOp() {
  Dispatch(ctx_, ops_->release);
  // Make parent release child now
  for (auto it = parent_->children_.begin(); it != parent_->children_.end(); ++it) {
    if (it->get() == this) {
      parent_->children_.erase(it);
      return;
    }
  }
  // Print error: we did not find ourselves!
}

MockDevice::~MockDevice() {
  while (!children_.empty()) {
    // This should remove the first child device from children_:
    children_.front()->ReleaseOp();
  }
}

void MockDevice::SuspendNewOp(uint8_t requested_state, bool enable_wake, uint8_t suspend_reason) {
  Dispatch(ctx_, ops_->suspend, requested_state, enable_wake, suspend_reason);
}

zx_status_t MockDevice::SetPerformanceStateOp(uint32_t requested_state, uint32_t* out_state) {
  return Dispatch(ctx_, ops_->set_performance_state, ZX_ERR_NOT_SUPPORTED, requested_state,
                  out_state);
}

zx_status_t MockDevice::ConfigureAutoSuspendOp(bool enable, uint8_t requested_state) {
  return Dispatch(ctx_, ops_->configure_auto_suspend, ZX_ERR_NOT_SUPPORTED, enable,
                  requested_state);
}

void MockDevice::ResumeNewOp(uint32_t requested_state) {
  Dispatch(ctx_, ops_->resume, requested_state);
}

zx_status_t MockDevice::ReadOp(void* buf, size_t count, zx_off_t off, size_t* actual) {
  return Dispatch(ctx_, ops_->read, ZX_ERR_NOT_SUPPORTED, buf, count, off, actual);
}

zx_status_t MockDevice::WriteOp(const void* buf, size_t count, zx_off_t off, size_t* actual) {
  return Dispatch(ctx_, ops_->write, ZX_ERR_NOT_SUPPORTED, buf, count, off, actual);
}

zx_off_t MockDevice::GetSizeOp() { return Dispatch(ctx_, ops_->get_size, 0lu); }

zx_status_t MockDevice::MessageOp(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return Dispatch(ctx_, ops_->message, ZX_ERR_NOT_SUPPORTED, msg, txn);
}

void MockDevice::ChildPreReleaseOp(void* child_ctx) {
  Dispatch(ctx_, ops_->child_pre_release, child_ctx);
}

void MockDevice::SetMetadata(uint32_t type, const void* data, size_t data_length) {
  metadata_[type] = std::make_pair(data, data_length);
  PropagateMetadata();
}

zx_status_t MockDevice::GetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual) {
  auto itr = metadata_.find(type);
  if (itr != metadata_.end()) {
    auto [metadata, size] = itr->second;
    *actual = size;
    if (buflen < size) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(buf, metadata, size);
    return ZX_OK;
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t MockDevice::GetMetadataSize(uint32_t type, size_t* out_size) {
  auto itr = metadata_.find(type);
  if (itr != metadata_.end()) {
    auto [_, size] = itr->second;
    *out_size = size;
    return ZX_OK;
  }
  return ZX_ERR_BAD_STATE;
}

void MockDevice::PropagateMetadata() {
  for (auto& child : children_) {
    for (const auto& [key, value] : metadata_) {
      child->metadata_[key] = value;
    }
    child->PropagateMetadata();
  }
}

void MockDevice::AddProtocol(uint32_t id, const void* ops, void* ctx) {
  protocols_.push_back(mock_ddk::ProtocolEntry{id, {ops, ctx}});
}

void MockDevice::AddParentProtocol(uint32_t id, const void* ops, void* ctx) {
  if (!IsRootParent()) {
    parent_->AddProtocol(id, ops, ctx);
  }
}

void MockDevice::SetFirmware(std::vector<uint8_t> firmware, std::string_view path) {
  firmware_[path] = std::move(firmware);
}

void MockDevice::SetFirmware(std::string firmware, std::string_view path) {
  std::vector<uint8_t> vec(firmware.begin(), firmware.end());
  SetFirmware(vec, path);
}

zx_status_t MockDevice::LoadFirmware(std::string_view path, zx_handle_t* fw, size_t* size) {
  auto firmware = firmware_.find(path);
  // If a match is not found to 'path', check if there is a firmware that was loaded with
  // path == nullptr:
  if (firmware == firmware_.end()) {
    firmware = firmware_.find("");
  }
  if (firmware == firmware_.end()) {
    return ZX_ERR_NOT_FOUND;
  }
  zx_status_t status = ZX_OK;
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  if ((status = zx_vmo_create(firmware->second.size(), 0, &vmo)) != ZX_OK) {
    return status;
  }
  if ((status = zx_vmo_write(vmo, firmware->second.data(), 0, firmware->second.size())) != ZX_OK) {
    return status;
  }

  *fw = vmo;
  *size = firmware->second.size();
  return ZX_OK;
}

zx_status_t MockDevice::GetProtocol(uint32_t proto_id, void* protocol) const {
  auto out = reinterpret_cast<mock_ddk::Protocol*>(protocol);
  // First we check if the user has added protocols:
  for (const auto& proto : protocols_) {
    if (proto_id == proto.id) {
      out->ops = proto.proto.ops;
      out->ctx = proto.proto.ctx;
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_SUPPORTED;
}

size_t MockDevice::descendant_count() const {
  size_t count = child_count();
  for (auto& child : children_) {
    count += child->descendant_count();
  }
  return count;
}

// helper functions:
namespace {

zx_status_t ProcessDeviceRemoval(MockDevice* device) {
  device->UnbindOp();
  // deleting children, so use a while loop:
  while (!device->children().empty()) {
    auto status = ProcessDeviceRemoval(device->children().back().get());
    if (status != ZX_OK) {
      return status;
    }
  }
  if (device->HasUnbindOp()) {
    zx_status_t status = device->WaitUntilUnbindReplyCalled();
    if (status != ZX_OK) {
      return status;
    }
  }
  device->ReleaseOp();
  return ZX_OK;
}
}  // anonymous namespace

zx_status_t mock_ddk::ReleaseFlaggedDevices(MockDevice* device) {
  if (device->AsyncRemoveCalled()) {
    return ProcessDeviceRemoval(device);
  }
  // Make a vector of the child device pointers, because we might delete the child:
  std::vector<MockDevice*> children;
  std::transform(device->children().begin(), device->children().end(), std::back_inserter(children),
                 [](std::shared_ptr<MockDevice> c) -> MockDevice* { return c.get(); });
  for (auto child : children) {
    auto ret = ReleaseFlaggedDevices(child);
    if (ret != ZX_OK) {
      return ret;
    }
  }
  return ZX_OK;
}
