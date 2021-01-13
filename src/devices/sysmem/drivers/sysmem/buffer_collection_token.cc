// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_collection_token.h"

#include <lib/fidl-utils/bind.h>
#include <lib/fidl/llcpp/server.h>

#include <ddk/trace/event.h>

namespace sysmem_driver {

BufferCollectionToken::~BufferCollectionToken() {
  TRACE_DURATION("gfx", "BufferCollectionToken::~BufferCollectionToken", "this", this, "parent",
                 parent_.get());
  // zx_koid_t values are never re-used during lifetime of running system, so
  // it's fine that the channel is already closed (no possibility of re-use
  // of value in the tracked set of values).

  // It's fine if server_koid() is ZX_KOID_INVALID - no effect in that case.
  parent_device_->UntrackToken(this);
}

void BufferCollectionToken::CloseChannel() {
  error_handler_ = {};
  if (server_binding_)
    server_binding_->Close(ZX_OK);
  server_binding_ = {};
}

// static

BindingHandle<BufferCollectionToken> BufferCollectionToken::Create(
    Device* parent_device, fbl::RefPtr<LogicalBufferCollection> parent,
    uint32_t rights_attenuation_mask) {
  return BindingHandle<BufferCollectionToken>(fbl::AdoptRef<BufferCollectionToken>(
      new BufferCollectionToken(parent_device, std::move(parent), rights_attenuation_mask)));
}

void BufferCollectionToken::Bind(zx::channel channel) {
  auto res = fidl::BindServer(
      parent_device_->dispatcher(), std::move(channel), this,
      fidl::OnUnboundFn<BufferCollectionToken>(
          [this, this_ref = fbl::RefPtr<BufferCollectionToken>(this)](
              BufferCollectionToken* token, fidl::UnbindInfo info, zx::channel channel) {
            // We need to keep a refptr to this class, since the unbind happens asynchronously and
            // can run after the parent closes a handle to this class.
            if (error_handler_)
              error_handler_(info.status);
            // *this can be destroyed at this point.
          }));
  if (res.is_error()) {
    return;
  }
  server_binding_ = res.take_value();
  return;
}

void BufferCollectionToken::Duplicate(uint32_t rights_attenuation_mask,
                                      zx::channel buffer_collection_token_request,
                                      DuplicateCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "BufferCollectionToken::Duplicate", "this", this, "parent", parent_.get());
  LogInfo(FROM_HERE, "BufferCollectionToken::Duplicate()");
  if (is_done_) {
    // Probably a Close() followed by Duplicate(), which is illegal and
    // causes the whole LogicalBufferCollection to fail.
    FailAsync(FROM_HERE, ZX_ERR_BAD_STATE,
              "BufferCollectionToken::Duplicate() attempted when is_done_");
    return;
  }
  auto duplicate_rights_attenuation_mask = rights_attenuation_mask_;
  if (rights_attenuation_mask != ZX_RIGHT_SAME_RIGHTS) {
    duplicate_rights_attenuation_mask &= rights_attenuation_mask;
  }
  parent()->CreateBufferCollectionToken(parent_, duplicate_rights_attenuation_mask,
                                        std::move(buffer_collection_token_request), &debug_info_);
  return;
}

void BufferCollectionToken::Sync(SyncCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "BufferCollectionToken::Sync", "this", this, "parent", parent_.get());
  if (is_done_) {
    // Probably a Close() followed by Sync(), which is illegal and
    // causes the whole LogicalBufferCollection to fail.
    FailAsync(FROM_HERE, ZX_ERR_BAD_STATE, "BufferCollectionToken::Sync() attempted when is_done_");
    return;
  }
  completer.Reply();
}

// Clean token close without causing LogicalBufferCollection failure.
void BufferCollectionToken::Close(CloseCompleter::Sync&) {
  if (is_done_ || buffer_collection_request_) {
    FailAsync(FROM_HERE, ZX_ERR_BAD_STATE,
              "BufferCollectionToken::Close() when already is_done_ || "
              "buffer_collection_request_");
    // We're failing async - no need to try to fail sync.
    return;
  }
  // We don't need to do anything else here because we want to enforce that
  // no other messages are sent between Close() and channel close.  So we
  // check for that as messages potentially arive and handle close via the
  // error handler after the client has closed the channel.
  is_done_ = true;
  return;
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
    FailAsync(FROM_HERE, ZX_ERR_BAD_STATE,
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

void BufferCollectionToken::SetName(uint32_t priority, fidl::StringView name,
                                    SetNameCompleter::Sync&) {
  parent_->SetName(priority, std::string(name.begin(), name.end()));
}

void BufferCollectionToken::SetDebugClientInfo(fidl::StringView name, uint64_t id,
                                               SetDebugClientInfoCompleter::Sync&) {
  SetDebugClientInfo(std::string(name.begin(), name.end()), id);
}

void BufferCollectionToken::SetDebugClientInfo(std::string name, uint64_t id) {
  debug_info_.name = name;
  debug_info_.id = id;
  debug_id_property_ = node_.CreateUint("debug_id", debug_info_.id);
  debug_name_property_ = node_.CreateString("debug_name", debug_info_.name);
  if (was_unfound_token_) {
    // Output the debug info now that we have it, since we previously said bad things about this
    // token's server_koid not being found when it should have been, but at that time we didn't have
    // the debug info.
    parent_->LogClientError(FROM_HERE, &debug_info_, "Got debug info for token %ld", server_koid_);
  }
}

void BufferCollectionToken::SetDebugTimeoutLogDeadline(int64_t deadline,
                                                       SetDebugTimeoutLogDeadlineCompleter::Sync&) {
  parent_->SetDebugTimeoutLogDeadline(deadline);
}

BufferCollectionToken::BufferCollectionToken(Device* parent_device,
                                             fbl::RefPtr<LogicalBufferCollection> parent,
                                             uint32_t rights_attenuation_mask)
    : LoggingMixin("BufferCollectionToken"),
      parent_device_(parent_device),
      parent_(parent),
      rights_attenuation_mask_(rights_attenuation_mask) {
  TRACE_DURATION("gfx", "BufferCollectionToken::BufferCollectionToken", "this", this, "parent",
                 parent_.get());
  ZX_DEBUG_ASSERT(parent_device_);
  ZX_DEBUG_ASSERT(parent_);
  node_ = parent_->node().CreateChild(CreateUniqueName("token-"));
}

void BufferCollectionToken::FailAsync(Location location, zx_status_t status, const char* format,
                                      ...) {
  // Idempotent, so only close once.
  if (!server_binding_)
    return;
  va_list args;
  va_start(args, format);
  vLog(true, location.file(), location.line(), logging_prefix(), "fail", format, args);
  va_end(args);

  server_binding_->Close(status);
  server_binding_ = {};
}

}  // namespace sysmem_driver
