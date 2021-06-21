// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_TOKEN_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_TOKEN_H_

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fidl-async-2/fidl_server.h>
#include <lib/fidl-async-2/simple_binding.h>

#include "logging.h"
#include "logical_buffer_collection.h"
#include "node.h"

namespace sysmem_driver {

class BufferCollectionToken;

class BufferCollectionToken : public Node,
                              public fidl::WireServer<fuchsia_sysmem::BufferCollectionToken>,
                              public LoggingMixin {
 public:
  ~BufferCollectionToken() override;

  // The returned reference is owned by new_node_properties, which in turn is owned by
  // logical_buffer_collection->root_.
  static BufferCollectionToken& EmplaceInTree(
      Device* parent_device, fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
      NodeProperties* new_node_properties);

  void SetErrorHandler(fit::function<void(zx_status_t)> error_handler) {
    error_handler_ = std::move(error_handler);
  }
  void Bind(fidl::ServerEnd<fuchsia_sysmem::BufferCollectionToken> token_request);

  void DuplicateSync(DuplicateSyncRequestView request,
                     DuplicateSyncCompleter::Sync& completer) override;
  void Duplicate(DuplicateRequestView request, DuplicateCompleter::Sync& completer) override;
  void Sync(SyncRequestView request, SyncCompleter::Sync& completer) override;
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override;
  void SetName(SetNameRequestView request, SetNameCompleter::Sync& completer) override;
  void SetDebugClientInfo(SetDebugClientInfoRequestView request,
                          SetDebugClientInfoCompleter::Sync& completer) override;
  void SetDebugTimeoutLogDeadline(SetDebugTimeoutLogDeadlineRequestView request,
                                  SetDebugTimeoutLogDeadlineCompleter::Sync& completer) override;
  void SetDispensable(SetDispensableRequestView request,
                      SetDispensableCompleter::Sync& completer) override;

  void SetDebugClientInfoInternal(std::string name, uint64_t id);

  void SetServerKoid(zx_koid_t server_koid);
  zx_koid_t server_koid();

  void SetDispensableInternal();

  bool is_done();

  bool was_unfound_token() const { return was_unfound_token_; }

  void SetBufferCollectionRequest(zx::channel buffer_collection_request);

  zx::channel TakeBufferCollectionRequest();

  void CloseChannel(zx_status_t epitaph);

  // Node interface
  bool ReadyForAllocation() override;
  void OnBuffersAllocated(const AllocationResult& allocation_result) override;
  void Fail(zx_status_t epitaph) override;
  BufferCollectionToken* buffer_collection_token() override;
  const BufferCollectionToken* buffer_collection_token() const override;
  BufferCollection* buffer_collection() override;
  const BufferCollection* buffer_collection() const override;
  OrphanedNode* orphaned_node() override;
  const OrphanedNode* orphaned_node() const override;
  bool is_connected() const override;

 private:
  friend class FidlServer;

  BufferCollectionToken(Device* parent_device, fbl::RefPtr<LogicalBufferCollection> parent,
                        NodeProperties* new_node_properties);

  void FailAsync(Location location, zx_status_t status, const char* format, ...);

  template <typename Completer>
  void FailSync(Location location, Completer& completer, zx_status_t status, const char* format,
                ...);

  Device* parent_device_ = nullptr;

  // Cached from parent_.
  TableSet& table_set_;

  std::optional<zx_status_t> async_failure_result_;
  fit::function<void(zx_status_t)> error_handler_;

  zx_koid_t server_koid_ = ZX_KOID_INVALID;

  // Becomes true on the first Close() or BindSharedCollection().  This being
  // true means a channel close is not fatal to the LogicalBufferCollection.
  // However, if the client sends a redundant Close(), that is fatal to the
  // BufferCollectionToken and fatal to the LogicalBufferCollection.
  bool is_done_ = false;

  bool was_unfound_token_ = false;

  std::optional<fidl::ServerBindingRef<fuchsia_sysmem::BufferCollectionToken>> server_binding_;

  // This is set up to once during
  // LogicalBufferCollection::BindSharedCollection(), and essentially curries
  // the buffer_collection_request past the processing of any remaining
  // inbound messages on the BufferCollectionToken before starting to serve
  // the BufferCollection that the token was exchanged for.  This way, inbound
  // Duplicate() messages in the BufferCollectionToken are seen before any
  // BufferCollection::SetConstraints() (which might otherwise try to allocate
  // buffers too soon before all tokens are gone).
  zx::channel buffer_collection_request_;

  inspect::Node inspect_node_;
  inspect::UintProperty debug_id_property_;
  inspect::StringProperty debug_name_property_;
  inspect::ValueList properties_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_TOKEN_H_
