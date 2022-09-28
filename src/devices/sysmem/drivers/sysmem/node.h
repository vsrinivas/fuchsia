// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_NODE_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_NODE_H_

#include <fidl/fuchsia.sysmem2/cpp/wire.h>
#include <lib/ddk/trace/event.h>
#include <lib/fit/defer.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <unordered_set>
#include <vector>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "allocation_result.h"
#include "device.h"
#include "koid_util.h"
#include "logical_buffer_collection.h"

namespace sysmem_driver {

class NodeProperties;
class BufferCollection;
class BufferCollectionToken;
class BufferCollectionTokenGroup;
class OrphanedNode;

// Implemented by BufferCollectionToken, BufferCollectionTokenGroup, BufferCollection, and
// OrphanedNode.
//
// Things that can change when transmuting from BufferCollectionToken to BufferCollection, from
// BufferCollectionToken to OrphanedNode, or from BufferCollection to OrphanedNode, should generally
// go in Node.  Things that don't change when transmuting go in NodeProperties.
class Node : public fbl::RefCounted<Node> {
 public:
  Node(const Node& to_copy) = delete;
  Node(Node&& to_move) = delete;

  Node(fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
       NodeProperties* node_properties, zx::unowned_channel server_end);
  // Construction status.
  zx_status_t create_status() const;
  virtual ~Node();

  void Bind(zx::channel server_end);

  void SetErrorHandler(fit::function<void(zx_status_t)> error_handler);

  // The Node must have 0 children to call Fail().
  void Fail(zx_status_t epitaph);

  // Not all Node(s) that are ReadyForAllocation() have buffer_collection_constraints().  In
  // particular an OrphanedNode is always ReadyForAllocation(), but may or may not have
  // buffer_collection_constraints().
  virtual bool ReadyForAllocation() = 0;

  // buffers_logically_allocated() must be false to call this.
  virtual void OnBuffersAllocated(const AllocationResult& allocation_result) = 0;

  // If this Node is a BufferCollectionToken, returns the BufferCollectionToken*, else returns
  // nullptr.
  virtual BufferCollectionToken* buffer_collection_token() = 0;
  virtual const BufferCollectionToken* buffer_collection_token() const = 0;
  // If this Node is a BufferCollection, returns the BufferCollection*, else returns nullptr.
  virtual BufferCollection* buffer_collection() = 0;
  virtual const BufferCollection* buffer_collection() const = 0;
  // If this Node is an OrphanedNode, returns the OrphanedNode*, else returns nullptr.
  virtual OrphanedNode* orphaned_node() = 0;
  virtual const OrphanedNode* orphaned_node() const = 0;
  // If this Node is a BufferCollectionTokenGroup, returns the BufferCollectionTokenGroup*, else
  // returns nullptr.
  virtual BufferCollectionTokenGroup* buffer_collection_token_group() = 0;
  virtual const BufferCollectionTokenGroup* buffer_collection_token_group() const = 0;
  // This is a constant per sub-class of Node.  When a "connected" node is no longer connected, the
  // Node sub-class is replaced with OrphanedNode, or deleted as appropriate.
  virtual bool is_connected_type() const = 0;
  // This is dynamic depending on whether the Node sub-class server-side binding is currently bound
  // or in other words whether the node is currently connected.  This will always return false
  // when !is_connected_type(), and can return true or false if is_connected_type().
  virtual bool is_currently_connected() const = 0;
  virtual const char* node_type_string() const = 0;

  LogicalBufferCollection& logical_buffer_collection() const;
  fbl::RefPtr<LogicalBufferCollection> shared_logical_buffer_collection();

  // If the NodeProperties this Node started with is gone, this asserts, including in release.  A
  // hard crash is better than going off in the weeds.
  NodeProperties& node_properties() const;

  void EnsureDetachedFromNodeProperties();

  // Returns server end of the channel serving this node.  At least for now, this must only be
  // called when it's known that the binding is still valid.  We check this using
  // is_currently_connected().
  zx::unowned_channel channel() const;

  bool is_done() const;

  bool has_client_koid() const;
  zx_koid_t client_koid() const;
  bool has_server_koid() const;
  zx_koid_t server_koid() const;

  void set_unfound_node() { was_unfound_node_ = true; }
  bool was_unfound_node() const { return was_unfound_node_; }

  Device* parent_device() const;

  void SetDebugClientInfoInternal(std::string name, uint64_t id);

 protected:
  // Called during Bind() to perform the sub-class protocol-specific bind itself.
  using ErrorHandlerWrapper = fit::function<void(fidl::UnbindInfo info)>;
  virtual void BindInternal(zx::channel server_end, ErrorHandlerWrapper error_handler_wrapper) = 0;

  template <typename Completer>
  void FailSync(Location location, Completer& completer, zx_status_t status, const char* format,
                ...) {
    va_list args;
    va_start(args, format);
    logical_buffer_collection().VLogClientError(location, &node_properties(), format, args);
    va_end(args);

    completer.Close(status);
    async_failure_result_ = status;
  }

  template <class SyncCompleterSync>
  void SyncImplV1(SyncCompleterSync& completer) {
    TRACE_DURATION("gfx", "Node::SyncImpl", "this", this, "logical_buffer_collection",
                   &logical_buffer_collection());
    // This isn't real churn.  As a temporary measure, we need to count churn despite there not
    // being any, since more real churn is coming soon, and we need to test the mitigation of that
    // churn.
    //
    // TODO(fxbug.dev/33670): Remove this fake churn count once we're creating real churn from tests
    // using new messages.  Also consider making TableSet::CountChurn() private.
    table_set().CountChurn();

    table_set().MitigateChurn();
    if (is_done_) {
      // Probably a Close() followed by Sync(), which is illegal and
      // causes the whole LogicalBufferCollection to fail.
      FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE, "Sync() after Close()");
      return;
    }

    completer.Reply();
  }

  template <class CloseCompleterSync>
  void CloseImplV1(CloseCompleterSync& completer) {
    table_set().MitigateChurn();
    if (is_done_) {
      FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE, "Close() after Close()");
      return;
    }
    // We still want to enforce that the client doesn't send any other messages
    // between Close() and closing the channel, so we just set is_done_ here and
    // do a FailSync() if is_done_ is seen to be set while handling any other
    // message.
    is_done_ = true;
  }

  template <class SetNameRequestView, class SetNameCompleterSync>
  void SetNameImplV1(SetNameRequestView request, SetNameCompleterSync& completer) {
    table_set().MitigateChurn();
    if (is_done_) {
      FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE, "SetName() after Close()");
      return;
    }
    logical_buffer_collection().SetName(request->priority,
                                        std::string(request->name.begin(), request->name.end()));
  }

  template <class SetDebugClientInfoRequestView, class SetDebugClientInfoCompleterSync>
  void SetDebugClientInfoImplV1(SetDebugClientInfoRequestView request,
                                SetDebugClientInfoCompleterSync& completer) {
    table_set().MitigateChurn();
    if (is_done_) {
      FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE, "SetDebugClientInfo() after Close()");
      return;
    }
    SetDebugClientInfoInternal(std::string(request->name.begin(), request->name.end()),
                               request->id);
  }

  template <class SetDebugTimeoutLogDeadlineRequestView,
            class SetDebugTimeoutLogDeadlineCompleterSync>
  void SetDebugTimeoutLogDeadlineImplV1(SetDebugTimeoutLogDeadlineRequestView request,
                                        SetDebugTimeoutLogDeadlineCompleterSync& completer) {
    table_set().MitigateChurn();
    if (is_done_) {
      FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
               "SetDebugTimeoutLogDeadline() after Close()");
      return;
    }
    logical_buffer_collection().SetDebugTimeoutLogDeadline(request->deadline);
  }

  template <class SetVerboseLoggingCompleterSync>
  void SetVerboseLoggingImplV1(SetVerboseLoggingCompleterSync& completer) {
    table_set().MitigateChurn();
    if (is_done_) {
      FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE, "SetVerboseLogging() after Close()");
      return;
    }
    logical_buffer_collection_->SetVerboseLogging();
  }

  void CloseChannel(zx_status_t epitaph);

  virtual void CloseServerBinding(zx_status_t epitaph) = 0;

  TableSet& table_set() { return table_set_; }

  // Becomes true on the first Close() (or BindSharedCollection(), in the case of
  // BufferCollectionToken).  This being true means a channel close is not fatal to the node's
  // sub-tree.  However, if the client sends a redundant Close(), that is fatal to the node's
  // sub-tree.
  bool is_done_ = false;

  std::optional<zx_status_t> async_failure_result_;

  // Used by all Node subclasses except OrphanedNode.
  fit::function<void(zx_status_t)> error_handler_;

  inspect::Node inspect_node_;
  inspect::UintProperty debug_id_property_;
  inspect::StringProperty debug_name_property_;
  inspect::ValueList properties_;

 private:
  // Construction status.
  zx_status_t create_status_ = ZX_ERR_INTERNAL;
  // At least one call to status() needs to happen before ~Node, typically shortly after
  // construction (and if status is failed, typically that one check of status() will also be
  // shortly before destruction).
  mutable bool create_status_was_checked_ = false;

  // This is in Node instead of NodeProperties because when BufferCollectionToken or
  // BufferCollection becomes an OrphanedNode, we no longer reference LogicalBufferCollection.
  fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection_;

  // Cached from LogicalBufferCollection.
  TableSet& table_set_;

  // The Node is co-owned by the NodeProperties, so the Node has a raw pointer back to
  // NodeProperties.
  //
  // This pointer is set to nullptr during ~NodeProperties(), so if we attempt to access via
  // node_properties_ after that, we'll get a hard crash instead of going off in the weeds.
  //
  // The main way we avoid accessing NodeProperties beyond when it goes away is the setting of
  // error_handler_ = {} in the two CloseChannel() methods.  We rely on sub-class's error_handler_
  // not running after CloseChannel(), and we rely on LLCPP not calling protocol message handlers
  // after server binding Close() (other than completion of any currently-in-progress message
  // handler), since we're running Close() on the same dispatcher.
  NodeProperties* node_properties_ = nullptr;

  // We keep server_end_ around
  zx::unowned_channel server_end_;
  zx_koid_t client_koid_ = ZX_KOID_INVALID;
  zx_koid_t server_koid_ = ZX_KOID_INVALID;

  // If true, this node was looked up by koid at some previous time, but at that time the koid
  // wasn't found.  When true, we log info later if/when the koid shows up and/or debug information
  // shows up.
  bool was_unfound_node_ = false;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_NODE_H_
