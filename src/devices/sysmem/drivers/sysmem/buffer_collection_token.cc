// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_collection_token.h"

#include <lib/ddk/trace/event.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/zx/channel.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "device.h"
#include "koid_util.h"
#include "node.h"
#include "node_properties.h"

namespace sysmem_driver {

BufferCollectionToken::~BufferCollectionToken() {
  TRACE_DURATION("gfx", "BufferCollectionToken::~BufferCollectionToken", "this", this,
                 "logical_buffer_collection", &logical_buffer_collection());

  // zx_koid_t values are never re-used during lifetime of running system, so
  // it's fine that the channel is already closed (no possibility of re-use
  // of value in the tracked set of values).

  // It's fine if server_koid() is ZX_KOID_INVALID - no effect in that case.
  parent_device()->UntrackToken(this);
}

void BufferCollectionToken::CloseServerBinding(zx_status_t epitaph) {
  if (server_binding_.has_value()) {
    server_binding_->Close(epitaph);
  }
  server_binding_ = {};
  parent_device()->UntrackToken(this);
}

// static
BufferCollectionToken& BufferCollectionToken::EmplaceInTree(
    fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
    NodeProperties* new_node_properties, zx::unowned_channel server_end) {
  auto token = fbl::AdoptRef(new BufferCollectionToken(std::move(logical_buffer_collection),
                                                       new_node_properties, std::move(server_end)));
  auto token_ptr = token.get();
  new_node_properties->SetNode(token);
  return *token_ptr;
}

void BufferCollectionToken::BindInternal(zx::channel token_request,
                                         ErrorHandlerWrapper error_handler_wrapper) {
  server_binding_ =
      fidl::BindServer(parent_device()->dispatcher(), std::move(token_request), this,
                       [error_handler_wrapper = std::move(error_handler_wrapper)](
                           BufferCollectionToken* token, fidl::UnbindInfo info,
                           fidl::ServerEnd<fuchsia_sysmem::BufferCollectionToken> channel) {
                         error_handler_wrapper(info);
                       });
}

void BufferCollectionToken::DuplicateSync(DuplicateSyncRequestView request,
                                          DuplicateSyncCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "BufferCollectionToken::DuplicateSync", "this", this,
                 "logical_buffer_collection", &logical_buffer_collection());
  if (is_done_) {
    // Probably a Close() followed by DuplicateSync(), which is illegal and
    // causes the whole LogicalBufferCollection to fail.
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::DuplicateSync() attempted when is_done_");
    return;
  }
  std::vector<fidl::ClientEnd<fuchsia_sysmem::BufferCollectionToken>> new_tokens;

  for (auto& rights_attenuation_mask : request->rights_attenuation_masks) {
    auto token_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
    if (!token_endpoints.is_ok()) {
      FailSync(FROM_HERE, completer, token_endpoints.status_value(),
               "BufferCollectionToken::DuplicateSync() failed to create token channel.");
      return;
    }

    NodeProperties* new_node_properties = node_properties().NewChild(&logical_buffer_collection());
    if (rights_attenuation_mask != ZX_RIGHT_SAME_RIGHTS) {
      new_node_properties->rights_attenuation_mask() &=
          safe_cast<uint32_t>(rights_attenuation_mask);
    }
    logical_buffer_collection().CreateBufferCollectionToken(shared_logical_buffer_collection(),
                                                            new_node_properties,
                                                            std::move(token_endpoints->server));
    new_tokens.push_back(std::move(token_endpoints->client));
  }

  completer.Reply(
      fidl::VectorView<fidl::ClientEnd<fuchsia_sysmem::BufferCollectionToken>>::FromExternal(
          new_tokens));
}

void BufferCollectionToken::Duplicate(DuplicateRequestView request,
                                      DuplicateCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "BufferCollectionToken::Duplicate", "this", this,
                 "logical_buffer_collection", &logical_buffer_collection());
  if (is_done_) {
    // Probably a Close() followed by Duplicate(), which is illegal and
    // causes the whole LogicalBufferCollection to fail.
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::Duplicate() attempted when is_done_");
    return;
  }
  NodeProperties* new_node_properties = node_properties().NewChild(&logical_buffer_collection());
  if (request->rights_attenuation_mask == 0) {
    logical_buffer_collection().LogClientError(
        FROM_HERE, &node_properties(),
        "rights_attenuation_mask of 0 is DEPRECATED - use ZX_RIGHT_SAME_RIGHTS instead.");
    request->rights_attenuation_mask = ZX_RIGHT_SAME_RIGHTS;
  }
  if (request->rights_attenuation_mask != ZX_RIGHT_SAME_RIGHTS) {
    new_node_properties->rights_attenuation_mask() &= request->rights_attenuation_mask;
  }
  logical_buffer_collection().CreateBufferCollectionToken(
      shared_logical_buffer_collection(), new_node_properties, std::move(request->token_request));
}

void BufferCollectionToken::Sync(SyncCompleter::Sync& completer) { SyncImplV1(completer); }

void BufferCollectionToken::DeprecatedSync(DeprecatedSyncCompleter::Sync& completer) {
  SyncImplV1(completer);
}

// Clean token close without causing LogicalBufferCollection failure.
void BufferCollectionToken::Close(CloseCompleter::Sync& completer) { TokenCloseImplV1(completer); }

void BufferCollectionToken::DeprecatedClose(DeprecatedCloseCompleter::Sync& completer) {
  TokenCloseImplV1(completer);
}

void BufferCollectionToken::OnServerKoid() {
  ZX_DEBUG_ASSERT(has_server_koid());
  parent_device()->TrackToken(this);
  if (parent_device()->TryRemoveKoidFromUnfoundTokenList(server_koid())) {
    set_unfound_node();
    // LogicalBufferCollection will print an error, since it might have useful client information.
  }
}

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

void BufferCollectionToken::SetName(SetNameRequestView request, SetNameCompleter::Sync& completer) {
  SetNameImplV1(request, completer);
}

void BufferCollectionToken::DeprecatedSetName(DeprecatedSetNameRequestView request,
                                              DeprecatedSetNameCompleter::Sync& completer) {
  SetNameImplV1(request, completer);
}

void BufferCollectionToken::SetDebugClientInfo(SetDebugClientInfoRequestView request,
                                               SetDebugClientInfoCompleter::Sync& completer) {
  SetDebugClientInfoImplV1(request, completer);
}

void BufferCollectionToken::DeprecatedSetDebugClientInfo(
    DeprecatedSetDebugClientInfoRequestView request,
    DeprecatedSetDebugClientInfoCompleter::Sync& completer) {
  SetDebugClientInfoImplV1(request, completer);
}

void BufferCollectionToken::SetDebugTimeoutLogDeadline(
    SetDebugTimeoutLogDeadlineRequestView request,
    SetDebugTimeoutLogDeadlineCompleter::Sync& completer) {
  SetDebugTimeoutLogDeadlineImplV1(request, completer);
}

void BufferCollectionToken::DeprecatedSetDebugTimeoutLogDeadline(
    DeprecatedSetDebugTimeoutLogDeadlineRequestView request,
    DeprecatedSetDebugTimeoutLogDeadlineCompleter::Sync& completer) {
  SetDebugTimeoutLogDeadlineImplV1(request, completer);
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

void BufferCollectionToken::CreateBufferCollectionTokenGroup(
    CreateBufferCollectionTokenGroupRequestView request,
    CreateBufferCollectionTokenGroupCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "BufferCollectionTokenGroup::CreateBufferCollectionTokenGroup", "this",
                 this, "logical_buffer_collection", &logical_buffer_collection());
  if (is_done_) {
    // Probably a Close() followed by Duplicate(), which is illegal and
    // causes the whole LogicalBufferCollection to fail.
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::CreateBufferCollectionTokenGroup() attempted when is_done_");
    return;
  }
  NodeProperties* new_node_properties = node_properties().NewChild(&logical_buffer_collection());
  logical_buffer_collection().CreateBufferCollectionTokenGroup(
      shared_logical_buffer_collection(), new_node_properties, std::move(request->group_request));
}

void BufferCollectionToken::SetVerboseLogging(SetVerboseLoggingCompleter::Sync& completer) {
  SetVerboseLoggingImplV1(completer);
}

void BufferCollectionToken::GetNodeRef(GetNodeRefCompleter::Sync& completer) {
  GetNodeRefImplV1(completer);
}

void BufferCollectionToken::IsAlternateFor(IsAlternateForRequestView request,
                                           IsAlternateForCompleter::Sync& completer) {
  IsAlternateForImplV1(request, completer);
}

BufferCollectionToken::BufferCollectionToken(
    fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection_param,
    NodeProperties* new_node_properties, zx::unowned_channel server_end)
    : Node(std::move(logical_buffer_collection_param), new_node_properties, std::move(server_end)),
      LoggingMixin("BufferCollectionToken") {
  TRACE_DURATION("gfx", "BufferCollectionToken::BufferCollectionToken", "this", this,
                 "logical_buffer_collection", &logical_buffer_collection());
  inspect_node_ =
      logical_buffer_collection().inspect_node().CreateChild(CreateUniqueName("token-"));
  if (create_status() != ZX_OK) {
    // Node::Node() failed and maybe !has_server_koid().
    return;
  }
  // Node::Node filled this out (or didn't and status() reflected that, which was already checked
  // above).
  ZX_DEBUG_ASSERT(has_server_koid());
  OnServerKoid();
}

void BufferCollectionToken::FailAsync(Location location, zx_status_t status, const char* format,
                                      ...) {
  va_list args;
  va_start(args, format);
  vLog(true, location.file(), location.line(), logging_prefix(), "fail", format, args);
  va_end(args);

  // Idempotent, so only close once.
  if (!server_binding_.has_value())
    return;

  async_failure_result_ = status;
  server_binding_->Close(status);
  server_binding_ = {};
}

bool BufferCollectionToken::ReadyForAllocation() { return false; }

void BufferCollectionToken::OnBuffersAllocated(const AllocationResult& allocation_result) {
  ZX_PANIC("Unexpected call to BufferCollectionToken::OnBuffersAllocated()");
}

BufferCollectionToken* BufferCollectionToken::buffer_collection_token() { return this; }

const BufferCollectionToken* BufferCollectionToken::buffer_collection_token() const { return this; }

BufferCollection* BufferCollectionToken::buffer_collection() { return nullptr; }

const BufferCollection* BufferCollectionToken::buffer_collection() const { return nullptr; }

BufferCollectionTokenGroup* BufferCollectionToken::buffer_collection_token_group() {
  return nullptr;
}

const BufferCollectionTokenGroup* BufferCollectionToken::buffer_collection_token_group() const {
  return nullptr;
}

OrphanedNode* BufferCollectionToken::orphaned_node() { return nullptr; }

const OrphanedNode* BufferCollectionToken::orphaned_node() const { return nullptr; }

bool BufferCollectionToken::is_connected_type() const { return true; }

bool BufferCollectionToken::is_currently_connected() const { return server_binding_.has_value(); }

const char* BufferCollectionToken::node_type_string() const { return "token"; }

}  // namespace sysmem_driver
