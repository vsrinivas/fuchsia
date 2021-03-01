// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_collection.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fidl-async-2/fidl_struct.h>
#include <lib/fidl-utils/bind.h>
#include <lib/sysmem-version/sysmem-version.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>

#include <atomic>

#include <ddk/trace/event.h>

#include "fuchsia/sysmem/c/fidl.h"
#include "logical_buffer_collection.h"
#include "macros.h"
#include "utils.h"

namespace sysmem_driver {

namespace {

using BufferCollectionInfo_v1 = FidlStruct<fuchsia_sysmem_BufferCollectionInfo_2,
                                           llcpp::fuchsia::sysmem::wire::BufferCollectionInfo_2>;

constexpr uint32_t kConcurrencyCap = 64;

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
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::SetDebugClientInfo>,
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::SetConstraintsAuxBuffers>,
    fidl::Binder<BufferCollection>::BindMember<&BufferCollection::GetAuxBuffers>,
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

void BufferCollection::Bind(zx::channel server_request) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      server_request.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status == ZX_OK) {
    node_.CreateUint("channel_koid", info.koid, &properties_);
  }
  FidlServer::Bind(std::move(server_request));
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

zx_status_t BufferCollection::SetConstraintsAuxBuffers(
    const fuchsia_sysmem_BufferCollectionConstraintsAuxBuffers* constraints_param) {
  ZX_DEBUG_ASSERT(constraints_param);

  // Copy data and own handles.
  static_assert(sizeof(llcpp::fuchsia::sysmem::wire::BufferCollectionConstraintsAuxBuffers) ==
                sizeof(fuchsia_sysmem_BufferCollectionConstraintsAuxBuffers));
  llcpp::fuchsia::sysmem::wire::BufferCollectionConstraintsAuxBuffers local_constraints;
  memcpy(&local_constraints, constraints_param, sizeof(local_constraints));

  if (is_set_constraints_aux_buffers_seen_) {
    FailAsync(ZX_ERR_NOT_SUPPORTED, "SetConstraintsAuxBuffers() can be called only once.");
    return ZX_ERR_NOT_SUPPORTED;
  }
  is_set_constraints_aux_buffers_seen_ = true;
  if (is_done_) {
    FailAsync(ZX_ERR_BAD_STATE,
              "BufferCollectionToken::SetConstraintsAuxBuffers() when already is_done_");
    // We're failing async - no need to try to fail sync.
    return ZX_OK;
  }
  if (is_set_constraints_seen_) {
    FailAsync(ZX_ERR_NOT_SUPPORTED,
              "SetConstraintsAuxBuffers() after SetConstraints() causes failure.");
    return ZX_ERR_NOT_SUPPORTED;
  }
  ZX_DEBUG_ASSERT(!constraints_aux_buffers_);
  constraints_aux_buffers_.emplace(std::move(local_constraints));
  // LogicalBufferCollection doesn't care about "clear aux buffers" constraints until the last
  // SetConstraints(), so done for now.
  return ZX_OK;
}

zx_status_t BufferCollection::SetConstraints(
    bool has_constraints, const fuchsia_sysmem_BufferCollectionConstraints* constraints_param) {
  TRACE_DURATION("gfx", "BufferCollection::SetConstraints", "this", this, "parent", parent_.get());
  // Regardless of has_constraints or not, we need to unconditionally take
  // ownership of any handles in constraints_param.  Not that there are
  // necessarily any handles in here currently, but to avoid being fragile re.
  // any handles potentially added later.
  ZX_DEBUG_ASSERT(constraints_param);

  // Copy data and own handles.
  static_assert(sizeof(llcpp::fuchsia::sysmem::wire::BufferCollectionConstraints) ==
                sizeof(fuchsia_sysmem_BufferCollectionConstraints));
  std::optional<llcpp::fuchsia::sysmem::wire::BufferCollectionConstraints> local_constraints;
  local_constraints.emplace();
  memcpy(&local_constraints.value(), constraints_param, sizeof(local_constraints.value()));

  if (is_set_constraints_seen_) {
    FailAsync(ZX_ERR_NOT_SUPPORTED, "2nd SetConstraints() causes failure.");
    return ZX_ERR_NOT_SUPPORTED;
  }
  is_set_constraints_seen_ = true;
  if (is_done_) {
    FailAsync(ZX_ERR_BAD_STATE, "BufferCollectionToken::SetConstraints() when already is_done_");
    // We're failing async - no need to try to fail sync.
    return ZX_OK;
  }
  if (!has_constraints) {
    // Not needed.
    local_constraints.reset();
    if (is_set_constraints_aux_buffers_seen_) {
      // No constraints are fine, just not aux buffers constraints without main constraints, because
      // I can't think of any reason why we'd need to support aux buffers constraints without main
      // constraints, so disallow at least for now.
      FailAsync(ZX_ERR_NOT_SUPPORTED, "SetConstraintsAuxBuffers() && !has_constraints");
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  ZX_DEBUG_ASSERT(!constraints_);
  // enforced above
  ZX_DEBUG_ASSERT(!constraints_aux_buffers_ || local_constraints);
  ZX_DEBUG_ASSERT(has_constraints == !!local_constraints);
  {  // scope result
    auto result = sysmem::V2CopyFromV1BufferCollectionConstraints(
        allocator_, local_constraints ? &local_constraints.value() : nullptr,
        constraints_aux_buffers_ ? &constraints_aux_buffers_.value() : nullptr);
    if (!result.is_ok()) {
      parent_->LogClientError(FROM_HERE, &debug_info_,
                              "V2CopyFromV1BufferCollectionConstraints() failed");
      return ZX_ERR_INVALID_ARGS;
    }
    ZX_DEBUG_ASSERT(!result.value().IsEmpty() || !local_constraints);
    constraints_.emplace(result.take_value());
  }  // ~result
  // No longer needed.
  constraints_aux_buffers_.reset();

  // Stash BufferUsage also, for benefit of GetUsageBasedRightsAttenuation() depsite
  // TakeConstraints().
  {  // scope buffer_usage
    llcpp::fuchsia::sysmem::wire::BufferUsage empty_buffer_usage{};
    llcpp::fuchsia::sysmem::wire::BufferUsage* source_buffer_usage = &empty_buffer_usage;
    if (local_constraints) {
      source_buffer_usage = &local_constraints.value().usage;
    }
    auto result = sysmem::V2CopyFromV1BufferUsage(allocator_, *source_buffer_usage);
    if (!result.is_ok()) {
      parent_->LogClientError(FROM_HERE, &debug_info_, "V2CopyFromV1BufferUsage failed");
      // Not expected given current impl of sysmem-version.
      return ZX_ERR_INTERNAL;
    }
    usage_.emplace(result.take_value());
  }  // ~buffer_usage

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
    // We're failing async - no need to fail sync.
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

zx_status_t BufferCollection::GetAuxBuffers(fidl_txn_t* txn_param) {
  BindingType::Txn::RecognizeTxn(txn_param);
  LogicalBufferCollection::AllocationResult allocation_result = parent()->allocation_result();
  if (allocation_result.status == ZX_OK && !allocation_result.buffer_collection_info) {
    FailAsync(ZX_ERR_BAD_STATE, "GetAuxBuffers() called before allocation complete.",
              ZX_ERR_BAD_STATE);
    // We're failing async - no need to fail sync.
    return ZX_OK;
  }
  if (allocation_result.status != ZX_OK) {
    FailAsync(ZX_ERR_BAD_STATE, "GetAuxBuffers() called after allocation failure.",
              ZX_ERR_BAD_STATE);
    // We're failing async - no need to fail sync.
    return ZX_OK;
  }
  BufferCollection::V1CBufferCollectionInfo v1_c(
      BufferCollection::V1CBufferCollectionInfo::Default);
  ZX_DEBUG_ASSERT(allocation_result.buffer_collection_info);
  auto v1_c_result = CloneAuxBuffersResultForSendingV1(*allocation_result.buffer_collection_info);
  if (!v1_c_result.is_ok()) {
    // FailAsync() already called.
    //
    // We're failing async - no need to fail sync.
    return ZX_OK;
  }
  v1_c = v1_c_result.take_value();
  // Ownership of handles in to_send are transferred to _reply().
  zx_status_t reply_status = fuchsia_sysmem_BufferCollectionGetAuxBuffers_reply(
      txn_param, allocation_result.status, v1_c.release());
  if (reply_status != ZX_OK) {
    FailAsync(reply_status,
              "fuchsia_sysmem_BufferCollectionGetAuxBuffers_reply failed - status: %d",
              reply_status);
    // We're failing async - no need to fail sync.
    return ZX_OK;
  }
  return ZX_OK;
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

zx_status_t BufferCollection::SetDebugClientInfo(const char* name_data, size_t name_size,
                                                 uint64_t id) {
  debug_info_.name = std::string(name_data, name_size);
  debug_info_.id = id;
  debug_id_property_ = node_.CreateUint("debug_id", debug_info_.id);
  debug_name_property_ = node_.CreateString("debug_name", debug_info_.name);
  return ZX_OK;
}

void BufferCollection::FailAsync(zx_status_t status, const char* format, ...) {
  va_list args;
  va_start(args, format);
  parent_->VLogClientError(FROM_HERE, &debug_info_, format, args);

  va_end(args);
  FidlServer::FailAsync(status, __FILE__, __LINE__, "");
}

fit::result<llcpp::fuchsia::sysmem2::wire::BufferCollectionInfo>
BufferCollection::CloneResultForSendingV2(
    const llcpp::fuchsia::sysmem2::wire::BufferCollectionInfo& buffer_collection_info) {
  auto clone_result = sysmem::V2CloneBufferCollectionInfo(
      allocator_, buffer_collection_info, GetClientVmoRights(), GetClientAuxVmoRights());
  if (!clone_result.is_ok()) {
    FailAsync(clone_result.error(),
              "CloneResultForSendingV1() V2CloneBufferCollectionInfo() failed - status: %d",
              clone_result.error());
    return fit::error();
  }
  auto v2_b = clone_result.take_value();
  if (!IsAnyUsage(usage_.value())) {
    // No VMO handles should be sent to the client in this case.
    if (v2_b.has_buffers()) {
      for (auto& vmo_buffer : v2_b.buffers()) {
        if (vmo_buffer.has_vmo()) {
          vmo_buffer.vmo().reset();
        }
        if (vmo_buffer.has_aux_vmo()) {
          vmo_buffer.aux_vmo().reset();
        }
      }
    }
  }
  return fit::ok(std::move(v2_b));
}

fit::result<BufferCollection::V1CBufferCollectionInfo> BufferCollection::CloneResultForSendingV1(
    const llcpp::fuchsia::sysmem2::wire::BufferCollectionInfo& buffer_collection_info) {
  auto v2_result = CloneResultForSendingV2(buffer_collection_info);
  if (!v2_result.is_ok()) {
    // FailAsync() already called.
    return fit::error();
  }
  auto v1_result = sysmem::V1MoveFromV2BufferCollectionInfo(v2_result.take_value());
  if (!v1_result.is_ok()) {
    FailAsync(ZX_ERR_INVALID_ARGS,
              "CloneResultForSendingV1() V1MoveFromV2BufferCollectionInfo() failed");
    return fit::error();
  }
  V1CBufferCollectionInfo v1_c(V1CBufferCollectionInfo::Default);
  // struct move
  v1_c.BorrowAsLlcpp() = v1_result.take_value();
  return fit::ok(std::move(v1_c));
}

fit::result<BufferCollection::V1CBufferCollectionInfo>
BufferCollection::CloneAuxBuffersResultForSendingV1(
    const llcpp::fuchsia::sysmem2::wire::BufferCollectionInfo& buffer_collection_info) {
  auto v2_result = CloneResultForSendingV2(buffer_collection_info);
  if (!v2_result.is_ok()) {
    // FailAsync() already called.
    return fit::error();
  }
  auto v1_result = sysmem::V1AuxBuffersMoveFromV2BufferCollectionInfo(v2_result.take_value());
  if (!v1_result.is_ok()) {
    FailAsync(ZX_ERR_INVALID_ARGS,
              "CloneResultForSendingV1() V1MoveFromV2BufferCollectionInfo() failed");
    return fit::error();
  }
  V1CBufferCollectionInfo v1_c(V1CBufferCollectionInfo::Default);
  // struct move
  v1_c.BorrowAsLlcpp() = v1_result.take_value();
  return fit::ok(std::move(v1_c));
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

  BufferCollection::V1CBufferCollectionInfo v1_c(
      BufferCollection::V1CBufferCollectionInfo::Default);
  if (parent()->allocation_result().status == ZX_OK) {
    ZX_DEBUG_ASSERT(parent()->allocation_result().buffer_collection_info);
    auto v1_c_result =
        CloneResultForSendingV1(*parent()->allocation_result().buffer_collection_info);
    if (!v1_c_result.is_ok()) {
      // FailAsync() already called.
      return;
    }
    v1_c = v1_c_result.take_value();
  }

  // Ownership of all handles in to_send is transferred to this function.
  fuchsia_sysmem_BufferCollectionEventsOnBuffersAllocated(
      events_.get(), parent()->allocation_result().status, v1_c.release());
}

bool BufferCollection::has_constraints() { return !!constraints_; }

const llcpp::fuchsia::sysmem2::wire::BufferCollectionConstraints& BufferCollection::constraints() {
  ZX_DEBUG_ASSERT(has_constraints());
  return constraints_.value();
}

llcpp::fuchsia::sysmem2::wire::BufferCollectionConstraints BufferCollection::TakeConstraints() {
  ZX_DEBUG_ASSERT(has_constraints());
  llcpp::fuchsia::sysmem2::wire::BufferCollectionConstraints result =
      std::move(constraints_.value());
  constraints_.reset();
  return result;
}

LogicalBufferCollection* BufferCollection::parent() { return parent_.get(); }

fbl::RefPtr<LogicalBufferCollection> BufferCollection::parent_shared() { return parent_; }

bool BufferCollection::is_done() { return is_done_; }

BufferCollection::BufferCollection(fbl::RefPtr<LogicalBufferCollection> parent)
    : FidlServer(parent->parent_device()->dispatcher(), "BufferCollection", kConcurrencyCap),
      parent_(parent),
      allocator_(parent->fidl_allocator()) {
  TRACE_DURATION("gfx", "BufferCollection::BufferCollection", "this", this, "parent",
                 parent_.get());
  ZX_DEBUG_ASSERT(parent_);
  node_ = parent_->node().CreateChild(CreateUniqueName("collection-"));
}

// This method is only meant to be called from GetClientVmoRights().
uint32_t BufferCollection::GetUsageBasedRightsAttenuation() {
  // This method won't be called for participants without any buffer data "usage".
  ZX_DEBUG_ASSERT(usage_);

  // We assume that read and map are both needed by all participants with any "usage".  Only
  // ZX_RIGHT_WRITE is controlled by usage.

  // It's not this method's job to attenuate down to kMaxClientVmoRights, so
  // let's not pretend like it is.
  uint32_t result = std::numeric_limits<uint32_t>::max();
  if (!IsWriteUsage(usage_.value())) {
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

uint32_t BufferCollection::GetClientAuxVmoRights() {
  // At least for now.
  return GetClientVmoRights();
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

    BufferCollection::V1CBufferCollectionInfo v1_c(
        BufferCollection::V1CBufferCollectionInfo::Default);
    if (allocation_result.status == ZX_OK) {
      ZX_DEBUG_ASSERT(allocation_result.buffer_collection_info);
      auto v1_c_result = CloneResultForSendingV1(*allocation_result.buffer_collection_info);
      if (!v1_c_result.is_ok()) {
        // FailAsync() already called.
        return;
      }
      v1_c = v1_c_result.take_value();
    }
    TRACE_ASYNC_END("gfx", "BufferCollection::WaitForBuffersAllocated async", async_id, "this",
                    this, "parent", parent_.get());
    // Ownership of handles in to_send are transferred to _reply().
    zx_status_t reply_status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated_reply(
        &txn->raw_txn(), allocation_result.status, v1_c.release());
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

}  // namespace sysmem_driver
