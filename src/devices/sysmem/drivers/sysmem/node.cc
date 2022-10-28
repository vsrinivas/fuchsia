// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "node.h"

#include <lib/zx/channel.h>
#include <zircon/types.h>

#include "koid_util.h"
#include "logical_buffer_collection.h"

namespace sysmem_driver {

Node::Node(fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
           NodeProperties* node_properties, zx::unowned_channel server_end)
    : logical_buffer_collection_(std::move(logical_buffer_collection)),
      node_properties_(node_properties),
      server_end_(std::move(server_end)) {
  if (*server_end_) {
    zx_koid_t server_koid;
    zx_koid_t client_koid;
    zx_status_t status =
        get_handle_koids(*server_end_, &server_koid, &client_koid, ZX_OBJ_TYPE_CHANNEL);
    if (status != ZX_OK) {
      create_status_ = status;
      return;
    }
    client_koid_ = client_koid;
    server_koid_ = server_koid;
  }
  create_status_ = ZX_OK;
}

zx_status_t Node::create_status() const {
  ZX_DEBUG_ASSERT(*server_end_ || orphaned_node());
  create_status_was_checked_ = true;
  return create_status_;
}

Node::~Node() {
  // EnsureDetachedFromNodeProperties() should have been called by this point.
  ZX_DEBUG_ASSERT(!node_properties_);
  ZX_DEBUG_ASSERT(create_status_was_checked_);
  // Ok to untrack zx_koid_t{}; also ok to untrack same zx_koid_t again.
}

void Node::Bind(zx::channel server_end) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      server_end.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status == ZX_OK) {
    inspect_node_.CreateUint("channel_koid", info.koid, &properties_);
  }
  // We need to keep a refptr to this class, since the unbind happens asynchronously and can run
  // after the parent closes a handle to this class.
  BindInternal(std::move(server_end),
               [this, this_ref = fbl::RefPtr<Node>(this)](fidl::UnbindInfo info) {
                 if (error_handler_) {
                   zx_status_t status = info.status();
                   if (async_failure_result_.has_value() && info.reason() == fidl::Reason::kClose) {
                     // On kClose the error is always ZX_OK, so report the real error to
                     // LogicalBufferCollection if the close was caused by FailAsync or FailSync.
                     status = *async_failure_result_;
                   }
                   error_handler_(status);
                 }
               });
}

void Node::SetErrorHandler(fit::function<void(zx_status_t)> error_handler) {
  // OrphanedNode deletes SetErrorHandler(), but also make sure OrphanedNode::SetErrorHandler()
  // isn't happening via a cast of an OrphanedNode* to Node* first.
  ZX_DEBUG_ASSERT(!orphaned_node());
  error_handler_ = std::move(error_handler);
}

void Node::Fail(zx_status_t epitaph) { CloseChannel(epitaph); }

void Node::SetDebugClientInfoInternal(std::string name, uint64_t id) {
  node_properties().client_debug_info().name = std::move(name);
  node_properties().client_debug_info().id = id;
  debug_id_property_ =
      inspect_node_.CreateUint("debug_id", node_properties().client_debug_info().id);
  debug_name_property_ =
      inspect_node_.CreateString("debug_name", node_properties().client_debug_info().name);
  if (was_unfound_node()) {
    // Output the debug info now that we have it, since eg. we previously said bad things about this
    // token's server_koid not being found when it should have been, but at that time we didn't have
    // the debug info.
    //
    // This is not a failure here, but the message provides debug info for a failure that previously
    // occurred.
    logical_buffer_collection().LogClientError(FROM_HERE, &node_properties(),
                                               "Got debug info for node %ld", server_koid_);
  }
}

LogicalBufferCollection& Node::logical_buffer_collection() const {
  return *logical_buffer_collection_;
}

fbl::RefPtr<LogicalBufferCollection> Node::shared_logical_buffer_collection() {
  return logical_buffer_collection_;
}

NodeProperties& Node::node_properties() const {
  ZX_ASSERT(node_properties_);
  return *node_properties_;
}

void Node::EnsureDetachedFromNodeProperties() { node_properties_ = nullptr; }

zx::unowned_channel Node::channel() const {
  ZX_ASSERT(is_currently_connected());
  return zx::unowned_channel(server_end_);
}

bool Node::is_done() const {
  ZX_DEBUG_ASSERT(!orphaned_node());
  return is_done_;
}

bool Node::has_client_koid() const {
  return create_status_ == ZX_OK && client_koid_ != zx_koid_t{};
}

zx_koid_t Node::client_koid() const {
  ZX_ASSERT(create_status_ == ZX_OK && client_koid_ != zx_koid_t{});
  return client_koid_;
}

bool Node::has_server_koid() const {
  return create_status_ == ZX_OK && server_koid_ != ZX_KOID_INVALID;
}

zx_koid_t Node::server_koid() const {
  ZX_ASSERT(create_status_ == ZX_OK && server_koid_ != ZX_KOID_INVALID);
  return server_koid_;
}

Device* Node::parent_device() const { return logical_buffer_collection_->parent_device(); }

void Node::CloseChannel(zx_status_t epitaph) {
  // This essentially converts the OnUnboundFn semantic of getting called regardless of channel-fail
  // vs. server-driven-fail into the more typical semantic where error_handler_ only gets called
  // on channel-fail but not on server-driven-fail.
  error_handler_ = {};
  CloseServerBinding(epitaph);
}

}  // namespace sysmem_driver
