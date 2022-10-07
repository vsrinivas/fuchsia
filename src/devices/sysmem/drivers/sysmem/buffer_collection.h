// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_H_

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fidl/fuchsia.sysmem2/cpp/wire.h>
#include <lib/fidl/internal.h>

#include <list>

#include "lib/zx/channel.h"
#include "logging.h"
#include "logical_buffer_collection.h"
#include "node.h"
#include "src/devices/sysmem/drivers/sysmem/device.h"

namespace sysmem_driver {

class BufferCollection : public Node, public fidl::WireServer<fuchsia_sysmem::BufferCollection> {
 public:
  // Use EmplaceInTree() instead of Create() (until we switch to llcpp when we can have a new
  // Create() that does what EmplaceInTree() currently does).  The returned reference is valid while
  // this Node is in the tree under root_.
  static BufferCollection& EmplaceInTree(
      fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection, BufferCollectionToken* token,
      zx::unowned_channel server_end);
  ~BufferCollection() override;

  // FIDL "compose Node" "interface" (identical among BufferCollection, BufferCollectionToken,
  // BufferCollectionTokenGroup)
  void Sync(SyncCompleter::Sync& completer) override;
  void DeprecatedSync(DeprecatedSyncCompleter::Sync& completer) override;
  void Close(CloseCompleter::Sync& completer) override;
  void DeprecatedClose(DeprecatedCloseCompleter::Sync& completer) override;
  void GetNodeRef(GetNodeRefCompleter::Sync& completer) override;
  void IsAlternateFor(IsAlternateForRequestView request,
                      IsAlternateForCompleter::Sync& completer) override;
  void SetName(SetNameRequestView request, SetNameCompleter::Sync& completer) override;
  void DeprecatedSetName(DeprecatedSetNameRequestView request,
                         DeprecatedSetNameCompleter::Sync& completer) override;
  void SetDebugClientInfo(SetDebugClientInfoRequestView request,
                          SetDebugClientInfoCompleter::Sync& completer) override;
  void DeprecatedSetDebugClientInfo(
      DeprecatedSetDebugClientInfoRequestView request,
      DeprecatedSetDebugClientInfoCompleter::Sync& completer) override;
  void SetDebugTimeoutLogDeadline(SetDebugTimeoutLogDeadlineRequestView request,
                                  SetDebugTimeoutLogDeadlineCompleter::Sync& completer) override;
  void SetVerboseLogging(SetVerboseLoggingCompleter::Sync& completer) override;

  //
  // fuchsia.sysmem.BufferCollection interface methods (see also "compose Node" methods above)
  //
  void SetConstraints(SetConstraintsRequestView request,
                      SetConstraintsCompleter::Sync& completer) override;
  void WaitForBuffersAllocated(WaitForBuffersAllocatedCompleter::Sync& completer) override;
  void CheckBuffersAllocated(CheckBuffersAllocatedCompleter::Sync& completer) override;
  void SetConstraintsAuxBuffers(SetConstraintsAuxBuffersRequestView request,
                                SetConstraintsAuxBuffersCompleter::Sync& completer) override;
  void GetAuxBuffers(GetAuxBuffersCompleter::Sync& completer) override;
  void AttachToken(AttachTokenRequestView request, AttachTokenCompleter::Sync& completer) override;
  void AttachLifetimeTracking(AttachLifetimeTrackingRequestView request,
                              AttachLifetimeTrackingCompleter::Sync& completer) override;

  //
  // LogicalBufferCollection uses these:
  //

  bool is_set_constraints_seen() const { return is_set_constraints_seen_; }
  bool has_constraints();

  // has_constraints() must be true to call this.
  const fuchsia_sysmem2::wire::BufferCollectionConstraints& constraints();

  // has_constraints() must be true to call this, and will stay true after calling this.
  fuchsia_sysmem2::wire::BufferCollectionConstraints CloneConstraints();

  fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection_shared();

  bool should_propagate_failure_to_parent_node() const;

  // Node interface
  bool ReadyForAllocation() override;
  void OnBuffersAllocated(const AllocationResult& allocation_result) override;
  BufferCollectionToken* buffer_collection_token() override;
  const BufferCollectionToken* buffer_collection_token() const override;
  BufferCollection* buffer_collection() override;
  const BufferCollection* buffer_collection() const override;
  BufferCollectionTokenGroup* buffer_collection_token_group() override;
  const BufferCollectionTokenGroup* buffer_collection_token_group() const override;
  OrphanedNode* orphaned_node() override;
  const OrphanedNode* orphaned_node() const override;
  bool is_connected_type() const override;
  bool is_currently_connected() const override;
  const char* node_type_string() const override;

 protected:
  void BindInternal(zx::channel collection_request,
                    ErrorHandlerWrapper error_handler_wrapper) override;

 private:
  friend class FidlServer;

  explicit BufferCollection(fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection,
                            const BufferCollectionToken& token, zx::unowned_channel server_end);

  void CloseServerBinding(zx_status_t epitaph) override;

  // The rights attenuation mask driven by usage, so that read-only usage
  // doesn't get write, etc.
  uint32_t GetUsageBasedRightsAttenuation();

  uint32_t GetClientVmoRights();
  uint32_t GetClientAuxVmoRights();
  void MaybeCompleteWaitForBuffersAllocated();
  void MaybeFlushPendingLifetimeTracking();

  void FailAsync(Location location, zx_status_t status, const char* format, ...) __PRINTFLIKE(4, 5);
  // FailSync must be used instead of FailAsync if the current method has a completer that needs a
  // reply.
  template <typename Completer>
  void FailSync(Location location, Completer& completer, zx_status_t status, const char* format,
                ...) __PRINTFLIKE(5, 6);

  fpromise::result<fuchsia_sysmem2::wire::BufferCollectionInfo> CloneResultForSendingV2(
      const fuchsia_sysmem2::wire::BufferCollectionInfo& buffer_collection_info);

  fpromise::result<fuchsia_sysmem::wire::BufferCollectionInfo2> CloneResultForSendingV1(
      const fuchsia_sysmem2::wire::BufferCollectionInfo& buffer_collection_info);
  fpromise::result<fuchsia_sysmem::wire::BufferCollectionInfo2> CloneAuxBuffersResultForSendingV1(
      const fuchsia_sysmem2::wire::BufferCollectionInfo& buffer_collection_info);

  // Temporarily holds fuchsia.sysmem.BufferCollectionConstraintsAuxBuffers until SetConstraints()
  // arrives.
  std::optional<TableHolder<fuchsia_sysmem::wire::BufferCollectionConstraintsAuxBuffers>>
      constraints_aux_buffers_;

  // FIDL protocol enforcement.
  bool is_set_constraints_seen_ = false;
  bool is_set_constraints_aux_buffers_seen_ = false;

  std::list<std::pair</*async_id*/ uint64_t, WaitForBuffersAllocatedCompleter::Async>>
      pending_wait_for_buffers_allocated_;

  bool is_done_ = false;

  std::optional<fidl::ServerBindingRef<fuchsia_sysmem::BufferCollection>> server_binding_;

  // Becomes set when OnBuffersAllocated() is called, and stays set after that.
  std::optional<AllocationResult> logical_allocation_result_;

  struct PendingLifetimeTracking {
    zx::eventpair server_end;
    uint32_t buffers_remaining;
  };
  std::vector<PendingLifetimeTracking> pending_lifetime_tracking_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_H_
