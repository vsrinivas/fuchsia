// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_collection_token.h"

#include <lib/ddk/trace/event.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fidl/llcpp/server.h>
#include <zircon/errors.h>

#include "node_properties.h"
#include "src/devices/sysmem/drivers/sysmem/device.h"
#include "src/devices/sysmem/drivers/sysmem/node.h"

namespace sysmem_driver {

BufferCollectionToken::~BufferCollectionToken() {
  TRACE_DURATION("gfx", "BufferCollectionToken::~BufferCollectionToken", "this", this,
                 "logical_buffer_collection", &logical_buffer_collection());

  // zx_koid_t values are never re-used during lifetime of running system, so
  // it's fine that the channel is already closed (no possibility of re-use
  // of value in the tracked set of values).

  // It's fine if server_koid() is ZX_KOID_INVALID - no effect in that case.
  parent_device_->UntrackToken(this);
}

void BufferCollectionToken::CloseChannel(zx_status_t epitaph) {
  // This essentially converts the OnUnboundFn semantic of getting called regardless of channel-fail
  // vs. server-driven-fail into the more typical semantic where error_handler_ only gets called
  // on channel-fail but not on server-driven-fail.
  error_handler_ = {};
  if (server_binding_)
    server_binding_->Close(epitaph);
  server_binding_ = {};
  parent_device_->UntrackToken(this);
}

// static
BufferCollectionToken& BufferCollectionToken::EmplaceInTree(
    Device* parent_device, fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
    NodeProperties* new_node_properties) {
  auto token = fbl::AdoptRef(new BufferCollectionToken(
      parent_device, std::move(logical_buffer_collection), new_node_properties));
  auto token_ptr = token.get();
  new_node_properties->SetNode(token);
  return *token_ptr;
}

void BufferCollectionToken::Bind(
    fidl::ServerEnd<fuchsia_sysmem::BufferCollectionToken> token_request) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      token_request.channel().get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status == ZX_OK) {
    inspect_node_.CreateUint("channel_koid", info.koid, &properties_);
  }
  server_binding_ =
      fidl::BindServer(parent_device_->dispatcher(), std::move(token_request), this,
                       [this, this_ref = fbl::RefPtr<BufferCollectionToken>(this)](
                           BufferCollectionToken* token, fidl::UnbindInfo info,
                           fidl::ServerEnd<fuchsia_sysmem::BufferCollectionToken> channel) {
                         // We need to keep a refptr to this class, since the unbind happens
                         // asynchronously and can run after the parent closes a handle to this
                         // class.
                         if (error_handler_) {
                           zx_status_t status = info.status();
                           if (async_failure_result_ && info.reason() == fidl::Reason::kClose) {
                             // On kClose the error is always ZX_OK, so report the real error to
                             // LogicalBufferCollection if the close was caused by FailAsync or
                             // FailSync.
                             status = *async_failure_result_;
                           }
                           error_handler_(status);
                         }
                         // *this can be destroyed by ~this_ref here
                       });
}

void BufferCollectionToken::Duplicate(
    uint32_t rights_attenuation_mask,
    fidl::ServerEnd<fuchsia_sysmem::BufferCollectionToken> token_request,
    DuplicateCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "BufferCollectionToken::Duplicate", "this", this,
                 "logical_buffer_collection", &logical_buffer_collection());
  table_set_.MitigateChurn();
  if (is_done_) {
    // Probably a Close() followed by Duplicate(), which is illegal and
    // causes the whole LogicalBufferCollection to fail.
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::Duplicate() attempted when is_done_");
    return;
  }
  NodeProperties* new_node_properties = node_properties().NewChild(&logical_buffer_collection());
  if (rights_attenuation_mask == 0) {
    logical_buffer_collection().LogClientError(
        FROM_HERE, &node_properties(),
        "rights_attenuation_mask of 0 is DEPRECATED - use ZX_RIGHT_SAME_RIGHTS instead.");
    rights_attenuation_mask = ZX_RIGHT_SAME_RIGHTS;
  }
  if (rights_attenuation_mask != ZX_RIGHT_SAME_RIGHTS) {
    new_node_properties->rights_attenuation_mask() &= rights_attenuation_mask;
  }
  logical_buffer_collection().CreateBufferCollectionToken(
      shared_logical_buffer_collection(), new_node_properties, std::move(token_request));
}

void BufferCollectionToken::Sync(SyncCompleter::Sync& completer) {
  table_set_.MitigateChurn();
  TRACE_DURATION("gfx", "BufferCollectionToken::Sync", "this", this, "logical_buffer_collection",
                 &logical_buffer_collection());
  if (is_done_) {
    // Probably a Close() followed by Sync(), which is illegal and
    // causes the whole LogicalBufferCollection to fail.
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::Sync() attempted when is_done_");
    return;
  }
  completer.Reply();
}

// Clean token close without causing LogicalBufferCollection failure.
void BufferCollectionToken::Close(CloseCompleter::Sync& completer) {
  table_set_.MitigateChurn();
  if (is_done_ || buffer_collection_request_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
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
}

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
  table_set_.MitigateChurn();
  logical_buffer_collection().SetName(priority, std::string(name.begin(), name.end()));
}

void BufferCollectionToken::SetDebugClientInfo(fidl::StringView name, uint64_t id,
                                               SetDebugClientInfoCompleter::Sync&) {
  table_set_.MitigateChurn();
  SetDebugClientInfoInternal(std::string(name.begin(), name.end()), id);
}

void BufferCollectionToken::SetDebugClientInfoInternal(std::string name, uint64_t id) {
  node_properties().client_debug_info().name = std::move(name);
  node_properties().client_debug_info().id = id;
  debug_id_property_ =
      inspect_node_.CreateUint("debug_id", node_properties().client_debug_info().id);
  debug_name_property_ =
      inspect_node_.CreateString("debug_name", node_properties().client_debug_info().name);
  if (was_unfound_token_) {
    // Output the debug info now that we have it, since we previously said bad things about this
    // token's server_koid not being found when it should have been, but at that time we didn't have
    // the debug info.
    //
    // This is not a failure here, but the message provides debug info for a failure that previously
    // occurred.
    logical_buffer_collection().LogClientError(FROM_HERE, &node_properties(),
                                               "Got debug info for token %ld", server_koid_);
  }
}

void BufferCollectionToken::SetDebugTimeoutLogDeadline(int64_t deadline,
                                                       SetDebugTimeoutLogDeadlineCompleter::Sync&) {
  table_set_.MitigateChurn();
  logical_buffer_collection().SetDebugTimeoutLogDeadline(deadline);
}

void BufferCollectionToken::SetDispensable(SetDispensableCompleter::Sync& completer) {
  SetDispensableInternal();
}

void BufferCollectionToken::SetDispensableInternal() {
  if (node_properties().error_propagation_mode() <
      ErrorPropagationMode::kPropagateBeforeAllocation) {
    node_properties().error_propagation_mode() = ErrorPropagationMode::kPropagateBeforeAllocation;
  }
}

BufferCollectionToken::BufferCollectionToken(
    Device* parent_device, fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection_param,
    NodeProperties* new_node_properties)
    : Node(std::move(logical_buffer_collection_param), new_node_properties),
      LoggingMixin("BufferCollectionToken"),
      parent_device_(parent_device),
      table_set_(logical_buffer_collection().table_set()) {
  TRACE_DURATION("gfx", "BufferCollectionToken::BufferCollectionToken", "this", this,
                 "logical_buffer_collection", &logical_buffer_collection());
  ZX_DEBUG_ASSERT(parent_device_);
  inspect_node_ =
      logical_buffer_collection().inspect_node().CreateChild(CreateUniqueName("token-"));
}

void BufferCollectionToken::FailAsync(Location location, zx_status_t status, const char* format,
                                      ...) {
  va_list args;
  va_start(args, format);
  vLog(true, location.file(), location.line(), logging_prefix(), "fail", format, args);
  va_end(args);

  // Idempotent, so only close once.
  if (!server_binding_)
    return;

  async_failure_result_ = status;
  server_binding_->Close(status);
  server_binding_ = {};
}

template <typename Completer>
void BufferCollectionToken::FailSync(Location location, Completer& completer, zx_status_t status,
                                     const char* format, ...) {
  va_list args;
  va_start(args, format);
  logical_buffer_collection().VLogClientError(location, &node_properties(), format, args);
  va_end(args);

  completer.Close(status);
  async_failure_result_ = status;
}

bool BufferCollectionToken::ReadyForAllocation() { return false; }

void BufferCollectionToken::OnBuffersAllocated(const AllocationResult& allocation_result) {
  ZX_PANIC("Unexpected call to BufferCollectionToken::OnBuffersAllocated()");
}

void BufferCollectionToken::Fail(zx_status_t epitaph) { CloseChannel(epitaph); }

BufferCollectionToken* BufferCollectionToken::buffer_collection_token() { return this; }

const BufferCollectionToken* BufferCollectionToken::buffer_collection_token() const { return this; }

BufferCollection* BufferCollectionToken::buffer_collection() { return nullptr; }

const BufferCollection* BufferCollectionToken::buffer_collection() const { return nullptr; }

OrphanedNode* BufferCollectionToken::orphaned_node() { return nullptr; }

const OrphanedNode* BufferCollectionToken::orphaned_node() const { return nullptr; }

bool BufferCollectionToken::is_connected() const { return true; }

}  // namespace sysmem_driver
