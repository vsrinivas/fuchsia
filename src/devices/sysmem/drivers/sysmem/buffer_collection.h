// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fidl-async-2/fidl_server.h>
#include <lib/fidl-async-2/fidl_struct.h>
#include <lib/fidl-async-2/simple_binding.h>
#include <lib/fidl/internal.h>

#include <list>

#include "logging.h"

namespace sysmem_driver {

class LogicalBufferCollection;
class BufferCollection
    : public FidlServer<BufferCollection,
                        SimpleBinding<BufferCollection, fuchsia_sysmem_BufferCollection_ops_t,
                                      fuchsia_sysmem_BufferCollection_dispatch>,
                        vLog> {
 public:
  using Constraints = FidlStruct<fuchsia_sysmem_BufferCollectionConstraints,
                                 llcpp::fuchsia::sysmem::BufferCollectionConstraints>;
  using ConstraintsAuxBuffers =
      FidlStruct<fuchsia_sysmem_BufferCollectionConstraintsAuxBuffers,
                 llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers>;

  ~BufferCollection();

  //
  // BufferCollection interface methods
  //

  zx_status_t SetEventSink(zx_handle_t buffer_collection_events_client);
  zx_status_t Sync(fidl_txn_t* txn);
  zx_status_t SetConstraints(bool has_constraints,
                             const fuchsia_sysmem_BufferCollectionConstraints* constraints);
  zx_status_t WaitForBuffersAllocated(fidl_txn_t* txn);
  zx_status_t CheckBuffersAllocated(fidl_txn_t* txn);
  zx_status_t CloseSingleBuffer(uint64_t buffer_index);
  zx_status_t AllocateSingleBuffer(uint64_t buffer_index);
  zx_status_t WaitForSingleBufferAllocated(uint64_t buffer_index, fidl_txn_t* txn);
  zx_status_t CheckSingleBufferAllocated(uint64_t buffer_index);
  zx_status_t Close();
  zx_status_t SetName(uint32_t priority, const char* name_data, size_t name_size);
  zx_status_t SetDebugClientInfo(const char* name_data, size_t name_size, uint64_t id);
  zx_status_t SetConstraintsAuxBuffers(
      const fuchsia_sysmem_BufferCollectionConstraintsAuxBuffers* constraints_aux_buffers);
  zx_status_t GetAuxBuffers(fidl_txn_t* txn_param);

  //
  // LogicalBufferCollection uses these:
  //

  using BufferCollectionInfo = FidlStruct<fuchsia_sysmem_BufferCollectionInfo_2,
                                          llcpp::fuchsia::sysmem::BufferCollectionInfo_2>;

  void OnBuffersAllocated();

  bool is_set_constraints_seen();

  // is_set_constraints_seen() must be true to call this.
  //
  // this can only be called if TakeConstraints() hasn't been called yet.
  const fuchsia_sysmem_BufferCollectionConstraints* constraints();

  // is_set_constraints_seen() must be true to call this.
  //
  // this can only be called once
  Constraints TakeConstraints();

  LogicalBufferCollection* parent();

  fbl::RefPtr<LogicalBufferCollection> parent_shared();

  bool is_done();

  const std::string& debug_name() const { return debug_name_; }
  uint64_t debug_id() const { return debug_id_; }

 private:
  friend class FidlServer;

  BufferCollection(fbl::RefPtr<LogicalBufferCollection> parent);

  // The rights attenuation mask driven by usage, so that read-only usage
  // doesn't get write, etc.
  uint32_t GetUsageBasedRightsAttenuation();

  uint32_t GetClientVmoRights();
  void MaybeCompleteWaitForBuffersAllocated();
  BufferCollectionInfo BufferCollectionInfoClone(
      const fuchsia_sysmem_BufferCollectionInfo_2* buffer_collection_info);

  static const fuchsia_sysmem_BufferCollection_ops_t kOps;

  fbl::RefPtr<LogicalBufferCollection> parent_;

  // Client end of a BufferCollectionEvents channel, for the local server to
  // send events to the remote client.  All of the messages in this interface
  // are one-way with no response, so sending an event doesn't block the
  // server thread.
  //
  // This may remain non-set if SetEventSink() is never used by a client.  A
  // client may send SetEventSink() up to once.
  //
  // For example:
  // fuchsia_sysmem_BufferCollectionEventsOnBuffersAllocated(
  //     events_.get(), ...);
  zx::channel events_;

  bool is_set_constraints_seen_ = false;

  Constraints constraints_{Constraints::Null};

  // The rights attenuation mask driven by BufferCollectionToken::Duplicate()
  // rights_attenuation_mask parameter(s) as the token is duplicated,
  // potentially via multiple participants.
  uint32_t client_rights_attenuation_mask_ = std::numeric_limits<uint32_t>::max();

  std::list<std::pair</*async_id*/ uint64_t, std::unique_ptr<BindingType::Txn>>>
      pending_wait_for_buffers_allocated_;

  bool is_done_ = false;

  std::string debug_name_;
  uint64_t debug_id_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_H_
