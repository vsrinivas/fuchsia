// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_collection.h"

#include <lib/sysmem-version/sysmem-version.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>

#include <atomic>

#include <ddk/trace/event.h>

#include "buffer_collection_token.h"
#include "fbl/ref_ptr.h"
#include "fuchsia/sysmem/c/fidl.h"
#include "logical_buffer_collection.h"
#include "macros.h"
#include "node_properties.h"
#include "utils.h"

namespace sysmem_driver {

namespace {

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

// static
BufferCollection& BufferCollection::EmplaceInTree(
    fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection, BufferCollectionToken* token) {
  // The token is passed in as a pointer because this method deletes token, but the caller must
  // provide non-nullptr token.
  ZX_DEBUG_ASSERT(token);
  // This conversion from unique_ptr<> to RefPtr<> will go away once we move BufferCollection to
  // LLCPP FIDL server binding.
  fbl::RefPtr<Node> node(fbl::AdoptRef(new BufferCollection(logical_buffer_collection, *token)));
  BufferCollection* buffer_collection_ptr = static_cast<BufferCollection*>(node.get());
  // This also deletes token.
  token->node_properties().SetNode(std::move(node));
  return *buffer_collection_ptr;
}

BufferCollection::~BufferCollection() {
  TRACE_DURATION("gfx", "BufferCollection::~BufferCollection", "this", this,
                 "logical_buffer_collection", &logical_buffer_collection());
}

void BufferCollection::CloseChannel(zx_status_t epitaph) {
  // This essentially converts the OnUnboundFn semantic of getting called regardless of channel-fail
  // vs. server-driven-fail into the more typical semantic where error_handler_ only gets called
  // on channel-fail but not on server-driven-fail.
  error_handler_ = {};
  if (server_binding_)
    server_binding_->Close(epitaph);
  server_binding_ = {};
}

void BufferCollection::Bind(zx::channel channel) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      channel.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status == ZX_OK) {
    inspect_node_.CreateUint("channel_koid", info.koid, &properties_);
  }
  auto res = fidl::BindServer(
      logical_buffer_collection().parent_device()->dispatcher(), std::move(channel), this,
      fidl::OnUnboundFn<BufferCollection>(
          [this, this_ref = fbl::RefPtr<BufferCollection>(this)](
              BufferCollection* collection, fidl::UnbindInfo info,
              fidl::ServerEnd<fuchsia_sysmem::BufferCollection> channel) {
            // We need to keep a refptr to this class, since the unbind happens asynchronously and
            // can run after the parent closes a handle to this class.
            if (error_handler_) {
              zx_status_t status = info.status;
              if (async_failure_result_ && info.reason == fidl::UnbindInfo::kClose) {
                // On kClose the error is always ZX_OK, so report the real error to
                // LogicalBufferCollection if the close was caused by FailAsync or FailSync.
                status = *async_failure_result_;
              }
              error_handler_(status);
            }
            // *this can be destroyed by ~this_ref here
          }));
  if (res.is_error()) {
    return;
  }
  server_binding_ = res.take_value();
  return;
}

void BufferCollection::SetEventSink(
    fidl::ClientEnd<fuchsia_sysmem::BufferCollectionEvents> buffer_collection_events_client,
    SetEventSinkCompleter::Sync& completer) {
  table_set_.MitigateChurn();
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::SetEventSink() when already is_done_");
    return;
  }
  if (!buffer_collection_events_client) {
    FailSync(FROM_HERE, completer, ZX_ERR_INVALID_ARGS,
             "BufferCollection::SetEventSink() must be called "
             "with a non-zero handle.");
    return;
  }
  if (is_set_constraints_seen_) {
    // It's not required to use SetEventSink(), but if it's used, it must be
    // before SetConstraints().
    FailSync(FROM_HERE, completer, ZX_ERR_INVALID_ARGS,
             "BufferCollection::SetEventSink() (if any) must be "
             "before SetConstraints().");
    return;
  }
  if (events_) {
    FailSync(FROM_HERE, completer, ZX_ERR_INVALID_ARGS,
             "BufferCollection::SetEventSink() may only be called at most once.");
    return;
  }

  events_.emplace(std::move(buffer_collection_events_client));
  // We don't create BufferCollection until after processing all inbound
  // messages that were queued toward the server on the token channel of the
  // token that was used to create this BufferCollection, so we can send this
  // event now, as all the Duplicate() inbound from that token are done being
  // processed before here.
  events_->OnDuplicatedTokensKnownByServer();
}

void BufferCollection::Sync(SyncCompleter::Sync& completer) {
  // This isn't real churn.  As a temporary measure, we need to count churn despite there not being
  // any, since more real churn is coming soon, and we need to test the mitigation of that churn.
  //
  // TODO(fxbug.dev/33670): Remove this fake churn count once we're creating real churn from tests
  // using new messages.  Also consider making TableSet::CountChurn() private.
  table_set_.CountChurn();

  table_set_.MitigateChurn();

  completer.Reply();
}

void BufferCollection::SetConstraintsAuxBuffers(
    fuchsia_sysmem::wire::BufferCollectionConstraintsAuxBuffers local_constraints,
    SetConstraintsAuxBuffersCompleter::Sync& completer) {
  table_set_.MitigateChurn();
  if (is_set_constraints_aux_buffers_seen_) {
    FailSync(FROM_HERE, completer, ZX_ERR_NOT_SUPPORTED,
             "SetConstraintsAuxBuffers() can be called only once.");
    return;
  }
  is_set_constraints_aux_buffers_seen_ = true;
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::SetConstraintsAuxBuffers() when already is_done_");
    return;
  }
  if (is_set_constraints_seen_) {
    FailSync(FROM_HERE, completer, ZX_ERR_NOT_SUPPORTED,
             "SetConstraintsAuxBuffers() after SetConstraints() causes failure.");
    return;
  }
  ZX_DEBUG_ASSERT(!constraints_aux_buffers_);
  constraints_aux_buffers_.emplace(table_set_, std::move(local_constraints));
  // LogicalBufferCollection doesn't care about "clear aux buffers" constraints until the last
  // SetConstraints(), so done for now.
}

void BufferCollection::SetConstraints(bool has_constraints_param,
                                      fuchsia_sysmem::wire::BufferCollectionConstraints constraints,
                                      SetConstraintsCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "BufferCollection::SetConstraints", "this", this,
                 "logical_buffer_collection", &logical_buffer_collection());
  table_set_.MitigateChurn();
  std::optional<fuchsia_sysmem::wire::BufferCollectionConstraints> local_constraints(
      std::move(constraints));
  if (is_set_constraints_seen_) {
    FailSync(FROM_HERE, completer, ZX_ERR_NOT_SUPPORTED, "2nd SetConstraints() causes failure.");
    return;
  }
  is_set_constraints_seen_ = true;
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::SetConstraints() when already is_done_");
    // We're failing async - no need to try to fail sync.
    return;
  }
  if (!has_constraints_param) {
    // Not needed.
    local_constraints.reset();
    if (is_set_constraints_aux_buffers_seen_) {
      // No constraints are fine, just not aux buffers constraints without main constraints, because
      // I can't think of any reason why we'd need to support aux buffers constraints without main
      // constraints, so disallow at least for now.
      FailSync(FROM_HERE, completer, ZX_ERR_NOT_SUPPORTED,
               "SetConstraintsAuxBuffers() && !has_constraints");
      return;
    }
  }

  ZX_DEBUG_ASSERT(!has_constraints());
  // enforced above
  ZX_DEBUG_ASSERT(!constraints_aux_buffers_ || local_constraints);
  ZX_DEBUG_ASSERT(has_constraints_param == !!local_constraints);
  {  // scope result
    auto result = sysmem::V2CopyFromV1BufferCollectionConstraints(
        table_set_.allocator(), local_constraints ? &local_constraints.value() : nullptr,
        constraints_aux_buffers_ ? &(**constraints_aux_buffers_) : nullptr);
    if (!result.is_ok()) {
      FailSync(FROM_HERE, completer, ZX_ERR_INVALID_ARGS,
               "V2CopyFromV1BufferCollectionConstraints() failed");
      return;
    }
    ZX_DEBUG_ASSERT(!result.value().IsEmpty() || !local_constraints);
    node_properties().SetBufferCollectionConstraints(TableHolder(table_set_, result.take_value()));
  }  // ~result

  // No longer needed.
  constraints_aux_buffers_.reset();

  // LogicalBufferCollection will ask for constraints when it needs them,
  // possibly during this call if this is the last participant to report
  // having initial constraints.
  //
  // The LogicalBufferCollection cares if this BufferCollection view has null
  // constraints, but only later when it asks for the specific constraints.
  logical_buffer_collection().OnSetConstraints();
  // |this| may be gone at this point, if the allocation failed.  Regardless,
  // SetConstraints() worked, so ZX_OK.
}

void BufferCollection::WaitForBuffersAllocated(WaitForBuffersAllocatedCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "BufferCollection::WaitForBuffersAllocated", "this", this,
                 "logical_buffer_collection", &logical_buffer_collection());
  table_set_.MitigateChurn();
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::WaitForBuffersAllocated() when already is_done_");
    return;
  }
  trace_async_id_t current_event_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("gfx", "BufferCollection::WaitForBuffersAllocated async", current_event_id,
                    "this", this, "logical_buffer_collection", &logical_buffer_collection());
  pending_wait_for_buffers_allocated_.emplace_back(
      std::make_pair(current_event_id, completer.ToAsync()));
  // The allocation is a one-shot (once true, remains true) and may already be
  // done, in which case this immediately completes txn.
  MaybeCompleteWaitForBuffersAllocated();
}

void BufferCollection::CheckBuffersAllocated(CheckBuffersAllocatedCompleter::Sync& completer) {
  table_set_.MitigateChurn();
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::CheckBuffersAllocated() when "
             "already is_done_");
    // We're failing async - no need to try to fail sync.
    return;
  }
  if (!logical_allocation_result_) {
    completer.Reply(ZX_ERR_UNAVAILABLE);
    return;
  }
  // Buffer collection has either been allocated or failed.
  completer.Reply(logical_allocation_result_->status);
}

void BufferCollection::GetAuxBuffers(GetAuxBuffersCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "BufferCollection::GetAuxBuffers", "this", this,
                 "logical_buffer_collection", &logical_buffer_collection());
  table_set_.MitigateChurn();
  if (!logical_allocation_result_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "GetAuxBuffers() called before allocation complete.");
    return;
  }
  if (logical_allocation_result_->status != ZX_OK) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "GetAuxBuffers() called after allocation failure.");
    // We're failing async - no need to fail sync.
    return;
  }
  BufferCollection::V1CBufferCollectionInfo v1_c(
      BufferCollection::V1CBufferCollectionInfo::Default);
  ZX_DEBUG_ASSERT(logical_allocation_result_->buffer_collection_info);
  auto v1_result =
      CloneAuxBuffersResultForSendingV1(*logical_allocation_result_->buffer_collection_info);
  if (!v1_result.is_ok()) {
    // Close to avoid assert.
    FailSync(FROM_HERE, completer, ZX_ERR_INTERNAL, "CloneAuxBuffersResultForSendingV1() failed.");
    return;
  }
  auto v1 = v1_result.take_value();
  completer.Reply(logical_allocation_result_->status, std::move(v1));
}

void BufferCollection::AttachToken(
    uint32_t rights_attenuation_mask,
    fidl::ServerEnd<fuchsia_sysmem::BufferCollectionToken> token_request,
    AttachTokenCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "BufferCollection::AttachToken", "this", this, "logical_buffer_collection",
                 &logical_buffer_collection());
  table_set_.MitigateChurn();
  if (is_done_) {
    // Probably a Close() followed by AttachToken(), which is not permitted and causes the whole
    // LogicalBufferCollection to fail.
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::AttachToken() attempted when is_done_");
    return;
  }

  NodeProperties* new_node_properties = node_properties().NewChild(&logical_buffer_collection());

  // These defaults can be overriden by Allocator.SetDebugClientInfo() called before
  // BindSharedCollection().
  if (!new_node_properties->client_debug_info().name.empty()) {
    // This can be overriden via Allocator::SetDebugClientInfo(), but in case that's not called,
    // this default may help clarify where the new token / BufferCollection channel came from.
    new_node_properties->client_debug_info().name =
        new_node_properties->client_debug_info().name + " then AttachToken()";
  } else {
    new_node_properties->client_debug_info().name = "from AttachToken()";
  }
  ZX_DEBUG_ASSERT(new_node_properties->client_debug_info().id == 0);

  if (rights_attenuation_mask != ZX_RIGHT_SAME_RIGHTS) {
    new_node_properties->rights_attenuation_mask() &= rights_attenuation_mask;
  }

  // All AttachToken() tokesn are ErrorPropagationMode::kDoNotPropagate from the start.
  new_node_properties->error_propagation_mode() = ErrorPropagationMode::kDoNotPropagate;
  logical_buffer_collection().CreateBufferCollectionToken(
      shared_logical_buffer_collection(), new_node_properties, std::move(token_request));
}

void BufferCollection::CloseSingleBuffer(uint64_t buffer_index,
                                         CloseSingleBufferCompleter::Sync& completer) {
  table_set_.MitigateChurn();
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::CloseSingleBuffer() when already is_done_");
    return;
  }
  // FailAsync() instead of returning a failure, mainly because FailAsync()
  // prints a message that's more obvious than the generic _dispatch() failure
  // would.
  FailSync(FROM_HERE, completer, ZX_ERR_NOT_SUPPORTED, "CloseSingleBuffer() not yet implemented");
}

void BufferCollection::AllocateSingleBuffer(uint64_t buffer_index,
                                            AllocateSingleBufferCompleter::Sync& completer) {
  table_set_.MitigateChurn();
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::AllocateSingleBuffer() when already "
             "is_done_");
    return;
  }
  FailSync(FROM_HERE, completer, ZX_ERR_NOT_SUPPORTED,
           "AllocateSingleBuffer() not yet implemented");
}

void BufferCollection::WaitForSingleBufferAllocated(
    uint64_t buffer_index, WaitForSingleBufferAllocatedCompleter::Sync& completer) {
  table_set_.MitigateChurn();
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::WaitForSingleBufferAllocated() when "
             "already is_done_");
    return;
  }
  FailSync(FROM_HERE, completer, ZX_ERR_NOT_SUPPORTED,
           "WaitForSingleBufferAllocated() not yet implemented");
}

void BufferCollection::CheckSingleBufferAllocated(
    uint64_t buffer_index, CheckSingleBufferAllocatedCompleter::Sync& completer) {
  table_set_.MitigateChurn();
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollectionToken::CheckSingleBufferAllocated() when "
             "already is_done_");
    return;
  }
  FailSync(FROM_HERE, completer, ZX_ERR_NOT_SUPPORTED,
           "CheckSingleBufferAllocated() not yet implemented");
}

void BufferCollection::Close(CloseCompleter::Sync& completer) {
  table_set_.MitigateChurn();
  if (is_done_) {
    FailSync(FROM_HERE, completer, ZX_ERR_BAD_STATE,
             "BufferCollection::Close() when already closed.");
    return;
  }
  // We still want to enforce that the client doesn't send any other messages
  // between Close() and closing the channel, so we just set is_done_ here and
  // do a FailAsync() if is_done_ is seen to be set while handling any other
  // message.
  is_done_ = true;
}

void BufferCollection::SetName(uint32_t priority, fidl::StringView name,
                               SetNameCompleter::Sync& completer) {
  table_set_.MitigateChurn();
  logical_buffer_collection().SetName(priority, std::string(name.begin(), name.end()));
}

void BufferCollection::SetDebugClientInfo(fidl::StringView name, uint64_t id,
                                          SetDebugClientInfoCompleter::Sync& completer) {
  table_set_.MitigateChurn();
  SetDebugClientInfoInternal(std::string(name.begin(), name.end()), id);
}

void BufferCollection::SetDebugClientInfoInternal(std::string name, uint64_t id) {
  node_properties().client_debug_info().name = std::move(name);
  node_properties().client_debug_info().id = id;
  debug_id_property_ =
      inspect_node_.CreateUint("debug_id", node_properties().client_debug_info().id);
  debug_name_property_ =
      inspect_node_.CreateString("debug_name", node_properties().client_debug_info().name);
}

void BufferCollection::FailAsync(Location location, zx_status_t status, const char* format, ...) {
  va_list args;
  va_start(args, format);
  logical_buffer_collection().VLogClientError(location, &node_properties(), format, args);
  va_end(args);

  // Idempotent, so only close once.
  if (!server_binding_)
    return;

  async_failure_result_ = status;
  server_binding_->Close(status);
  server_binding_ = {};
}

template <typename Completer>
void BufferCollection::FailSync(Location location, Completer& completer, zx_status_t status,
                                const char* format, ...) {
  va_list args;
  va_start(args, format);
  logical_buffer_collection().VLogClientError(location, &node_properties(), format, args);
  va_end(args);

  completer.Close(status);
  async_failure_result_ = status;
}

fit::result<fuchsia_sysmem2::wire::BufferCollectionInfo> BufferCollection::CloneResultForSendingV2(
    const fuchsia_sysmem2::wire::BufferCollectionInfo& buffer_collection_info) {
  auto clone_result =
      sysmem::V2CloneBufferCollectionInfo(table_set_.allocator(), buffer_collection_info,
                                          GetClientVmoRights(), GetClientAuxVmoRights());
  if (!clone_result.is_ok()) {
    FailAsync(FROM_HERE, clone_result.error(),
              "CloneResultForSendingV1() V2CloneBufferCollectionInfo() failed - status: %d",
              clone_result.error());
    return fit::error();
  }
  auto v2_b = clone_result.take_value();
  ZX_DEBUG_ASSERT(has_constraints());

  if (!constraints().has_usage() || !IsAnyUsage(constraints().usage())) {
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

fit::result<fuchsia_sysmem::wire::BufferCollectionInfo_2> BufferCollection::CloneResultForSendingV1(
    const fuchsia_sysmem2::wire::BufferCollectionInfo& buffer_collection_info) {
  auto v2_result = CloneResultForSendingV2(buffer_collection_info);
  if (!v2_result.is_ok()) {
    // FailAsync() already called.
    return fit::error();
  }
  auto v1_result = sysmem::V1MoveFromV2BufferCollectionInfo(v2_result.take_value());
  if (!v1_result.is_ok()) {
    FailAsync(FROM_HERE, ZX_ERR_INVALID_ARGS,
              "CloneResultForSendingV1() V1MoveFromV2BufferCollectionInfo() failed");
    return fit::error();
  }
  return v1_result;
}

fit::result<fuchsia_sysmem::wire::BufferCollectionInfo_2>
BufferCollection::CloneAuxBuffersResultForSendingV1(
    const fuchsia_sysmem2::wire::BufferCollectionInfo& buffer_collection_info) {
  auto v2_result = CloneResultForSendingV2(buffer_collection_info);
  if (!v2_result.is_ok()) {
    // FailAsync() already called.
    return fit::error();
  }
  auto v1_result = sysmem::V1AuxBuffersMoveFromV2BufferCollectionInfo(v2_result.take_value());
  if (!v1_result.is_ok()) {
    FailAsync(FROM_HERE, ZX_ERR_INVALID_ARGS,
              "CloneResultForSendingV1() V1MoveFromV2BufferCollectionInfo() failed");
    return fit::error();
  }
  return fit::ok(v1_result.take_value());
}

void BufferCollection::OnBuffersAllocated(const AllocationResult& allocation_result) {
  ZX_DEBUG_ASSERT(!logical_allocation_result_);

  ZX_DEBUG_ASSERT((allocation_result.status == ZX_OK) ==
                  !!allocation_result.buffer_collection_info);

  node_properties().SetBuffersLogicallyAllocated();

  logical_allocation_result_.emplace(allocation_result);

  // Any that are pending are completed by this call or something called
  // FailAsync().  It's fine for this method to ignore the fact that
  // FailAsync() may have already been called.  That's essentially the main
  // reason we have FailAsync() instead of Fail().
  MaybeCompleteWaitForBuffersAllocated();

  if (!events_) {
    return;
  }

  fuchsia_sysmem::wire::BufferCollectionInfo_2 v1;
  if (logical_allocation_result_->status == ZX_OK) {
    ZX_DEBUG_ASSERT(logical_allocation_result_->buffer_collection_info);
    auto v1_result = CloneResultForSendingV1(*logical_allocation_result_->buffer_collection_info);
    if (!v1_result.is_ok()) {
      // FailAsync() already called.
      return;
    }
    v1 = v1_result.take_value();
  }

  events_->OnBuffersAllocated(logical_allocation_result_->status, std::move(v1));
}

bool BufferCollection::has_constraints() { return node_properties().has_constraints(); }

const fuchsia_sysmem2::wire::BufferCollectionConstraints& BufferCollection::constraints() {
  ZX_DEBUG_ASSERT(has_constraints());
  return *node_properties().buffer_collection_constraints();
}

fuchsia_sysmem2::wire::BufferCollectionConstraints BufferCollection::CloneConstraints() {
  ZX_DEBUG_ASSERT(has_constraints());
  return sysmem::V2CloneBufferCollectionConstraints(table_set_.allocator(), constraints());
}

bool BufferCollection::is_done() const { return is_done_; }

BufferCollection::BufferCollection(
    fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection_param,
    const BufferCollectionToken& token)
    : Node(std::move(logical_buffer_collection_param), &token.node_properties()),
      table_set_(logical_buffer_collection().table_set()) {
  TRACE_DURATION("gfx", "BufferCollection::BufferCollection", "this", this,
                 "logical_buffer_collection", &this->logical_buffer_collection());
  ZX_DEBUG_ASSERT(shared_logical_buffer_collection());
  inspect_node_ =
      this->logical_buffer_collection().inspect_node().CreateChild(CreateUniqueName("collection-"));
}

// This method is only meant to be called from GetClientVmoRights().
uint32_t BufferCollection::GetUsageBasedRightsAttenuation() {
  // This method won't be called for participants without constraints.
  ZX_DEBUG_ASSERT(has_constraints());

  // We assume that read and map are both needed by all participants with any "usage".  Only
  // ZX_RIGHT_WRITE is controlled by usage.

  // It's not this method's job to attenuate down to kMaxClientVmoRights, so
  // let's not pretend like it is.
  uint32_t result = std::numeric_limits<uint32_t>::max();
  if (!constraints().has_usage() || !IsWriteUsage(constraints().usage())) {
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
      node_properties().rights_attenuation_mask();
}

uint32_t BufferCollection::GetClientAuxVmoRights() {
  // At least for now.
  return GetClientVmoRights();
}

void BufferCollection::MaybeCompleteWaitForBuffersAllocated() {
  if (!logical_allocation_result_) {
    // Everything is ok so far, but allocation isn't done yet.
    return;
  }
  while (!pending_wait_for_buffers_allocated_.empty()) {
    auto [async_id, txn] = std::move(pending_wait_for_buffers_allocated_.front());
    pending_wait_for_buffers_allocated_.pop_front();

    fuchsia_sysmem::wire::BufferCollectionInfo_2 v1;
    if (logical_allocation_result_->status == ZX_OK) {
      ZX_DEBUG_ASSERT(logical_allocation_result_->buffer_collection_info);
      auto v1_result = CloneResultForSendingV1(*logical_allocation_result_->buffer_collection_info);
      if (!v1_result.is_ok()) {
        // FailAsync() already called.
        return;
      }
      v1 = v1_result.take_value();
    }
    TRACE_ASYNC_END("gfx", "BufferCollection::WaitForBuffersAllocated async", async_id, "this",
                    this, "logical_buffer_collection", &logical_buffer_collection());
    auto reply_status = txn.Reply(logical_allocation_result_->status, std::move(v1));
    if (!reply_status.ok()) {
      FailAsync(FROM_HERE, reply_status.status(),
                "fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated_"
                "reply failed - status: %s",
                reply_status.error());
      return;
    }
    // ~txn
  }
}

bool BufferCollection::ReadyForAllocation() { return has_constraints(); }

void BufferCollection::Fail(zx_status_t epitaph) { CloseChannel(epitaph); }

BufferCollectionToken* BufferCollection::buffer_collection_token() { return nullptr; }

const BufferCollectionToken* BufferCollection::buffer_collection_token() const { return nullptr; }

BufferCollection* BufferCollection::buffer_collection() { return this; }

const BufferCollection* BufferCollection::buffer_collection() const { return this; }

OrphanedNode* BufferCollection::orphaned_node() { return nullptr; }

const OrphanedNode* BufferCollection::orphaned_node() const { return nullptr; }

bool BufferCollection::is_connected() const { return true; }

}  // namespace sysmem_driver
