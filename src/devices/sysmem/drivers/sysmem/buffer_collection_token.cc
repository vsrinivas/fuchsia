// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_collection_token.h"

#include <lib/fidl-utils/bind.h>

#include <ddk/trace/event.h>

namespace sysmem_driver {

namespace {

constexpr uint32_t kConcurrencyCap = 64;

}  // namespace

const fuchsia_sysmem_BufferCollectionToken_ops_t BufferCollectionToken::kOps = {
    fidl::Binder<BufferCollectionToken>::BindMember<&BufferCollectionToken::Duplicate>,
    fidl::Binder<BufferCollectionToken>::BindMember<&BufferCollectionToken::Sync>,
    fidl::Binder<BufferCollectionToken>::BindMember<&BufferCollectionToken::Close>,
    fidl::Binder<BufferCollectionToken>::BindMember<&BufferCollectionToken::SetName>,
    fidl::Binder<BufferCollectionToken>::BindMember<&BufferCollectionToken::SetDebugClientInfo>,
    fidl::Binder<BufferCollectionToken>::BindMember<
        &BufferCollectionToken::SetDebugTimeoutLogDeadline>,
};

BufferCollectionToken::~BufferCollectionToken() {
  TRACE_DURATION("gfx", "BufferCollectionToken::~BufferCollectionToken", "this", this, "parent",
                 parent_.get());
  // zx_koid_t values are never re-used during lifetime of running system, so
  // it's fine that the channel is already closed (no possibility of re-use
  // of value in the tracked set of values).

  // It's fine if server_koid() is ZX_KOID_INVALID - no effect in that case.
  parent_device_->UntrackToken(this);
}

zx_status_t BufferCollectionToken::Duplicate(uint32_t rights_attenuation_mask,
                                             zx_handle_t buffer_collection_token_request_param) {
  TRACE_DURATION("gfx", "BufferCollectionToken::Duplicate", "this", this, "parent", parent_.get());
  zx::channel buffer_collection_token_request(buffer_collection_token_request_param);
  LogInfo("BufferCollectionToken::Duplicate()");
  if (is_done_) {
    // Probably a Close() followed by Duplicate(), which is illegal and
    // causes the whole LogicalBufferCollection to fail.
    FailAsync(ZX_ERR_BAD_STATE, "BufferCollectionToken::Duplicate() attempted when is_done_");
    return ZX_OK;
  }
  auto duplicate_rights_attenuation_mask = rights_attenuation_mask_;
  if (rights_attenuation_mask != ZX_RIGHT_SAME_RIGHTS) {
    duplicate_rights_attenuation_mask &= rights_attenuation_mask;
  }
  parent()->CreateBufferCollectionToken(parent_, duplicate_rights_attenuation_mask,
                                        std::move(buffer_collection_token_request), &debug_info_);
  return ZX_OK;
}

zx_status_t BufferCollectionToken::Sync(fidl_txn_t* txn) {
  TRACE_DURATION("gfx", "BufferCollectionToken::Sync", "this", this, "parent", parent_.get());
  BindingType::Txn::RecognizeTxn(txn);
  if (is_done_) {
    // Probably a Close() followed by Sync(), which is illegal and
    // causes the whole LogicalBufferCollection to fail.
    FailAsync(ZX_ERR_BAD_STATE, "BufferCollectionToken::Sync() attempted when is_done_");
    return ZX_OK;
  }
  return fuchsia_sysmem_BufferCollectionTokenSync_reply(txn);
}

// Clean token close without causing LogicalBufferCollection failure.
zx_status_t BufferCollectionToken::Close() {
  if (is_done_ || buffer_collection_request_) {
    FailAsync(ZX_ERR_BAD_STATE,
              "BufferCollectionToken::Close() when already is_done_ || "
              "buffer_collection_request_");
    // We're failing async - no need to try to fail sync.
    return ZX_OK;
  }
  // We don't need to do anything else here because we want to enforce that
  // no other messages are sent between Close() and channel close.  So we
  // check for that as messages potentially arive and handle close via the
  // error handler after the client has closed the channel.
  is_done_ = true;
  return ZX_OK;
}

LogicalBufferCollection* BufferCollectionToken::parent() { return parent_.get(); }

fbl::RefPtr<LogicalBufferCollection> BufferCollectionToken::parent_shared() { return parent_; }

void BufferCollectionToken::SetServerKoid(zx_koid_t server_koid) {
  ZX_DEBUG_ASSERT(server_koid_ == ZX_KOID_INVALID);
  ZX_DEBUG_ASSERT(server_koid != ZX_KOID_INVALID);
  server_koid_ = server_koid;
  parent_device_->TrackToken(this);
  if (parent_device_->TryRemoveKoidFromUnfoundTokenList(server_koid_)) {
    was_unfound_token_ = true;
    // LogicalBufferCollection will print an error, since it might have useful client information
  }
}

zx_koid_t BufferCollectionToken::server_koid() { return server_koid_; }

bool BufferCollectionToken::is_done() { return is_done_; }

void BufferCollectionToken::SetBufferCollectionRequest(zx::channel buffer_collection_request) {
  if (is_done_ || buffer_collection_request_) {
    FailAsync(ZX_ERR_BAD_STATE,
              "BufferCollectionToken::SetBufferCollectionRequest() attempted "
              "when already is_done_ || buffer_collection_request_");
    return;
  }
  ZX_DEBUG_ASSERT(!buffer_collection_request_);
  buffer_collection_request_ = std::move(buffer_collection_request);
}

zx::channel BufferCollectionToken::TakeBufferCollectionRequest() {
  return std::move(buffer_collection_request_);
}

zx_status_t BufferCollectionToken::SetName(uint32_t priority, const char* name_data,
                                           size_t name_size) {
  parent_->SetName(priority, std::string(name_data, name_size));
  return ZX_OK;
}

zx_status_t BufferCollectionToken::SetDebugClientInfo(const char* name_data, size_t name_size,
                                                      uint64_t id) {
  debug_info_.name = std::string(name_data, name_size);
  debug_info_.id = id;
  debug_id_property_ = node_.CreateUint("debug_id", debug_info_.id);
  debug_name_property_ = node_.CreateString("debug_name", debug_info_.name);
  if (was_unfound_token_) {
    // Output the debug info now that we have it, since we previously said bad things about this
    // token's server_koid not being found when it should have been, but at that time we didn't have
    // the debug info.
    parent_->LogClientError(&debug_info_, "Got debug info for token %ld", server_koid_);
  }
  return ZX_OK;
}

zx_status_t BufferCollectionToken::SetDebugTimeoutLogDeadline(int64_t deadline) {
  parent_->SetDebugTimeoutLogDeadline(deadline);
  return ZX_OK;
}

BufferCollectionToken::BufferCollectionToken(Device* parent_device,
                                             fbl::RefPtr<LogicalBufferCollection> parent,
                                             uint32_t rights_attenuation_mask)
    : FidlServer(parent_device->dispatcher(), "BufferCollectionToken", kConcurrencyCap),
      parent_device_(parent_device),
      parent_(parent),
      rights_attenuation_mask_(rights_attenuation_mask) {
  TRACE_DURATION("gfx", "BufferCollectionToken::BufferCollectionToken", "this", this, "parent",
                 parent_.get());
  ZX_DEBUG_ASSERT(parent_device_);
  ZX_DEBUG_ASSERT(parent_);
  node_ = parent_->node().CreateChild(CreateUniqueName("token-"));
}

}  // namespace sysmem_driver
