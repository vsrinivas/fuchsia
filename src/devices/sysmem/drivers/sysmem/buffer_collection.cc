// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_collection.h"

#include <lib/fidl-utils/bind.h>
#include <zircon/compiler.h>

#include <atomic>

#include <ddk/trace/event.h>

#include "fuchsia/sysmem/c/fidl.h"
#include "logical_buffer_collection.h"

namespace sysmem_driver {

namespace {

namespace {

constexpr uint32_t kConcurrencyCap = 64;

}  // namespace

// For max client vmo rights, we specify the RIGHT bits individually to avoid
// picking up any newly-added rights unintentionally.  This is based on
// ZX_DEFAULT_VMO_RIGHTS, but with a few rights removed.
const uint32_t kMaxClientVmoRights =
    // ZX_RIGHTS_BASIC, except ZX_RIGHT_INSPECT (at least for now).
    ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_WAIT |
    // ZX_RIGHTS_IO:
    ZX_RIGHT_READ | ZX_RIGHT_WRITE |
    // ZX_RIGHTS_PROPERTY allows a participant to set ZX_PROP_NAME for easier
    // memory metrics.  Nothing prevents participants from figting over the
    // name, though the kernel should make each set/get atomic with respect to
    // other set/get.  This relies on ZX_RIGHTS_PROPERTY not implying anything
    // that could be used as an attack vector between processes sharing a VMO.
    ZX_RIGHTS_PROPERTY |
    // We intentionally omit ZX_RIGHT_EXECUTE (indefinitely) and ZX_RIGHT_SIGNAL
    // (at least for now).
    //
    // Remaining bits of ZX_DEFAULT_VMO_RIGHTS (as of this writing):
    ZX_RIGHT_MAP;
}  // namespace

const fuchsia_sysmem_BufferCollection_ops_t BufferCollection::kOps = {
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::SetEventSink>,
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::Sync>,
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::SetConstraints>,
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::WaitForBuffersAllocated>,
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::CheckBuffersAllocated>,
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::CloseSingleBuffer>,
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::AllocateSingleBuffer>,
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::WaitForSingleBufferAllocated>,
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::CheckSingleBufferAllocated>,
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::Close>,
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::SetName>,
};

BufferCollection::~BufferCollection() {
  TRACE_DURATION("gfx", "BufferCollection::~BufferCollection", "this", this, "parent",
                 parent_.get());
  // Close() the SimpleBinding<> before deleting the list of pending Txn(s),
  // so that ~Txn doesn't complain about being deleted without being
  // completed.
  //
  // Don't run the error handler; if any error handler remains it'll just get
  // deleted here.
  (void)binding_.Close();
}

zx_status_t BufferCollection::SetEventSink(zx_handle_t buffer_collection_events_client_param) {
  zx::channel buffer_collection_events_client(buffer_collection_events_client_param);
  if (is_done_) {
    FailAsync(ZX_ERR_BAD_STATE, "BufferCollectionToken::SetEventSink() when already is_done_");
    // We're failing async - no need to try to fail sync.
    return ZX_OK;
  }
  if (!buffer_collection_events_client) {
    zx_status_t status = ZX_ERR_INVALID_ARGS;
    FailAsync(status,
              "BufferCollection::SetEventSink() must be called "
              "with a non-zero handle.");
    return status;
  }
  if (is_set_constraints_seen_) {
    zx_status_t status = ZX_ERR_INVALID_ARGS;
    // It's not required to use SetEventSink(), but if it's used, it must be
    // before SetConstraints().
    FailAsync(status,
              "BufferCollection::SetEventSink() (if any) must be "
              "before SetConstraints().");
    return status;
  }
  if (events_) {
    zx_status_t status = ZX_ERR_INVALID_ARGS;
    FailAsync(status, "BufferCollection::SetEventSink() may only be called at most once.");
    return status;
  }
  events_ = std::move(buffer_collection_events_client);
  // We don't create BufferCollection until after processing all inbound
  // messages that were queued toward the server on the token channel of the
  // token that was used to create this BufferCollection, so we can send this
  // event now, as all the Duplicate() inbound from that token are done being
  // processed before here.
  fuchsia_sysmem_BufferCollectionEventsOnDuplicatedTokensKnownByServer(events_.get());
  return ZX_OK;
}

zx_status_t BufferCollection::Sync(fidl_txn_t* txn) {
  BindingType::Txn::RecognizeTxn(txn);
  return fuchsia_sysmem_BufferCollectionSync_reply(txn);
}

zx_status_t BufferCollection::SetConstraints(
    bool has_constraints, const fuchsia_sysmem_BufferCollectionConstraints* constraints_param) {
  TRACE_DURATION("gfx", "BufferCollection::SetConstraints", "this", this, "parent", parent_.get());
  // Regardless of has_constraints or not, we need to unconditionally take
  // ownership of any handles in constraints_param.  Not that there are
  // necessarily any handles in here currently, but to avoid being fragile re.
  // any handles potentially added later.
  constraints_.reset(constraints_param);
  if (is_done_) {
    FailAsync(ZX_ERR_BAD_STATE, "BufferCollectionToken::SetConstraints() when already is_done_");
    // We're failing async - no need to try to fail sync.
    return ZX_OK;
  }
  if (is_set_constraints_seen_) {
    FailAsync(ZX_ERR_NOT_SUPPORTED, "For now, 2nd SetConstraints() causes failure.");
    return ZX_ERR_NOT_SUPPORTED;
  }
  is_set_constraints_seen_ = true;

  if (!has_constraints) {
    // We don't need any of the handles/info in constraints_param, so close
    // the handles sooner rather than later.  This also lets us track
    // whether we have null constraints without a separate bool.
    constraints_.reset(nullptr);
  }

  // LogicalBufferCollection will ask for constraints when it needs them,
  // possibly during this call if this is the last participant to report
  // having initial constraints.
  //
  // The LogicalBufferCollection cares if this BufferCollection view has null
  // constraints, but only later when it asks for the specific constraints.
  parent()->OnSetConstraints();
  // |this| may be gone at this point, if the allocation failed.  Regardless,
  // SetConstraints() worked, so ZX_OK.
  return ZX_OK;
}

zx_status_t BufferCollection::WaitForBuffersAllocated(fidl_txn_t* txn_param) {
  TRACE_DURATION("gfx", "BufferCollection::WaitForBuffersAllocated", "this", this, "parent",
                 parent_.get());
  BindingType::Txn::RecognizeTxn(txn_param);
  if (is_done_) {
    FailAsync(ZX_ERR_BAD_STATE,
              "BufferCollectionToken::WaitForBuffersAllocated() when already is_done_");
    // We're failing async - no need to try to fail sync.
    return ZX_OK;
  }
  // In general we're handling this async, so take ownership of the txn.
  std::unique_ptr<BindingType::Txn> txn = BindingType::Txn::TakeTxn(txn_param);
  trace_async_id_t current_event_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("gfx", "BufferCollection::WaitForBuffersAllocated async", current_event_id,
                    "this", this, "parent", parent_.get());
  pending_wait_for_buffers_allocated_.emplace_back(
      std::make_pair(current_event_id, std::move(txn)));
  // The allocation is a one-shot (once true, remains true) and may already be
  // done, in which case this immediately completes txn.
  MaybeCompleteWaitForBuffersAllocated();
  return ZX_OK;
}

zx_status_t BufferCollection::CheckBuffersAllocated(fidl_txn_t* txn) {
  BindingType::Txn::RecognizeTxn(txn);
  if (is_done_) {
    FailAsync(ZX_ERR_BAD_STATE,
              "BufferCollectionToken::CheckBuffersAllocated() when "
              "already is_done_");
    // We're failing async - no need to try to fail sync.
    return ZX_OK;
  }
  LogicalBufferCollection::AllocationResult allocation_result = parent()->allocation_result();
  if (allocation_result.status == ZX_OK && !allocation_result.buffer_collection_info) {
    return fuchsia_sysmem_BufferCollectionCheckBuffersAllocated_reply(txn, ZX_ERR_UNAVAILABLE);
  }
  // Buffer collection has either been allocated or failed.
  return fuchsia_sysmem_BufferCollectionCheckBuffersAllocated_reply(txn, allocation_result.status);
}

zx_status_t BufferCollection::CloseSingleBuffer(uint64_t buffer_index) {
  if (is_done_) {
    FailAsync(ZX_ERR_BAD_STATE, "BufferCollectionToken::CloseSingleBuffer() when already is_done_");
    // We're failing async - no need to try to fail sync.
    return ZX_OK;
  }
  // FailAsync() instead of returning a failure, mainly because FailAsync()
  // prints a message that's more obvious than the generic _dispatch() failure
  // would.
  FailAsync(ZX_ERR_NOT_SUPPORTED, "CloseSingleBuffer() not yet implemented");
  return ZX_OK;
}

zx_status_t BufferCollection::AllocateSingleBuffer(uint64_t buffer_index) {
  if (is_done_) {
    FailAsync(ZX_ERR_BAD_STATE,
              "BufferCollectionToken::AllocateSingleBuffer() when already "
              "is_done_");
    // We're failing async - no need to try to fail sync.
    return ZX_OK;
  }
  FailAsync(ZX_ERR_NOT_SUPPORTED, "AllocateSingleBuffer() not yet implemented");
  return ZX_OK;
}

zx_status_t BufferCollection::WaitForSingleBufferAllocated(uint64_t buffer_index, fidl_txn_t* txn) {
  BindingType::Txn::RecognizeTxn(txn);
  if (is_done_) {
    FailAsync(ZX_ERR_BAD_STATE,
              "BufferCollectionToken::WaitForSingleBufferAllocated() when "
              "already is_done_");
    // We're failing async - no need to try to fail sync.
    return ZX_OK;
  }
  FailAsync(ZX_ERR_NOT_SUPPORTED, "WaitForSingleBufferAllocated() not yet implemented");
  return ZX_OK;
}

zx_status_t BufferCollection::CheckSingleBufferAllocated(uint64_t buffer_index) {
  if (is_done_) {
    FailAsync(ZX_ERR_BAD_STATE,
              "BufferCollectionToken::CheckSingleBufferAllocated() when "
              "already is_done_");
    // We're failing async - no need to try to fail sync.
    return ZX_OK;
  }
  FailAsync(ZX_ERR_NOT_SUPPORTED, "CheckSingleBufferAllocated() not yet implemented");
  return ZX_OK;
}

zx_status_t BufferCollection::Close() {
  if (is_done_) {
    FailAsync(ZX_ERR_BAD_STATE, "BufferCollection::Close() when already closed.");
    return ZX_OK;
  }
  // We still want to enforce that the client doesn't send any other messages
  // between Close() and closing the channel, so we just set is_done_ here and
  // do a FailAsync() if is_done_ is seen to be set while handling any other
  // message.
  is_done_ = true;
  return ZX_OK;
}

zx_status_t BufferCollection::SetName(uint32_t priority, const char* name_data, size_t name_size) {
  parent_->SetName(priority, std::string(name_data, name_size));
  return ZX_OK;
}

void BufferCollection::OnBuffersAllocated() {
  // Any that are pending are completed by this call or something called
  // FailAsync().  It's fine for this method to ignore the fact that
  // FailAsync() may have already been called.  That's essentially the main
  // reason we have FailAsync() instead of Fail().
  MaybeCompleteWaitForBuffersAllocated();

  if (!events_) {
    return;
  }

  LogicalBufferCollection::AllocationResult allocation_result = parent()->allocation_result();
  ZX_DEBUG_ASSERT(allocation_result.buffer_collection_info || allocation_result.status != ZX_OK);

  BufferCollectionInfo to_send(BufferCollectionInfo::Default);
  if (allocation_result.buffer_collection_info) {
    to_send = BufferCollectionInfoClone(allocation_result.buffer_collection_info);
    if (!to_send) {
      // FailAsync() already called by Clone()
      return;
    }
  }

  // Ownership of all handles in to_send is transferred to this function.
  fuchsia_sysmem_BufferCollectionEventsOnBuffersAllocated(events_.get(), allocation_result.status,
                                                          to_send.release());
}

bool BufferCollection::is_set_constraints_seen() { return is_set_constraints_seen_; }

const fuchsia_sysmem_BufferCollectionConstraints* BufferCollection::constraints() {
  ZX_DEBUG_ASSERT(is_set_constraints_seen());
  if (!constraints_) {
    return nullptr;
  }
  return constraints_.get();
}

BufferCollection::Constraints BufferCollection::TakeConstraints() {
  ZX_DEBUG_ASSERT(is_set_constraints_seen());
  return std::move(constraints_);
}

LogicalBufferCollection* BufferCollection::parent() { return parent_.get(); }

fbl::RefPtr<LogicalBufferCollection> BufferCollection::parent_shared() { return parent_; }

bool BufferCollection::is_done() { return is_done_; }

BufferCollection::BufferCollection(fbl::RefPtr<LogicalBufferCollection> parent)
    : FidlServer(parent->parent_device()->dispatcher(), "BufferCollection", kConcurrencyCap),
      parent_(parent) {
  TRACE_DURATION("gfx", "BufferCollection::BufferCollection", "this", this, "parent",
                 parent_.get());
  ZX_DEBUG_ASSERT(parent_);
}

// This method is only meant to be called from GetClientVmoRights().
uint32_t BufferCollection::GetUsageBasedRightsAttenuation() {
  ZX_DEBUG_ASSERT(is_set_constraints_seen_);
  // If there are no constraints from this participant, it means this
  // participant doesn't intend to do any "usage" at all aside from referring
  // to buffers by their index in communication with other participants, so
  // this participant doesn't need any VMO handles at all.  So this method
  // never should be called if that's the case.
  ZX_DEBUG_ASSERT(constraints_);

  // We assume that read and map are both needed by all participants.  Only
  // ZX_RIGHT_WRITE is controlled by usage.

  bool is_write_needed = false;

  const fuchsia_sysmem_BufferUsage* usage = &constraints_->usage;

  const uint32_t kCpuWriteBits = fuchsia_sysmem_cpuUsageWriteOften | fuchsia_sysmem_cpuUsageWrite;
  // This list may not be complete.
  const uint32_t kVulkanWriteBits =
      fuchsia_sysmem_vulkanUsageTransferDst | fuchsia_sysmem_vulkanUsageStorage;
  // Display usages don't include any writing.
  const uint32_t kDisplayWriteBits = 0;
  const uint32_t kVideoWriteBits =
      fuchsia_sysmem_videoUsageHwDecoder | fuchsia_sysmem_videoUsageHwDecoderInternal |
      fuchsia_sysmem_videoUsageDecryptorOutput | fuchsia_sysmem_videoUsageHwEncoder;

  is_write_needed = (usage->cpu & kCpuWriteBits) || (usage->vulkan & kVulkanWriteBits) ||
                    (usage->display & kDisplayWriteBits) || (usage->video & kVideoWriteBits);

  // It's not this method's job to attenuate down to kMaxClientVmoRights, so
  // let's not pretend like it is.
  uint32_t result = std::numeric_limits<uint32_t>::max();
  if (!is_write_needed) {
    result &= ~ZX_RIGHT_WRITE;
  }

  return result;
}

uint32_t BufferCollection::GetClientVmoRights() {
  return
      // max possible rights for a client to have
      kMaxClientVmoRights &
      // attenuate write if client doesn't need write
      GetUsageBasedRightsAttenuation() &
      // attenuate according to BufferCollectionToken.Duplicate() rights
      // parameter so that initiator and participant can distribute the token
      // and remove any unnecessary/unintended rights along the way.
      client_rights_attenuation_mask_;
}

void BufferCollection::MaybeCompleteWaitForBuffersAllocated() {
  LogicalBufferCollection::AllocationResult allocation_result = parent()->allocation_result();
  if (allocation_result.status == ZX_OK && !allocation_result.buffer_collection_info) {
    // Everything is ok so far, but allocation isn't done yet.
    return;
  }
  while (!pending_wait_for_buffers_allocated_.empty()) {
    auto [async_id, txn] = std::move(pending_wait_for_buffers_allocated_.front());
    pending_wait_for_buffers_allocated_.pop_front();
    BufferCollectionInfo to_send(BufferCollectionInfo::Default);
    ZX_DEBUG_ASSERT(allocation_result.buffer_collection_info || allocation_result.status != ZX_OK);
    if (allocation_result.buffer_collection_info) {
      to_send = BufferCollectionInfoClone(allocation_result.buffer_collection_info);
      if (!to_send) {
        // FailAsync() has already been run by the Clone()
        return;
      }
    }
    TRACE_ASYNC_END("gfx", "BufferCollection::WaitForBuffersAllocated async", async_id, "this",
                    this, "parent", parent_.get());
    // Ownership of handles in to_send are transferred to _reply().
    zx_status_t reply_status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated_reply(
        &txn->raw_txn(), allocation_result.status, to_send.release());
    if (reply_status != ZX_OK) {
      FailAsync(reply_status,
                "fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated_"
                "reply failed - status: %d",
                reply_status);
      return;
    }
    // ~txn
  }
}

BufferCollection::BufferCollectionInfo BufferCollection::BufferCollectionInfoClone(
    const fuchsia_sysmem_BufferCollectionInfo_2* buffer_collection_info) {
  // We must not set handles in here that we don't own.  This means any
  // sending of handles involves duplicating handles first, not just assigning
  // a handle value into the |to_send| struct.
  BufferCollectionInfo clone(BufferCollectionInfo::Default);

  // We don't close the handles in temp on returning from this method.  Those
  // handles don't belong to this method call, they belong to the caller.
  //
  // struct copy
  fuchsia_sysmem_BufferCollectionInfo_2 temp = *buffer_collection_info;

  // Zero the handles in temp so we can copy the rest of temp into to_send
  // without putting any handles in to_send yet (as we haven't duplicated any
  // handles yet).  There isn't any fidl_zero_handles() and
  // fidl::internal::BufferWalker isn't meant to be used as a lib, so do this
  // manually for now.
  for (auto& vmo_buffer : temp.buffers) {
    if (vmo_buffer.vmo == ZX_HANDLE_INVALID) {
      // All the rest are already 0, so we can stop here.
      break;
    }
    vmo_buffer.vmo = ZX_HANDLE_INVALID;
  }

  // Now we can copy the data of |temp| into to_send without owning handles we
  // haven't duplicated yet.
  //
  // struct copy
  *clone.get() = temp;

  if (!constraints_) {
    // No VMO handles should be copied in this case.
    //
    // TODO(dustingreen): Usage "none" should also do this.
    return clone;
  }

  // We duplicate the handles in buffer_collection_info into to_send, so that
  // if we fail mid-way we'll still remember to close the non-sent duplicates.
  for (uint32_t i = 0; i < countof(buffer_collection_info->buffers); ++i) {
    if (buffer_collection_info->buffers[i].vmo == ZX_HANDLE_INVALID) {
      // The rest are ZX_HANDLE_INVALID also.
      break;
    }
    zx::vmo handle_to_send;
    zx_status_t duplicate_status =
        zx_handle_duplicate(buffer_collection_info->buffers[i].vmo, GetClientVmoRights(),
                            handle_to_send.reset_and_get_address());
    if (duplicate_status != ZX_OK) {
      // We fail the BufferCollection view with FailAsync(), which the
      // LogicalBufferCollection will likely handle by failing the whole
      // LogicalBufferCollection.  However, the LogicalBufferCollection is
      // still permitted to delete |this| before FailAsync() is fully done
      // - that possibility is handled cleanly by FailAsync() code.
      //
      // The important thing here is that by failing async, this
      // particular stack doesn't have to check whether |this| still
      // exists, or whether LogicalBufferCollection still exists.
      FailAsync(duplicate_status,
                "BufferCollection::OnBuffersAllocated() "
                "zx::vmo::duplicate() failed - status: %d",
                duplicate_status);
      return BufferCollectionInfo(BufferCollectionInfo::Null);
    }
    // Transfer ownership of owned handle directly from zx::vmo to
    // FidlStruct, in a way that can't fail.
    clone->buffers[i].vmo = handle_to_send.release();
  }

  return clone;
}

}  // namespace sysmem_driver
