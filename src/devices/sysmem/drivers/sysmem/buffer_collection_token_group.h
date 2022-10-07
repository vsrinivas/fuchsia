// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_TOKEN_GROUP_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_TOKEN_GROUP_H_

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fidl/fuchsia.sysmem2/cpp/wire.h>

#include "lib/zx/channel.h"
#include "node.h"

namespace sysmem_driver {

// A BufferCollectionTokenGroup represents a prioritized OR among the child tokens of the group.
//
// For example a participant can create a first token that's preferred and a second token that's
// fallback.  If aggregation using the preferred token fails, aggregation will be re-attempted using
// the fallback token.
class BufferCollectionTokenGroup
    : public Node,
      public fidl::WireServer<fuchsia_sysmem::BufferCollectionTokenGroup> {
 public:
  static BufferCollectionTokenGroup& EmplaceInTree(
      fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
      NodeProperties* node_properties, zx::unowned_channel server_end);

  // FIDL "compose Node" "interface" (identical among BufferCollection, BufferCollectionToken,
  // BufferCollectionTokenGroup)
  void Sync(SyncCompleter::Sync& completer) override;
  void Close(CloseCompleter::Sync& completer) override;
  void GetNodeRef(GetNodeRefCompleter::Sync& completer) override;
  void IsAlternateFor(IsAlternateForRequestView request,
                      IsAlternateForCompleter::Sync& completer) override;
  void SetName(SetNameRequestView request, SetNameCompleter::Sync& completer) override;
  void SetDebugClientInfo(SetDebugClientInfoRequestView request,
                          SetDebugClientInfoCompleter::Sync& completer) override;
  void SetDebugTimeoutLogDeadline(SetDebugTimeoutLogDeadlineRequestView request,
                                  SetDebugTimeoutLogDeadlineCompleter::Sync& completer) override;
  void SetVerboseLogging(SetVerboseLoggingCompleter::Sync& completer) override;

  //
  // fuchsia.sysmem.BufferCollectionTokenGroup interface methods (see also "compose Node" methods
  // above)
  //
  void CreateChild(CreateChildRequestView request, CreateChildCompleter::Sync& completer) override;
  void CreateChildrenSync(CreateChildrenSyncRequestView request,
                          CreateChildrenSyncCompleter::Sync& completer) override;
  void AllChildrenPresent(AllChildrenPresentCompleter::Sync& completer) override;

  // Node interface
  bool ReadyForAllocation() override;

  void OnBuffersAllocated(const AllocationResult& allocation_result) override;
  BufferCollectionToken* buffer_collection_token() override;
  const BufferCollectionToken* buffer_collection_token() const override;
  BufferCollection* buffer_collection() override;
  const BufferCollection* buffer_collection() const override;
  OrphanedNode* orphaned_node() override;
  const OrphanedNode* orphaned_node() const override;
  BufferCollectionTokenGroup* buffer_collection_token_group() override;
  const BufferCollectionTokenGroup* buffer_collection_token_group() const override;
  bool is_connected_type() const override;
  bool is_currently_connected() const override;
  const char* node_type_string() const override;

 protected:
  void BindInternal(zx::channel group_request, ErrorHandlerWrapper error_handler_wrapper) override;
  void CloseServerBinding(zx_status_t epitaph) override;

 private:
  BufferCollectionTokenGroup(fbl::RefPtr<LogicalBufferCollection> parent,
                             NodeProperties* new_node_properties, zx::unowned_channel server_end);

  std::optional<fidl::ServerBindingRef<fuchsia_sysmem::BufferCollectionTokenGroup>> server_binding_;

  bool is_all_children_present_ = false;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_TOKEN_GROUP_H_
