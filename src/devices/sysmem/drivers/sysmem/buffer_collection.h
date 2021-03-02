// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/heap_allocator.h>

#include <list>

#include "binding_handle.h"
#include "logging.h"
#include "logical_buffer_collection.h"

namespace sysmem_driver {

class BufferCollection : public llcpp::fuchsia::sysmem::BufferCollection::Interface,
                         public fbl::RefCounted<BufferCollection> {
 public:
  ~BufferCollection();

  static BindingHandle<BufferCollection> Create(fbl::RefPtr<LogicalBufferCollection> parent);

  void SetErrorHandler(fit::function<void(zx_status_t)> error_handler) {
    error_handler_ = std::move(error_handler);
  }
  void Bind(zx::channel channel);

  //
  // fuchsia.sysmem.BufferCollection interface methods
  //

  void SetEventSink(fidl::ClientEnd<llcpp::fuchsia::sysmem::BufferCollectionEvents>
                        buffer_collection_events_client,
                    SetEventSinkCompleter::Sync& completer) override;
  void Sync(SyncCompleter::Sync& completer) override;
  void SetConstraints(bool has_constraints,
                      llcpp::fuchsia::sysmem::wire::BufferCollectionConstraints constraints,
                      SetConstraintsCompleter::Sync& completer) override;
  void WaitForBuffersAllocated(WaitForBuffersAllocatedCompleter::Sync& completer) override;
  void CheckBuffersAllocated(CheckBuffersAllocatedCompleter::Sync& completer) override;
  void CloseSingleBuffer(uint64_t buffer_index,
                         CloseSingleBufferCompleter::Sync& completer) override;
  void AllocateSingleBuffer(uint64_t buffer_index,
                            AllocateSingleBufferCompleter::Sync& completer) override;
  void WaitForSingleBufferAllocated(
      uint64_t buffer_index, WaitForSingleBufferAllocatedCompleter::Sync& completer) override;
  void CheckSingleBufferAllocated(uint64_t buffer_index,
                                  CheckSingleBufferAllocatedCompleter::Sync& completer) override;
  void Close(CloseCompleter::Sync& completer) override;
  void SetName(uint32_t priority, fidl::StringView name,
               SetNameCompleter::Sync& completer) override;
  void SetDebugClientInfo(fidl::StringView name, uint64_t id,
                          SetDebugClientInfoCompleter::Sync& completer) override;
  void SetConstraintsAuxBuffers(
      llcpp::fuchsia::sysmem::wire::BufferCollectionConstraintsAuxBuffers constraints_aux_buffers,
      SetConstraintsAuxBuffersCompleter::Sync& completer) override;
  void GetAuxBuffers(GetAuxBuffersCompleter::Sync& completer) override;

  //
  // LogicalBufferCollection uses these:
  //

  void OnBuffersAllocated();

  bool has_constraints();

  // has_constraints() must be true to call this.
  //
  // this can only be called if TakeConstraints() hasn't been called yet.
  const llcpp::fuchsia::sysmem2::wire::BufferCollectionConstraints& constraints();

  // has_constraints() must be true to call this.
  //
  // this can only be called once
  llcpp::fuchsia::sysmem2::wire::BufferCollectionConstraints TakeConstraints();

  LogicalBufferCollection* parent();

  fbl::RefPtr<LogicalBufferCollection> parent_shared();

  void CloseChannel();

  bool is_done();

  void SetDebugClientInfo(std::string name, uint64_t id);

  const std::string& debug_name() const { return debug_info_.name; }
  uint64_t debug_id() const { return debug_info_.id; }

 private:
  explicit BufferCollection(fbl::RefPtr<LogicalBufferCollection> parent);

  // The rights attenuation mask driven by usage, so that read-only usage
  // doesn't get write, etc.
  uint32_t GetUsageBasedRightsAttenuation();

  uint32_t GetClientVmoRights();
  uint32_t GetClientAuxVmoRights();
  void MaybeCompleteWaitForBuffersAllocated();

  void FailAsync(zx_status_t status, const char* format, ...) __PRINTFLIKE(3, 4);
  // FailSync must be used instead of FailAsync if the current method has a completer that needs a
  // reply.
  template <typename Completer>
  void FailSync(Location location, Completer& completer, zx_status_t status, const char* format,
                ...) __PRINTFLIKE(5, 6);

  fit::result<llcpp::fuchsia::sysmem2::wire::BufferCollectionInfo> CloneResultForSendingV2(
      const llcpp::fuchsia::sysmem2::wire::BufferCollectionInfo& buffer_collection_info);

  fit::result<llcpp::fuchsia::sysmem::wire::BufferCollectionInfo_2> CloneResultForSendingV1(
      const llcpp::fuchsia::sysmem2::wire::BufferCollectionInfo& buffer_collection_info);
  fit::result<llcpp::fuchsia::sysmem::wire::BufferCollectionInfo_2>
  CloneAuxBuffersResultForSendingV1(
      const llcpp::fuchsia::sysmem2::wire::BufferCollectionInfo& buffer_collection_info);

  static const fuchsia_sysmem_BufferCollection_ops_t kOps;

  fbl::RefPtr<LogicalBufferCollection> parent_;
  std::optional<zx_status_t> async_failure_result_;
  fit::function<void(zx_status_t)> error_handler_;

  // Cached from LogicalBufferCollection.
  TableSet& table_set_;

  // Client end of a BufferCollectionEvents channel, for the local server to
  // send events to the remote client.  All of the messages in this interface
  // are one-way with no response, so sending an event doesn't block the
  // server thread.
  //
  // This may remain non-set if SetEventSink() is never used by a client.  A
  // client may send SetEventSink() up to once.
  //
  std::optional<llcpp::fuchsia::sysmem::BufferCollectionEvents::SyncClient> events_;

  // Constraints as set by:
  //
  // v1:
  //     optional SetConstraintsAuxBuffers
  //     SetConstraints()
  //
  // v2 (TODO):
  //     SetConstraints()
  //
  // Either way, the constraints here are in v2 form.
  std::optional<TableHolder<llcpp::fuchsia::sysmem2::BufferCollectionConstraints>> constraints_;

  // Stash BufferUsage aside for benefit of GetUsageBasedRightsAttenuation() despite
  // TakeConstraints().
  std::optional<TableHolder<llcpp::fuchsia::sysmem2::wire::BufferUsage>> usage_;

  // Temporarily holds fuchsia.sysmem.BufferCollectionConstraintsAuxBuffers until SetConstraints()
  // arrives.
  std::optional<TableHolder<llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers>>
      constraints_aux_buffers_;

  // FIDL protocol enforcement.
  bool is_set_constraints_seen_ = false;
  bool is_set_constraints_aux_buffers_seen_ = false;

  // The rights attenuation mask driven by BufferCollectionToken::Duplicate()
  // rights_attenuation_mask parameter(s) as the token is duplicated,
  // potentially via multiple participants.
  //
  // TODO(fxbug.dev/50578): Finish plumbing this.
  uint32_t client_rights_attenuation_mask_ = std::numeric_limits<uint32_t>::max();

  std::list<std::pair</*async_id*/ uint64_t, WaitForBuffersAllocatedCompleter::Async>>
      pending_wait_for_buffers_allocated_;

  bool is_done_ = false;

  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::sysmem::BufferCollection>> server_binding_;

  LogicalBufferCollection::ClientInfo debug_info_;

  inspect::Node node_;
  inspect::UintProperty debug_id_property_;
  inspect::StringProperty debug_name_property_;
  inspect::ValueList properties_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_H_
