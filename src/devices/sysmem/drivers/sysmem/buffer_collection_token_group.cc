// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_collection_token_group.h"

#include <lib/ddk/trace/event.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <zircon/errors.h>
#include <zircon/rights.h>

#include "fidl/fuchsia.sysmem/cpp/markers.h"
#include "node.h"
#include "src/devices/sysmem/drivers/sysmem/node_properties.h"

namespace sysmem_driver {

void BufferCollectionTokenGroup::Sync(SyncCompleter::Sync& completer) { SyncImplV1(completer); }

void BufferCollectionTokenGroup::Close(CloseCompleter::Sync& completer) { CloseImplV1(completer); }

void BufferCollectionTokenGroup::SetName(SetNameRequestView request,
                                         SetNameCompleter::Sync& completer) {
  SetNameImplV1(request, completer);
}

void BufferCollectionTokenGroup::SetDebugClientInfo(SetDebugClientInfoRequestView request,
                                                    SetDebugClientInfoCompleter::Sync& completer) {
  SetDebugClientInfoImplV1(request, completer);
}

void BufferCollectionTokenGroup::SetDebugTimeoutLogDeadline(
    SetDebugTimeoutLogDeadlineRequestView request,
    SetDebugTimeoutLogDeadlineCompleter::Sync& completer) {
  SetDebugTimeoutLogDeadlineImplV1(request, completer);
}

void BufferCollectionTokenGroup::SetVerboseLogging(SetVerboseLoggingCompleter::Sync& completer) {
  SetVerboseLoggingImplV1(completer);
}

void BufferCollectionTokenGroup::CreateChild(CreateChildRequestView request,
                                             CreateChildCompleter::Sync& completer) {
  table_set().MitigateChurn();
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE, "CreateChild() after Close()");
    return;
  }
  if (is_all_children_present_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE, "CreateChild() after AllChildrenPresent()");
    return;
  }
  if (!request->has_token_request()) {
    FailSync(FROM_HERE, completer, ZX_ERR_INVALID_ARGS, "CreateChild() missing token_request");
    return;
  }
  uint32_t rights_attenuation_mask = ZX_RIGHT_SAME_RIGHTS;
  if (request->has_rights_attenuation_mask()) {
    rights_attenuation_mask = request->rights_attenuation_mask();
  }
  NodeProperties* new_node_properties = node_properties().NewChild(&logical_buffer_collection());
  if (rights_attenuation_mask != ZX_RIGHT_SAME_RIGHTS) {
    new_node_properties->rights_attenuation_mask() &=
        static_cast<uint32_t>(rights_attenuation_mask);
  }
  logical_buffer_collection().CreateBufferCollectionToken(
      shared_logical_buffer_collection(), new_node_properties, std::move(request->token_request()));
}

void BufferCollectionTokenGroup::CreateChildrenSync(CreateChildrenSyncRequestView request,
                                                    CreateChildrenSyncCompleter::Sync& completer) {
  table_set().MitigateChurn();
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE, "CreateChildrenSync() after Close()");
    return;
  }
  if (is_all_children_present_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "CreateChildrenSync() after AllChildrenPresent()");
    return;
  }
  std::vector<fidl::ClientEnd<fuchsia_sysmem::BufferCollectionToken>> new_tokens;
  for (auto& rights_attenuation_mask : request->rights_attenuation_masks) {
    auto token_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
    if (!token_endpoints.is_ok()) {
      FailSync(FROM_HERE, completer, token_endpoints.status_value(),
               "BufferCollectionTokenGroup::CreateChildrenSync() failed to create token channel.");
      return;
    }

    NodeProperties* new_node_properties = node_properties().NewChild(&logical_buffer_collection());
    if (rights_attenuation_mask != ZX_RIGHT_SAME_RIGHTS) {
      new_node_properties->rights_attenuation_mask() &=
          static_cast<uint32_t>(rights_attenuation_mask);
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

void BufferCollectionTokenGroup::AllChildrenPresent(AllChildrenPresentCompleter::Sync& completer) {
  table_set().MitigateChurn();
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE, "AllChildrenPresent() after Close()");
    return;
  }
  if (is_all_children_present_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "AllChildrenPresent() after AllChildrenPresent()");
    return;
  }
  if (node_properties().child_count() < 1) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE, "AllChildrenPresent() without any children");
    return;
  }
  is_all_children_present_ = true;
  logical_buffer_collection().OnNodeReady();
}

BufferCollectionTokenGroup& BufferCollectionTokenGroup::EmplaceInTree(
    fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
    NodeProperties* new_node_properties, zx::unowned_channel server_end) {
  auto group = fbl::AdoptRef(new BufferCollectionTokenGroup(
      std::move(logical_buffer_collection), new_node_properties, std::move(server_end)));
  auto group_ptr = group.get();
  new_node_properties->SetNode(group);
  return *group_ptr;
}

BufferCollectionTokenGroup::BufferCollectionTokenGroup(fbl::RefPtr<LogicalBufferCollection> parent,
                                                       NodeProperties* new_node_properties,
                                                       zx::unowned_channel server_end)
    : Node(std::move(parent), new_node_properties, std::move(server_end)) {
  TRACE_DURATION("gfx", "BufferCollectionTokenGroup::BufferCollectionTokenGroup", "this", this,
                 "logical_buffer_collection", &this->logical_buffer_collection());
  ZX_DEBUG_ASSERT(shared_logical_buffer_collection());
  inspect_node_ =
      this->logical_buffer_collection().inspect_node().CreateChild(CreateUniqueName("group-"));
}

void BufferCollectionTokenGroup::BindInternal(zx::channel group_request,
                                              ErrorHandlerWrapper error_handler_wrapper) {
  server_binding_ =
      fidl::BindServer(parent_device()->dispatcher(), std::move(group_request), this,
                       [error_handler_wrapper = std::move(error_handler_wrapper)](
                           BufferCollectionTokenGroup* group, fidl::UnbindInfo info,
                           fidl::ServerEnd<fuchsia_sysmem::BufferCollectionTokenGroup> channel) {
                         error_handler_wrapper(info);
                       });
}

bool BufferCollectionTokenGroup::ReadyForAllocation() { return is_all_children_present_; }

void BufferCollectionTokenGroup::OnBuffersAllocated(const AllocationResult& allocation_result) {
  node_properties().SetBuffersLogicallyAllocated();
}

BufferCollectionToken* BufferCollectionTokenGroup::buffer_collection_token() { return nullptr; }

const BufferCollectionToken* BufferCollectionTokenGroup::buffer_collection_token() const {
  return nullptr;
}

BufferCollection* BufferCollectionTokenGroup::buffer_collection() { return nullptr; }

const BufferCollection* BufferCollectionTokenGroup::buffer_collection() const { return nullptr; }

OrphanedNode* BufferCollectionTokenGroup::orphaned_node() { return nullptr; }

const OrphanedNode* BufferCollectionTokenGroup::orphaned_node() const { return nullptr; }

BufferCollectionTokenGroup* BufferCollectionTokenGroup::buffer_collection_token_group() {
  return this;
}

const BufferCollectionTokenGroup* BufferCollectionTokenGroup::buffer_collection_token_group()
    const {
  return this;
}

bool BufferCollectionTokenGroup::is_connected_type() const { return true; }

bool BufferCollectionTokenGroup::is_currently_connected() const {
  return server_binding_.has_value();
}

void BufferCollectionTokenGroup::CloseServerBinding(zx_status_t epitaph) {
  if (server_binding_) {
    server_binding_->Close(epitaph);
  }
  server_binding_ = {};
}

const char* BufferCollectionTokenGroup::node_type_string() const { return "group"; }

}  // namespace sysmem_driver
