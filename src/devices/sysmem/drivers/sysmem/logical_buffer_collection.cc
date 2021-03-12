// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logical_buffer_collection.h"

#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/fidl/llcpp/fidl_allocator.h>
#include <lib/image-format/image_format.h>
#include <lib/sysmem-version/sysmem-version.h>
#include <limits.h>  // PAGE_SIZE
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <limits>  // std::numeric_limits
#include <type_traits>
#include <unordered_map>

#include <ddk/trace/event.h>
#include <fbl/algorithm.h>
#include <fbl/string_printf.h>

#include "buffer_collection.h"
#include "buffer_collection_token.h"
#include "device.h"
#include "koid_util.h"
#include "macros.h"
#include "orphaned_node.h"
#include "src/devices/sysmem/drivers/sysmem/logging.h"
#include "usage_pixel_format_cost.h"
#include "utils.h"

namespace sysmem_driver {

namespace {

// Sysmem is creating the VMOs, so sysmem can have all the rights and just not
// mis-use any rights.  Remove ZX_RIGHT_EXECUTE though.
const uint32_t kSysmemVmoRights = ZX_DEFAULT_VMO_RIGHTS & ~ZX_RIGHT_EXECUTE;
// 1 GiB cap for now.
const uint64_t kMaxTotalSizeBytesPerCollection = 1ull * 1024 * 1024 * 1024;
// 256 MiB cap for now.
const uint64_t kMaxSizeBytesPerBuffer = 256ull * 1024 * 1024;

// Zero-initialized, so it shouldn't take up space on-disk.
constexpr uint64_t kFlushThroughBytes = 8192;
const uint8_t kZeroes[kFlushThroughBytes] = {};

constexpr uint32_t kNeedAuxVmoAlso = 1;

template <typename T>
bool IsNonZeroPowerOf2(T value) {
  if (!value) {
    return false;
  }
  if (value & (value - 1)) {
    return false;
  }
  return true;
}

// TODO(fxbug.dev/50590): It'd be nice if this could be a function template over FIDL scalar field
// types.
#define FIELD_DEFAULT_1(table_ref_name, field_name)                                    \
  do {                                                                                 \
    auto& table_ref = (table_ref_name);                                                \
    static_assert(fidl::IsTable<std::remove_reference_t<decltype(table_ref)>>::value); \
    using FieldType = std::remove_reference<decltype((table_ref.field_name()))>::type; \
    if (!table_ref.has_##field_name()) {                                               \
      table_ref.set_##field_name(table_set_.allocator(), static_cast<FieldType>(1));   \
      ZX_DEBUG_ASSERT(table_ref.field_name() == 1);                                    \
    }                                                                                  \
    ZX_DEBUG_ASSERT(table_ref.has_##field_name());                                     \
  } while (false)

// TODO(fxbug.dev/50590): It'd be nice if this could be a function template over FIDL scalar field
// types.
#define FIELD_DEFAULT_MAX(table_ref_name, field_name)                                            \
  do {                                                                                           \
    auto& table_ref = (table_ref_name);                                                          \
    static_assert(fidl::IsTable<std::remove_reference_t<decltype(table_ref)>>::value);           \
    using FieldType = std::remove_reference<decltype((table_ref.field_name()))>::type;           \
    if (!table_ref.has_##field_name()) {                                                         \
      table_ref.set_##field_name(table_set_.allocator(), std::numeric_limits<FieldType>::max()); \
      ZX_DEBUG_ASSERT(table_ref.field_name() == std::numeric_limits<FieldType>::max());          \
    }                                                                                            \
    ZX_DEBUG_ASSERT(table_ref.has_##field_name());                                               \
  } while (false)

// TODO(fxbug.dev/50590): It'd be nice if this could be a function template over FIDL scalar field
// types.
#define FIELD_DEFAULT_ZERO(table_ref_name, field_name)                                 \
  do {                                                                                 \
    auto& table_ref = (table_ref_name);                                                \
    static_assert(fidl::IsTable<std::remove_reference_t<decltype(table_ref)>>::value); \
    using FieldType = std::remove_reference<decltype((table_ref.field_name()))>::type; \
    if (!table_ref.has_##field_name()) {                                               \
      table_ref.set_##field_name(table_set_.allocator(), static_cast<FieldType>(0));   \
      ZX_DEBUG_ASSERT(!static_cast<bool>(table_ref.field_name()));                     \
    }                                                                                  \
    ZX_DEBUG_ASSERT(table_ref.has_##field_name());                                     \
  } while (false)

#define FIELD_DEFAULT_FALSE(table_ref_name, field_name)                                \
  do {                                                                                 \
    auto& table_ref = (table_ref_name);                                                \
    static_assert(fidl::IsTable<std::remove_reference_t<decltype(table_ref)>>::value); \
    using FieldType = std::remove_reference<decltype((table_ref.field_name()))>::type; \
    static_assert(std::is_same<FieldType, bool>::value);                               \
    if (!table_ref.has_##field_name()) {                                               \
      table_ref.set_##field_name(table_set_.allocator(), false);                       \
      ZX_DEBUG_ASSERT(!table_ref.field_name());                                        \
    }                                                                                  \
    ZX_DEBUG_ASSERT(table_ref.has_##field_name());                                     \
  } while (false)

#define FIELD_DEFAULT(table_ref_name, field_name, value_name)                          \
  do {                                                                                 \
    auto& table_ref = (table_ref_name);                                                \
    static_assert(fidl::IsTable<std::remove_reference_t<decltype(table_ref)>>::value); \
    using FieldType = std::remove_reference<decltype((table_ref.field_name()))>::type; \
    static_assert(!fidl::IsFidlObject<FieldType>::value);                              \
    static_assert(!fidl::IsVectorView<FieldType>::value);                              \
    static_assert(!fidl::IsStringView<FieldType>::value);                              \
    if (!table_ref.has_##field_name()) {                                               \
      auto field_value = (value_name);                                                 \
      table_ref.set_##field_name(table_set_.allocator(), field_value);                 \
      ZX_DEBUG_ASSERT(table_ref.field_name() == field_value);                          \
    }                                                                                  \
    ZX_DEBUG_ASSERT(table_ref.has_##field_name());                                     \
  } while (false)

#define FIELD_DEFAULT_SET(table_ref_name, field_name)                                        \
  do {                                                                                       \
    auto& table_ref = (table_ref_name);                                                      \
    static_assert(fidl::IsTable<std::remove_reference_t<decltype(table_ref)>>::value);       \
    using TableType = std::remove_reference_t<decltype((table_ref.field_name()))>;           \
    static_assert(fidl::IsTable<TableType>::value);                                          \
    if (!table_ref.has_##field_name()) {                                                     \
      table_ref.set_##field_name(table_set_.allocator(), TableType(table_set_.allocator())); \
    }                                                                                        \
    ZX_DEBUG_ASSERT(table_ref.has_##field_name());                                           \
  } while (false)

// regardless of capacity, initial count is always 0
#define FIELD_DEFAULT_SET_VECTOR(table_ref_name, field_name, capacity_param)                   \
  do {                                                                                         \
    auto& table_ref = (table_ref_name);                                                        \
    static_assert(fidl::IsTable<std::remove_reference_t<decltype(table_ref)>>::value);         \
    using VectorFieldType = std::remove_reference_t<decltype((table_ref.field_name()))>;       \
    static_assert(fidl::IsVectorView<VectorFieldType>::value);                                 \
    if (!table_ref.has_##field_name()) {                                                       \
      size_t capacity = (capacity_param);                                                      \
      table_ref.set_##field_name(table_set_.allocator(), table_set_.allocator(), 0, capacity); \
    }                                                                                          \
    ZX_DEBUG_ASSERT(table_ref.has_##field_name());                                             \
  } while (false)

template <typename T>
T AlignUp(T value, T divisor) {
  return (value + divisor - 1) / divisor * divisor;
}

void BarrierAfterFlush() {
#if defined(__aarch64__)
  // According to the ARMv8 ARM K11.5.4 it's better to use DSB instead of DMB
  // for ordering with respect to MMIO (DMB is ok if all agents are just
  // observing memory). The system shareability domain is used because that's
  // the only domain the video decoder is guaranteed to be in. SY is used
  // instead of LD or ST because section B2.3.5 says that the barrier needs both
  // read and write access types to be effective with regards to cache
  // operations.
  asm __volatile__("dsb sy");
#elif defined(__x86_64__)
  // This is here just in case we both (a) don't need to flush cache on x86 due to cache coherent
  // DMA (CLFLUSH not needed), and (b) we have code using non-temporal stores or "string
  // operations" whose surrounding code didn't itself take care of doing an SFENCE.  After returning
  // from this function, we may write to MMIO to start DMA - we want any previous (program order)
  // non-temporal stores to be visible to HW before that MMIO write that starts DMA.  The MFENCE
  // instead of SFENCE is mainly paranoia, though one could hypothetically create HW that starts or
  // continues DMA based on an MMIO read (please don't), in which case MFENCE might be needed here
  // before that read.
  asm __volatile__("mfence");
#else
  ZX_PANIC("logical_buffer_collection.cc missing BarrierAfterFlush() impl for this platform");
#endif
}

bool IsSecureHeap(const fuchsia_sysmem2::wire::HeapType heap_type) {
  // TODO(fxbug.dev/37452): Generalize this by finding if the heap_type maps to secure
  // MemoryAllocator.
  return heap_type == fuchsia_sysmem2::wire::HeapType::AMLOGIC_SECURE ||
         heap_type == fuchsia_sysmem2::wire::HeapType::AMLOGIC_SECURE_VDEC;
}

bool IsPotentiallyIncludedInInitialAllocation(const NodeProperties& node) {
  bool potentially_included_in_initial_allocation = true;
  for (const NodeProperties* iter = &node; iter; iter = iter->parent()) {
    if (iter->error_propagation_mode() == ErrorPropagationMode::kDoNotPropagate) {
      potentially_included_in_initial_allocation = false;
    }
  }
  return potentially_included_in_initial_allocation;
}

}  // namespace

// static
void LogicalBufferCollection::Create(zx::channel buffer_collection_token_request,
                                     Device* parent_device) {
  fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection =
      fbl::AdoptRef<LogicalBufferCollection>(new LogicalBufferCollection(parent_device));
  // The existence of a channel-owned BufferCollectionToken adds a
  // fbl::RefPtr<> ref to LogicalBufferCollection.
  LogInfo(FROM_HERE, "LogicalBufferCollection::Create()");
  logical_buffer_collection->root_ = NodeProperties::NewRoot(logical_buffer_collection.get());
  logical_buffer_collection->CreateBufferCollectionToken(
      logical_buffer_collection, logical_buffer_collection->root_.get(),
      std::move(buffer_collection_token_request));
}

// static
//
// The buffer_collection_token is the client end of the BufferCollectionToken
// which the client is exchanging for the BufferCollection (which the client is
// passing the server end of in buffer_collection_request).
//
// However, before we convert the client's token into a BufferCollection and
// start processing the messages the client may have already sent toward the
// BufferCollection, we want to process all the messages the client may have
// already sent toward the BufferCollectionToken.  This comes up because the
// BufferCollectionToken and Allocator are separate channels.
//
// We know that fidl_server will process all messages before it processes the
// close - it intentionally delays noticing the close until no messages are
// available to read.
//
// So this method will close the buffer_collection_token and when it closes via
// normal FIDL processing path, the token will remember the
// buffer_collection_request to essentially convert itself into.
void LogicalBufferCollection::BindSharedCollection(Device* parent_device,
                                                   zx::channel buffer_collection_token,
                                                   zx::channel buffer_collection_request,
                                                   const ClientDebugInfo* client_debug_info) {
  ZX_DEBUG_ASSERT(buffer_collection_token);
  ZX_DEBUG_ASSERT(buffer_collection_request);

  zx_koid_t token_client_koid;
  zx_koid_t token_server_koid;
  zx_status_t status =
      get_channel_koids(buffer_collection_token, &token_client_koid, &token_server_koid);
  if (status != ZX_OK) {
    LogErrorStatic(FROM_HERE, client_debug_info, "Failed to get channel koids");
    // ~buffer_collection_token
    // ~buffer_collection_request
    return;
  }

  BufferCollectionToken* token = parent_device->FindTokenByServerChannelKoid(token_server_koid);
  if (!token) {
    // The most likely scenario for why the token was not found is that Sync() was not called on
    // either the BufferCollectionToken or the BufferCollection.
    LogErrorStatic(FROM_HERE, client_debug_info,
                   "BindSharedCollection could not find token from server channel koid %ld; "
                   "perhaps BufferCollectionToken.Sync() was not called",
                   token_server_koid);
    // ~buffer_collection_token
    // ~buffer_collection_request
    return;
  }

  // This will token->FailAsync() if the token has already got one, or if the
  // token already saw token->Close().
  token->SetBufferCollectionRequest(std::move(buffer_collection_request));

  if (client_debug_info) {
    // The info will be propagated into the logcial buffer collection when the token closes.
    token->SetDebugClientInfoInternal(client_debug_info->name, client_debug_info->id);
  }

  // At this point, the token will process the rest of its previously queued
  // messages (from client to server), and then will convert the token into
  // a BufferCollection (view).  That conversion happens async shortly in
  // BindSharedCollectionInternal() (unless the LogicalBufferCollection fails
  // before then, in which case everything just gets deleted).
  //
  // ~buffer_collection_token here closes the client end of the token, but we
  // still process the rest of the queued messages before we process the
  // close.
  //
  // ~buffer_collection_token
}

zx_status_t LogicalBufferCollection::ValidateBufferCollectionToken(Device* parent_device,
                                                                   zx_koid_t token_server_koid) {
  BufferCollectionToken* token = parent_device->FindTokenByServerChannelKoid(token_server_koid);
  return token ? ZX_OK : ZX_ERR_NOT_FOUND;
}

void LogicalBufferCollection::CreateBufferCollectionToken(
    fbl::RefPtr<LogicalBufferCollection> self, NodeProperties* new_node_properties,
    fidl::ServerEnd<fuchsia_sysmem::BufferCollectionToken> token_request) {
  ZX_DEBUG_ASSERT(token_request);
  auto& token = BufferCollectionToken::EmplaceInTree(parent_device_, self, new_node_properties);
  token.SetErrorHandler([this, &token](zx_status_t status) {
    // Clean close from FIDL channel point of view is ZX_ERR_PEER_CLOSED,
    // and ZX_OK is never passed to the error handler.
    ZX_DEBUG_ASSERT(status != ZX_OK);

    // The dispatcher shut down before we were able to Bind(...)
    if (status == ZX_ERR_BAD_STATE) {
      LogAndFailRootNode(FROM_HERE, status, "sysmem dispatcher shutting down - status: %d", status);
      return;
    }

    // We know |this| is alive because the token is alive and the token has
    // a fbl::RefPtr<LogicalBufferCollection>.  The token is alive because
    // the token is still under the tree rooted at root_.
    //
    // Any other deletion of the token_ptr out of the tree at root_ (outside of
    // this error handler) doesn't run this error handler.
    ZX_DEBUG_ASSERT(root_);

    zx::channel buffer_collection_request = token.TakeBufferCollectionRequest();

    if (!(status == ZX_ERR_PEER_CLOSED && (token.is_done() || buffer_collection_request))) {
      // LogAndFailDownFrom() will also remove any no-longer-needed nodes from the tree.
      //
      // A token whose error handler sees anything other than clean close
      // with is_done() implies LogicalBufferCollection failure.  The
      // ability to detect unexpected closure of a token is a main reason
      // we use a channel for BufferCollectionToken instead of an
      // eventpair.
      //
      // If a participant for some reason finds itself with an extra token it doesn't need, the
      // participant should use Close() to avoid triggering this failure.
      NodeProperties* tree_to_fail = FindTreeToFail(&token.node_properties());
      if (tree_to_fail == root_.get()) {
        LogAndFailDownFrom(FROM_HERE, tree_to_fail, status,
                           "Token failure causing LogicalBufferCollection failure - status: %d",
                           status);
      } else {
        LogAndFailDownFrom(FROM_HERE, tree_to_fail, status,
                           "Token failure causing AttachToken() sub-tree failure - status: %d",
                           status);
      }
      return;
    }

    // At this point we know the token channel was closed, and that the client either did a Close()
    // or the token was dispensable, or allocator::BindSharedCollection() was used.
    ZX_DEBUG_ASSERT(status == ZX_ERR_PEER_CLOSED && (token.is_done() || buffer_collection_request));
    // BufferCollectionToken enforces that these never both become true; the BufferCollectionToken
    // will fail instead.
    ZX_DEBUG_ASSERT(!(token.is_done() && buffer_collection_request));

    if (!buffer_collection_request) {
      ZX_DEBUG_ASSERT(token.is_done());
      // This was a token::Close().  We want to stop tracking the token now that we've processed all
      // its previously-queued inbound messages.  This might be the last token, so we
      // MaybeAllocate().  This path isn't a failure (unless there are also zero BufferCollection
      // views in which case MaybeAllocate() calls Fail()).
      //
      // Keep self alive via "self" in case this will drop connected_node_count_ to zero.
      auto self = token.shared_logical_buffer_collection();
      ZX_DEBUG_ASSERT(self.get() == this);
      // This token never had any constraints, and it was Close()ed, but we need to keep the
      // NodeProperties because it may have child NodeProperties under it, and it may have had
      // SetDispensable() called on it.
      //
      // This OrphanedNode has no BufferCollectionConstraints.
      ZX_DEBUG_ASSERT(!token.node_properties().buffers_logically_allocated());
      NodeProperties& node_properties = token.node_properties();
      // This replaces token with an OrphanedNode, and also de-refs token.  Not possible to send an
      // epitaph because ZX_ERR_PEER_CLOSED.
      OrphanedNode::EmplaceInTree(fbl::RefPtr(this), &node_properties);
      MaybeAllocate();
      // ~self may delete "this"
    } else {
      // At this point we know that this was a BindSharedCollection().  We need to convert the
      // BufferCollectionToken into a BufferCollection.  The NodeProperties remains, with its Node
      // set to the new BufferCollection instead of the old BufferCollectionToken.
      //
      // ~token during this call
      BindSharedCollectionInternal(&token, std::move(buffer_collection_request));
    }
  });

  zx_koid_t server_koid;
  zx_koid_t client_koid;
  zx_status_t status = get_channel_koids(token_request.channel(), &server_koid, &client_koid);
  if (status != ZX_OK) {
    LogAndFailNode(FROM_HERE, new_node_properties, status,
                   "get_channel_koids() failed - status: %d", status);
    // ~token
    return;
  }
  token.SetServerKoid(server_koid);
  if (token.was_unfound_token()) {
    // No failure triggered by this, but a helpful debug message on how to avoid a previous failure.
    LogClientError(FROM_HERE, new_node_properties,
                   "BufferCollectionToken.Duplicate() received for creating token with server koid"
                   "%ld after BindSharedCollection() previously received attempting to use same"
                   "token.  Client sequence should be Duplicate(), Sync(), BindSharedCollection()."
                   "Missing Sync()?",
                   server_koid);
  }

  LogInfo(FROM_HERE, "CreateBufferCollectionToken() - server_koid: %lu", token.server_koid());
  token.Bind(std::move(token_request));
}

void LogicalBufferCollection::OnSetConstraints() {
  // MaybeAllocate() requires the caller to keep "this" alive.
  auto self = fbl::RefPtr(this);
  MaybeAllocate();
  return;
}

void LogicalBufferCollection::SetName(uint32_t priority, std::string name) {
  if (!name_ || (priority > name_->priority)) {
    name_ = CollectionName{priority, name};
    name_property_ = inspect_node_.CreateString("name", name);
  }
}

void LogicalBufferCollection::SetDebugTimeoutLogDeadline(int64_t deadline) {
  creation_timer_.Cancel();
  zx_status_t status =
      creation_timer_.PostForTime(parent_device_->dispatcher(), zx::time(deadline));
  ZX_ASSERT(status == ZX_OK);
}

uint64_t LogicalBufferCollection::CreateDispensableOrdinal() { return next_dispensable_ordinal_++; }

AllocationResult LogicalBufferCollection::allocation_result() {
  ZX_DEBUG_ASSERT(has_allocation_result_ ||
                  (allocation_result_status_ == ZX_OK && !allocation_result_info_));
  // If this assert fails, it mean we've already done ::Fail().  This should be impossible since
  // Fail() clears all BufferCollection views so they shouldn't be able to call
  // ::allocation_result().
  ZX_DEBUG_ASSERT(
      !(has_allocation_result_ && allocation_result_status_ == ZX_OK && !allocation_result_info_));
  return {
      .buffer_collection_info =
          allocation_result_info_ ? &(*allocation_result_info_.value()) : nullptr,
      .status = allocation_result_status_,
  };
}

LogicalBufferCollection::LogicalBufferCollection(Device* parent_device)
    : parent_device_(parent_device) {
  TRACE_DURATION("gfx", "LogicalBufferCollection::LogicalBufferCollection", "this", this);
  LogInfo(FROM_HERE, "LogicalBufferCollection::LogicalBufferCollection()");
  parent_device_->AddLogicalBufferCollection(this);
  inspect_node_ =
      parent_device_->collections_node().CreateChild(CreateUniqueName("logical-collection-"));

  zx_status_t status = creation_timer_.PostDelayed(parent_device_->dispatcher(), zx::sec(5));
  ZX_ASSERT(status == ZX_OK);
  // nothing else to do here
}

LogicalBufferCollection::~LogicalBufferCollection() {
  TRACE_DURATION("gfx", "LogicalBufferCollection::~LogicalBufferCollection", "this", this);
  LogInfo(FROM_HERE, "~LogicalBufferCollection");

  // This isn't strictly necessary, but to avoid any confusion or brittle-ness, cancel explicitly
  // before member destructors start running.
  creation_timer_.Cancel();

  // Cancel all TrackedParentVmo waits to avoid a use-after-free of |this|
  for (auto& tracked : parent_vmos_) {
    tracked.second->CancelWait();
  }

  if (memory_allocator_) {
    memory_allocator_->RemoveDestroyCallback(reinterpret_cast<intptr_t>(this));
  }

  parent_device_->RemoveLogicalBufferCollection(this);
}

void LogicalBufferCollection::LogAndFailRootNode(Location location, zx_status_t epitaph,
                                                 const char* format, ...) {
  ZX_DEBUG_ASSERT(format);
  va_list args;
  va_start(args, format);
  vLog(true, location.file(), location.line(), "LogicalBufferCollection", "fail", format, args);
  va_end(args);
  FailRootNode(epitaph);
}

void LogicalBufferCollection::FailRootNode(zx_status_t epitaph) {
  FailDownFrom(root_.get(), epitaph);
}

void LogicalBufferCollection::LogAndFailDownFrom(Location location, NodeProperties* tree_to_fail,
                                                 zx_status_t epitaph, const char* format, ...) {
  ZX_DEBUG_ASSERT(format);
  va_list args;
  va_start(args, format);
  bool is_error = (tree_to_fail == root_.get());
  vLog(is_error, location.file(), location.line(), "LogicalBufferCollection",
       is_error ? "root fail" : "sub-tree fail", format, args);
  va_end(args);
  FailDownFrom(tree_to_fail, epitaph);
}

void LogicalBufferCollection::FailDownFrom(NodeProperties* tree_to_fail, zx_status_t epitaph) {
  // Keep self alive until this method is done.
  auto self = fbl::RefPtr(this);
  bool is_root = (tree_to_fail == root_.get());
  std::vector<NodeProperties*> breadth_first_order = tree_to_fail->BreadthFirstOrder();
  while (!breadth_first_order.empty()) {
    NodeProperties* child_most = breadth_first_order.back();
    breadth_first_order.pop_back();
    ZX_DEBUG_ASSERT(child_most->child_count() == 0);
    child_most->node()->Fail(epitaph);
    child_most->RemoveFromTreeAndDelete();
  }
  if (is_root) {
    // At this point there is no further way for any participant on this LogicalBufferCollection to
    // perform a BufferCollectionToken::Duplicate(), BufferCollection::AttachToken(), or
    // BindSharedCollection(), because all the BufferCollectionToken(s) and BufferCollection(s) are
    // gone.  Any further pending requests were dropped when the channels closed just above.
    //
    // Since all the token views and collection views are gone, there is no way for any client to be
    // sent the VMOs again, so we can close the handles to the VMOs here.  This is necessary in
    // order to get ZX_VMO_ZERO_CHILDREN to happen in TrackedParentVmo, but not sufficient alone
    // (clients must also close their VMO(s)).
    //
    // We can't just allocation_result_info_.reset() here, because we're using a
    // BufferThenHeapAllocator<> that'll delay close of the VMOs until during
    // ~LogicalBufferCollection (a deadlock) unless we dig into the structure and close these VMOs
    // directly.
    if (allocation_result_info_) {
      auto& allocation_result = allocation_result_info_->mutate();
      for (uint32_t i = 0; i < allocation_result.buffers().count(); ++i) {
        if (allocation_result.buffers()[i].has_vmo()) {
          allocation_result.buffers()[i].vmo().reset();
        }
        if (allocation_result.buffers()[i].has_aux_vmo()) {
          allocation_result.buffers()[i].aux_vmo().reset();
        }
      }
      allocation_result_info_.reset();
    }
  }
  // ~self, which will delete "this" if there are no more references to "this".
}

void LogicalBufferCollection::LogAndFailNode(Location location, NodeProperties* member_node,
                                             zx_status_t epitaph, const char* format, ...) {
  ZX_DEBUG_ASSERT(format);
  auto tree_to_fail = FindTreeToFail(member_node);
  va_list args;
  va_start(args, format);
  bool is_error = (tree_to_fail == root_.get());
  vLog(is_error, location.file(), location.line(), "LogicalBufferCollection",
       is_error ? "root fail" : "sub-tree fail", format, args);
  va_end(args);
  FailDownFrom(tree_to_fail, epitaph);
}

void LogicalBufferCollection::FailNode(NodeProperties* member_node, zx_status_t epitaph) {
  auto tree_to_fail = FindTreeToFail(member_node);
  FailDownFrom(tree_to_fail, epitaph);
}

namespace {
// This function just adds a bit of indirection to allow us to construct a va_list with one entry.
// Format should always be "%s".
void LogErrorInternal(Location location, const char* format, ...) {
  va_list args;
  va_start(args, format);

  zxlogvf(ERROR, location.file(), location.line(), format, args);
  va_end(args);
}
}  // namespace

void LogicalBufferCollection::LogInfo(Location location, const char* format, ...) {
  va_list args;
  va_start(args, format);
  zxlogvf(DEBUG, location.file(), location.line(), format, args);
  va_end(args);
}

void LogicalBufferCollection::LogErrorStatic(Location location,
                                             const ClientDebugInfo* client_debug_info,
                                             const char* format, ...) {
  va_list args;
  va_start(args, format);
  fbl::String formatted = fbl::StringVPrintf(format, args);
  if (client_debug_info && !client_debug_info->name.empty()) {
    fbl::String client_name = fbl::StringPrintf(
        " - client \"%s\" id %ld", client_debug_info->name.c_str(), client_debug_info->id);

    formatted = fbl::String::Concat({formatted, client_name});
  }
  LogErrorInternal(location, "%s", formatted.c_str());
  va_end(args);
}

void LogicalBufferCollection::VLogClient(bool is_error, Location location,
                                         const NodeProperties* node_properties, const char* format,
                                         va_list args) const {
  const char* collection_name = name_ ? name_->name.c_str() : "Unknown";
  fbl::String formatted = fbl::StringVPrintf(format, args);
  if (node_properties && !node_properties->client_debug_info().name.empty()) {
    fbl::String client_name = fbl::StringPrintf(
        " - collection \"%s\" - client \"%s\" id %ld", collection_name,
        node_properties->client_debug_info().name.c_str(), node_properties->client_debug_info().id);

    formatted = fbl::String::Concat({formatted, client_name});
  } else {
    fbl::String client_name = fbl::StringPrintf(" - collection \"%s\"", collection_name);

    formatted = fbl::String::Concat({formatted, client_name});
  }

  if (is_error) {
    LogErrorInternal(location, "%s", formatted.c_str());
  } else {
    LogInfo(location, "%s", formatted.c_str());
  }

  va_end(args);
}

void LogicalBufferCollection::LogClientInfo(Location location,
                                            const NodeProperties* node_properties,
                                            const char* format, ...) const {
  va_list args;
  va_start(args, format);
  VLogClientInfo(location, node_properties, format, args);
  va_end(args);
}

void LogicalBufferCollection::LogClientError(Location location,
                                             const NodeProperties* node_properties,
                                             const char* format, ...) const {
  va_list args;
  va_start(args, format);
  VLogClientError(location, node_properties, format, args);
  va_end(args);
}

void LogicalBufferCollection::VLogClientInfo(Location location,
                                             const NodeProperties* node_properties,
                                             const char* format, va_list args) const {
  VLogClient(/*is_error=*/false, location, node_properties, format, args);
}

void LogicalBufferCollection::VLogClientError(Location location,
                                              const NodeProperties* node_properties,
                                              const char* format, va_list args) const {
  VLogClient(/*is_error=*/true, location, node_properties, format, args);
}

void LogicalBufferCollection::LogError(Location location, const char* format, ...) const {
  va_list args;
  va_start(args, format);
  VLogError(location, format, args);
  va_end(args);
}

void LogicalBufferCollection::VLogError(Location location, const char* format, va_list args) const {
  VLogClientError(location, current_node_properties_, format, args);
}

void LogicalBufferCollection::InitializeConstraintSnapshots(
    const ConstraintsList& constraints_list) {
  ZX_DEBUG_ASSERT(constraints_at_allocation_.empty());
  ZX_DEBUG_ASSERT(!constraints_list.empty());
  for (auto& constraints : constraints_list) {
    ConstraintInfoSnapshot snapshot;
    snapshot.inspect_node =
        inspect_node().CreateChild(CreateUniqueName("collection-at-allocation-"));
    if (constraints.constraints().has_min_buffer_count_for_camping()) {
      snapshot.inspect_node.CreateUint("min_buffer_count_for_camping",
                                       constraints.constraints().min_buffer_count_for_camping(),
                                       &snapshot.node_constraints);
    }
    if (constraints.constraints().has_min_buffer_count_for_shared_slack()) {
      snapshot.inspect_node.CreateUint(
          "min_buffer_count_for_shared_slack",
          constraints.constraints().min_buffer_count_for_shared_slack(),
          &snapshot.node_constraints);
    }
    if (constraints.constraints().has_min_buffer_count_for_dedicated_slack()) {
      snapshot.inspect_node.CreateUint(
          "min_buffer_count_for_dedicated_slack",
          constraints.constraints().min_buffer_count_for_dedicated_slack(),
          &snapshot.node_constraints);
    }
    if (constraints.constraints().has_min_buffer_count()) {
      snapshot.inspect_node.CreateUint("min_buffer_count",
                                       constraints.constraints().min_buffer_count(),
                                       &snapshot.node_constraints);
    }
    snapshot.inspect_node.CreateUint("debug_id", constraints.client_debug_info().id,
                                     &snapshot.node_constraints);
    snapshot.inspect_node.CreateString("debug_name", constraints.client_debug_info().name,
                                       &snapshot.node_constraints);
    constraints_at_allocation_.push_back(std::move(snapshot));
  }
}

void LogicalBufferCollection::MaybeAllocate() {
  bool did_something;
  do {
    did_something = false;
    // If a previous iteration of the loop failed the root_ of the LogicalBufferCollection, we'll
    // return below when we noticed that root_.connected_client_count() == 0.
    //
    // MaybeAllocate() is called after a connection drops.  If that connection is the last
    // connection to a failure domain, we'll fail the failure domain via this check.  We don't blame
    // the specific node that closed here, as it's just the last one, and could just as easily not
    // have been the last one.  We blame the root of the failure domain since it's fairly likely to
    // have useful debug name and debug ID.
    //
    // When it comes to connected_client_count() 0, we care about failure domains as defined by
    // FailurePropagationMode != kPropagate.  In other words, we'll fail a sub-tree with any degree
    // of failure isolation if its connected_client_count() == 0.  Whether we also fail the parent
    // tree depends on ErrorPropagationMode::kPropagateBeforeAllocation (and is_allocate_attempted_)
    // vs. ErrorPropagationMode::kDoNotPropagate.
    //
    // There's no "safe" iteration order that isn't subject to NodeProperties getting deleted out
    // from under the iteration, so we re-enumerate failure domains each time we fail a node + any
    // relevant other nodes.  The cost of enumeration could be reduced, but it should be good enough
    // given the expected participant counts.
    while (true) {
    fresh_failure_domains:;
      auto failure_domains = FailureDomainSubtrees();
      // To get more detailed log output, we fail smaller trees first.
      for (int32_t i = failure_domains.size() - 1; i >= 0; --i) {
        auto node_properties = failure_domains[i];
        if (node_properties->connected_client_count() == 0) {
          bool is_root = (node_properties == root_.get());
          if (is_root) {
            if (is_allocate_attempted_) {
              // Only log as info, because this is a normal way to destroy the buffer collection.
              LogClientInfo(FROM_HERE, node_properties, "Zero clients remain (after allocation).");
            } else {
              LogClientError(FROM_HERE, node_properties,
                             "Zero clients remain (before allocation).");
            }
          } else {
            LogClientError(FROM_HERE, node_properties,
                           "Sub-tree has zero clients remaining - failure_propagation_mode(): %u "
                           "is_allocate_attempted_: %u",
                           node_properties->error_propagation_mode(), is_allocate_attempted_);
          }
          // This may fail the parent failure domain, possibly including the root, depending on
          // error_propagation_mode() and possibly is_allocate_attempted_.  If that happens,
          // FailNode() will log INFO saying so (FindTreeToFail() will log INFO).
          FailNode(node_properties, ZX_ERR_PEER_CLOSED);
          if (is_root) {
            return;
          }
          // Not "continue" because we're nested within a for loop.
          goto fresh_failure_domains;
        }
      }
      // Processed all failure domains and found zero that needed to fail due to zero
      // connected_client_count().
      break;
    }

    // We may have failed the root.  The caller is keeping "this" alive, so we can still check
    // root_.
    if (!root_) {
      LogError(FROM_HERE,
               "Root node was failed due to sub-tree having zero clients remaining. (1)");
      return;
    }

    auto eligible_subtrees = PrunedSubtreesEligibleForLogicalAllocation();
    if (eligible_subtrees.empty()) {
      // nothing to do
      return;
    }
    for (auto eligible_subtree : eligible_subtrees) {
      // It's possible to fail a sub-tree mid-way through processing sub-trees; in that case we're
      // fine to continue with the next sub-tree since failure of one sub-tree in the list doesn't
      // impact any other sub-tree in the list.  However, if the root_ is failed, there are no
      // longer any sub-trees.
      if (!root_) {
        LogError(FROM_HERE,
                 "Root node was failed due to sub-tree having zero clients remaining. (2)");
        return;
      }

      auto nodes = NodesOfPrunedSubtreeEligibleForLogicalAllocation(*eligible_subtree);
      ZX_DEBUG_ASSERT(nodes.front() == eligible_subtree);
      bool found_not_ready_node = false;
      for (auto node_properties : nodes) {
        if (!node_properties->node()->ReadyForAllocation()) {
          found_not_ready_node = true;
          break;
        }
      }
      if (found_not_ready_node) {
        // next sub-tree
        continue;
      }

      // We know all the nodes of this sub-tree are ready to attempt allocation.

      ZX_DEBUG_ASSERT((!is_allocate_attempted_) == (eligible_subtree == root_.get()));
      ZX_DEBUG_ASSERT(is_allocate_attempted_ || eligible_subtrees.size() == 1);

      if (is_allocate_attempted_) {
        // Allocate was already previously attempted.
        TryLateLogicalAllocation(std::move(nodes));
        did_something = true;
        // next sub-tree
        continue;
      }

      // All the views have seen SetConstraints(), and there are no tokens left.
      // Regardless of whether allocation succeeds or fails, we remember we've
      // started an attempt to allocate so we don't attempt again.
      is_allocate_attempted_ = true;
      TryAllocate(std::move(nodes));
      did_something = true;

      // Try again, in case there were ready AttachToken()(s) and/or dispensable views queued up
      // behind the initial allocation.  In the next iteration if there's nothing to do we'll
      // return.
      ZX_DEBUG_ASSERT(eligible_subtrees.size() == 1);
    }
  } while (did_something);
}

void LogicalBufferCollection::TryAllocate(std::vector<NodeProperties*> nodes) {
  TRACE_DURATION("gfx", "LogicalBufferCollection::TryAllocate", "this", this);

  // If we're here it means we have connected clients.
  ZX_DEBUG_ASSERT(root_->connected_client_count() != 0);
  ZX_DEBUG_ASSERT(!root_->buffers_logically_allocated());
  ZX_DEBUG_ASSERT(!nodes.empty());

  // Since this is the initial allocation, it's impossible for anything to be eligible other than
  // the root, since parents logically allocate before children, or together with children,
  // depending on use of AttachToken() or not.
  //
  // The root will be nodes[0].
  ZX_DEBUG_ASSERT(nodes[0] == root_.get());

  // Build a list of current constraints.  We clone/copy the constraints instead of moving any
  // portion of constraints, since we potentially will need the original constraints again after an
  // AttachToken().
  ConstraintsList constraints_list;
  for (auto node_properties : nodes) {
    ZX_DEBUG_ASSERT(node_properties->node()->ReadyForAllocation());
    if (node_properties->buffer_collection_constraints()) {
      constraints_list.emplace_back(
          table_set_,
          sysmem::V2CloneBufferCollectionConstraints(
              table_set_.allocator(), *node_properties->buffer_collection_constraints()),
          *node_properties);
    }
  }

  InitializeConstraintSnapshots(constraints_list);

  auto combine_result = CombineConstraints(&constraints_list);
  if (!combine_result.is_ok()) {
    // It's impossible to combine the constraints due to incompatible
    // constraints, or all participants set null constraints.
    LOG(ERROR, "CombineConstraints() failed");
    SetFailedAllocationResult(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  ZX_DEBUG_ASSERT(combine_result.is_ok());
  ZX_DEBUG_ASSERT(constraints_list.empty());
  auto combined_constraints = combine_result.take_value();

  auto generate_result = GenerateUnpopulatedBufferCollectionInfo(combined_constraints);
  if (!generate_result.is_ok()) {
    ZX_DEBUG_ASSERT(generate_result.error() != ZX_OK);
    LOG(ERROR, "GenerateUnpopulatedBufferCollectionInfo() failed");
    SetFailedAllocationResult(generate_result.error());
    return;
  }
  ZX_DEBUG_ASSERT(generate_result.is_ok());
  auto buffer_collection_info = generate_result.take_value();

  // Save BufferCollectionInfo prior to populating with VMOs, for later comparison with analogous
  // BufferCollectionInfo generated after AttachToken().
  //
  // Save both non-linearized and linearized versions of pre-populated BufferCollectionInfo.  The
  // linearized copy is for checking whether an attachtoken sequence should succeed, and the
  // non-linearized copy is for easier logging of diffs if an AttachToken() sequence fails due to
  // mismatched BufferCollectionInfo.
  ZX_DEBUG_ASSERT(!buffer_collection_info_before_population_);
  auto clone_result =
      sysmem::V2CloneBufferCollectionInfo(table_set_.allocator(), buffer_collection_info, 0, 0);
  if (!clone_result.is_ok()) {
    ZX_DEBUG_ASSERT(clone_result.error() != ZX_OK);
    LOG(ERROR, "V2CloneBufferCollectionInfo() failed");
    SetFailedAllocationResult(clone_result.error());
    return;
  }
  buffer_collection_info_before_population_.emplace(
      TableHolder(table_set_, clone_result.take_value()));
  clone_result =
      sysmem::V2CloneBufferCollectionInfo(table_set_.allocator(), buffer_collection_info, 0, 0);
  if (!clone_result.is_ok()) {
    ZX_DEBUG_ASSERT(clone_result.error() != ZX_OK);
    LOG(ERROR, "V2CloneBufferCollectionInfo() failed");
    SetFailedAllocationResult(clone_result.error());
    return;
  }
  auto tmp_buffer_collection_info_before_population = clone_result.take_value();
  linearized_buffer_collection_info_before_population_.emplace(
      &tmp_buffer_collection_info_before_population);

  fit::result<fuchsia_sysmem2::wire::BufferCollectionInfo, zx_status_t> result =
      Allocate(combined_constraints, &buffer_collection_info);
  if (!result.is_ok()) {
    ZX_DEBUG_ASSERT(result.error() != ZX_OK);
    SetFailedAllocationResult(result.error());
    return;
  }
  ZX_DEBUG_ASSERT(result.is_ok());

  SetAllocationResult(std::move(nodes), result.take_value());
}

// This requires that nodes have the sub-tree's root-most node at nodes[0].
void LogicalBufferCollection::TryLateLogicalAllocation(std::vector<NodeProperties*> nodes) {
  TRACE_DURATION("gfx", "LogicalBufferCollection::TryLateLogicalAllocation", "this", this);

  // The initial allocation was attempted, or we wouldn't be here.
  ZX_DEBUG_ASSERT(is_allocate_attempted_);

  // The initial allocation succeeded, or we wouldn't be here.  If the initial allocation fails, it
  // responds to any already-pending late allocation attempts also, and clears out all allocation
  // attempts including late allocation attempts.
  ZX_DEBUG_ASSERT(allocation_result().status == ZX_OK &&
                  allocation_result().buffer_collection_info != nullptr);

  // If we're here it means we still have connected clients.
  ZX_DEBUG_ASSERT(root_->connected_client_count() != 0);

  // Build a list of current constraints.  We clone/copy instead of moving since we potentially will
  // need the original constraints again after another AttachToken() later.
  ConstraintsList constraints_list;

  // The constraints_list will include all already-logically-allocated node constraints, as well as
  // all node constraints from the "nodes" list which is all the nodes attempting to logically
  // allocate in this attempt.  There's also a synthetic entry to make sure the total # of buffers
  // is at least as large as the number already allocated.

  // Constraints of already-logically-allocated nodes.  This can include some OrphanedNode(s) in
  // addition to still-connected BufferCollection nodes.  There are no BufferCollectionToken nodes
  // in this category.
  auto logically_allocated_nodes =
      root_->BreadthFirstOrder([](const NodeProperties& node_properties) {
        bool keep_and_iterate_children = node_properties.buffers_logically_allocated();
        return NodeFilterResult{.keep_node = keep_and_iterate_children,
                                .iterate_children = keep_and_iterate_children};
      });
  for (auto logically_allocated_node : logically_allocated_nodes) {
    if (logically_allocated_node->buffer_collection_constraints()) {
      constraints_list.emplace_back(
          table_set_,
          sysmem::V2CloneBufferCollectionConstraints(
              table_set_.allocator(), *logically_allocated_node->buffer_collection_constraints()),
          *logically_allocated_node);
    }
  }

  // Constraints of nodes trying to logically allocate now.  These can include BufferCollection(s)
  // and OrphanedNode(s).
  for (auto additional_node : nodes) {
    ZX_DEBUG_ASSERT(additional_node->node()->ReadyForAllocation());
    if (additional_node->buffer_collection_constraints()) {
      constraints_list.emplace_back(
          table_set_,
          sysmem::V2CloneBufferCollectionConstraints(
              table_set_.allocator(), *additional_node->buffer_collection_constraints()),
          *additional_node);
    }
  }

  // Synthetic constraints entry to make sure the total # of buffers is at least as large as the
  // number already allocated.  Also, to try to use the same PixelFormat as we've already allocated,
  // else we'll fail to CombineConstraints().  Also, if what we've already allocated has any
  // optional characteristics, we require those so that we'll choose to enable those characteristics
  // again if we can, else we'll fail to CombineConstraints().
  const auto& existing = **buffer_collection_info_before_population_;
  auto existing_constraints =
      fuchsia_sysmem2::wire::BufferCollectionConstraints(table_set_.allocator());
  auto usage = fuchsia_sysmem2::wire::BufferUsage(table_set_.allocator());
  usage.set_none(table_set_.allocator(), fuchsia_sysmem2::wire::NONE_USAGE);
  existing_constraints.set_usage(table_set_.allocator(), std::move(usage));
  ZX_DEBUG_ASSERT(!existing_constraints.has_min_buffer_count_for_camping());
  ZX_DEBUG_ASSERT(!existing_constraints.has_min_buffer_count_for_dedicated_slack());
  ZX_DEBUG_ASSERT(!existing_constraints.has_min_buffer_count_for_shared_slack());
  ZX_DEBUG_ASSERT(!existing_constraints.has_min_buffer_count_for_shared_slack());
  existing_constraints.set_min_buffer_count(table_set_.allocator(),
                                            static_cast<uint32_t>(existing.buffers().count()));
  // We don't strictly need to set this, because we always try to allocate as few buffers as we can
  // so we'd catch needing more than we have during linear form comparison below, but _might_ be
  // easier to diagnose why we failed with this set, as the constraints aggregation will fail with
  // a logged message about the max_buffer_count being exceeded.
  existing_constraints.set_max_buffer_count(table_set_.allocator(),
                                            static_cast<uint32_t>(existing.buffers().count()));
  existing_constraints.set_buffer_memory_constraints(table_set_.allocator(),
                                                     table_set_.allocator());
  auto& buffer_memory_constraints = existing_constraints.buffer_memory_constraints();
  buffer_memory_constraints.set_min_size_bytes(table_set_.allocator(),
                                               existing.settings().buffer_settings().size_bytes());
  buffer_memory_constraints.set_max_size_bytes(table_set_.allocator(),
                                               existing.settings().buffer_settings().size_bytes());
  if (existing.settings().buffer_settings().is_physically_contiguous()) {
    buffer_memory_constraints.set_physically_contiguous_required(table_set_.allocator(), true);
  }
  ZX_DEBUG_ASSERT(existing.settings().buffer_settings().is_secure() ==
                  IsSecureHeap(existing.settings().buffer_settings().heap()));
  if (existing.settings().buffer_settings().is_secure()) {
    buffer_memory_constraints.set_secure_required(table_set_.allocator(), true);
  }
  switch (existing.settings().buffer_settings().coherency_domain()) {
    case fuchsia_sysmem2::wire::CoherencyDomain::CPU:
      buffer_memory_constraints.set_cpu_domain_supported(table_set_.allocator(), true);
      break;
    case fuchsia_sysmem2::wire::CoherencyDomain::RAM:
      buffer_memory_constraints.set_ram_domain_supported(table_set_.allocator(), true);
      break;
    case fuchsia_sysmem2::wire::CoherencyDomain::INACCESSIBLE:
      buffer_memory_constraints.set_inaccessible_domain_supported(table_set_.allocator(), true);
      break;
    default:
      ZX_PANIC("not yet implemented (new enum value?)");
  }
  buffer_memory_constraints.set_heap_permitted(table_set_.allocator(), table_set_.allocator(), 1);
  buffer_memory_constraints.heap_permitted()[0] = existing.settings().buffer_settings().heap();
  if (existing.settings().has_image_format_constraints()) {
    // We can't loosen the constraints after initial allocation, nor can we tighten them.  We also
    // want to chose the same PixelFormat as we already have allocated.
    existing_constraints.set_image_format_constraints(table_set_.allocator(),
                                                      table_set_.allocator(), 1);
    existing_constraints.image_format_constraints()[0] = sysmem::V2CloneImageFormatConstraints(
        table_set_.allocator(), existing.settings().image_format_constraints());
  }
  if (existing.buffers()[0].vmo_usable_start() & kNeedAuxVmoAlso) {
    existing_constraints.set_need_clear_aux_buffers_for_secure(table_set_.allocator(), true);
  }
  existing_constraints.set_allow_clear_aux_buffers_for_secure(table_set_.allocator(), true);
  // We could make this temp NodeProperties entirely stack-based, but we'd rather enforce that
  // NodeProperties is always tracked with std::unique_ptr<NodeProperties>.
  auto tmp_node = NodeProperties::NewTemporary(this, std::move(existing_constraints),
                                               "sysmem-internals-no-fewer");
  constraints_list.emplace_back(
      table_set_,
      sysmem::V2CloneBufferCollectionConstraints(table_set_.allocator(),
                                                 *tmp_node->buffer_collection_constraints()),
      *tmp_node);

  auto combine_result = CombineConstraints(&constraints_list);
  if (!combine_result.is_ok()) {
    // It's impossible to combine the constraints due to incompatible
    // constraints, or all participants set null constraints.
    LOG(ERROR, "CombineConstraints() failed -> AttachToken() sequence failed");
    // While nodes are from the pruned tree, if a parent can't allocate, then its child can't
    // allocate either, so this fails the whole sub-tree.
    SetFailedLateLogicalAllocationResult(nodes[0], ZX_ERR_NOT_SUPPORTED);
    return;
  }

  ZX_DEBUG_ASSERT(combine_result.is_ok());
  ZX_DEBUG_ASSERT(constraints_list.empty());
  auto combined_constraints = combine_result.take_value();

  auto generate_result = GenerateUnpopulatedBufferCollectionInfo(combined_constraints);
  if (!generate_result.is_ok()) {
    ZX_DEBUG_ASSERT(generate_result.error() != ZX_OK);
    LOG(ERROR,
        "GenerateUnpopulatedBufferCollectionInfo() failed -> AttachToken() sequence failed "
        "- status: %d",
        generate_result.error());
    SetFailedLateLogicalAllocationResult(nodes[0], generate_result.error());
    return;
  }
  ZX_DEBUG_ASSERT(generate_result.is_ok());
  fuchsia_sysmem2::wire::BufferCollectionInfo unpopulated_buffer_collection_info =
      generate_result.take_value();

  auto clone_result = sysmem::V2CloneBufferCollectionInfo(table_set_.allocator(),
                                                          unpopulated_buffer_collection_info, 0, 0);
  if (!clone_result.is_ok()) {
    ZX_DEBUG_ASSERT(clone_result.error() != ZX_OK);
    LOG(ERROR, "V2CloneBufferCollectionInfo() failed -> AttachToken() sequence failed - status: %d",
        clone_result.error());
    SetFailedLateLogicalAllocationResult(nodes[0], clone_result.error());
    return;
  }
  auto tmp_unpopulated_buffer_collection_info = clone_result.take_value();
  // This could be big so use heap.
  auto linearized_late_logical_allocation_buffer_collection_info =
      std::make_unique<fuchsia_sysmem2::wire::BufferCollectionInfo::OwnedEncodedMessage>(
          &tmp_unpopulated_buffer_collection_info);

  fidl::OutgoingByteMessage& original_linear_buffer_collection_info =
      linearized_buffer_collection_info_before_population_->GetOutgoingMessage();
  fidl::OutgoingByteMessage& new_linear_buffer_collection_info =
      linearized_late_logical_allocation_buffer_collection_info->GetOutgoingMessage();
  if (!original_linear_buffer_collection_info.ok()) {
    LOG(ERROR, "original error: %s", original_linear_buffer_collection_info.error());
  }
  if (!new_linear_buffer_collection_info.ok()) {
    LOG(ERROR, "new error: %s", new_linear_buffer_collection_info.error());
  }
  ZX_DEBUG_ASSERT(original_linear_buffer_collection_info.ok());
  ZX_DEBUG_ASSERT(new_linear_buffer_collection_info.ok());
  ZX_DEBUG_ASSERT(original_linear_buffer_collection_info.handle_actual() == 0);
  ZX_DEBUG_ASSERT(new_linear_buffer_collection_info.handle_actual() == 0);
  if (original_linear_buffer_collection_info.byte_actual() !=
      new_linear_buffer_collection_info.byte_actual()) {
    LOG(WARNING,
        "original_linear_buffer_collection_info.byte_actual() != "
        "new_linear_buffer_collection_info.byte_actual()");
    LogDiffsBufferCollectionInfo(**buffer_collection_info_before_population_,
                                 unpopulated_buffer_collection_info);
    SetFailedLateLogicalAllocationResult(nodes[0], ZX_ERR_NOT_SUPPORTED);
    return;
  }
  size_t linear_size_bytes = original_linear_buffer_collection_info.byte_actual();
  if (0 != memcmp(original_linear_buffer_collection_info.bytes(),
                  new_linear_buffer_collection_info.bytes(), linear_size_bytes)) {
    LOG(WARNING,
        "0 != memcmp(original_linear_buffer_collection_info.bytes(), "
        "new_linear_buffer_collection_info.bytes(), linear_size_bytes)");
    LogDiffsBufferCollectionInfo(**buffer_collection_info_before_population_,
                                 unpopulated_buffer_collection_info);
    SetFailedLateLogicalAllocationResult(nodes[0], ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // Now that we know the new participants can be added without changing the BufferCollectionInfo,
  // we can inform the new participants that their logical allocation succeeded.
  //
  // This sets success for nodes of the pruned sub-tree, not any AttachToken() children; those
  // attempt logical allocation later assuming all goes well.
  SetSucceededLateLogicalAllocationResult(std::move(nodes));
}

void LogicalBufferCollection::SetFailedAllocationResult(zx_status_t status) {
  ZX_DEBUG_ASSERT(status != ZX_OK);

  // Only set result once.
  ZX_DEBUG_ASSERT(!has_allocation_result_);
  // allocation_result_status_ is initialized to ZX_OK, so should still be set
  // that way.
  ZX_DEBUG_ASSERT(allocation_result_status_ == ZX_OK);

  creation_timer_.Cancel();
  allocation_result_status_ = status;
  // Was initialized to nullptr.
  ZX_DEBUG_ASSERT(!allocation_result_info_);
  has_allocation_result_ = true;
  SendAllocationResult(root_->BreadthFirstOrder());
  return;
}

void LogicalBufferCollection::SetAllocationResult(
    std::vector<NodeProperties*> nodes, fuchsia_sysmem2::wire::BufferCollectionInfo&& info) {
  // Setting empty constraints as the success case isn't allowed.  That's considered a failure.  At
  // least one participant must specify non-empty constraints.
  ZX_DEBUG_ASSERT(!info.IsEmpty());

  // Only set result once.
  ZX_DEBUG_ASSERT(!has_allocation_result_);

  // allocation_result_status_ is initialized to ZX_OK, so should still be set
  // that way.
  ZX_DEBUG_ASSERT(allocation_result_status_ == ZX_OK);

  creation_timer_.Cancel();
  allocation_result_status_ = ZX_OK;
  allocation_result_info_.emplace(table_set_, std::move(info));
  has_allocation_result_ = true;
  SendAllocationResult(std::move(nodes));
}

void LogicalBufferCollection::SendAllocationResult(std::vector<NodeProperties*> nodes) {
  ZX_DEBUG_ASSERT(has_allocation_result_);
  ZX_DEBUG_ASSERT(root_->buffer_collection_count() != 0);
  ZX_DEBUG_ASSERT(nodes[0] == root_.get());

  for (auto node_properties : nodes) {
    ZX_DEBUG_ASSERT(!node_properties->buffers_logically_allocated());
    node_properties->node()->OnBuffersAllocated(allocation_result());
    ZX_DEBUG_ASSERT(node_properties->buffers_logically_allocated());
  }

  if (allocation_result_status_ != ZX_OK) {
    LogAndFailRootNode(FROM_HERE, allocation_result_status_,
                       "LogicalBufferCollection::SendAllocationResult() done sending allocation "
                       "failure - now auto-failing self.");
    return;
  }
}

void LogicalBufferCollection::SetFailedLateLogicalAllocationResult(NodeProperties* tree,
                                                                   zx_status_t status_param) {
  ZX_DEBUG_ASSERT(status_param != ZX_OK);
  AllocationResult logical_allocation_result{
      .buffer_collection_info = nullptr,
      .status = status_param,
  };
  auto nodes_to_notify_and_fail = tree->BreadthFirstOrder();
  for (auto node_properties : nodes_to_notify_and_fail) {
    ZX_DEBUG_ASSERT(!node_properties->buffers_logically_allocated());
    node_properties->node()->OnBuffersAllocated(logical_allocation_result);
    ZX_DEBUG_ASSERT(node_properties->buffers_logically_allocated());
  }
  LogAndFailDownFrom(FROM_HERE, tree, status_param,
                     "AttachToken() sequence failed logical allocation - status: %d", status_param);
}

void LogicalBufferCollection::SetSucceededLateLogicalAllocationResult(
    std::vector<NodeProperties*> pruned_sub_tree) {
  ZX_DEBUG_ASSERT(allocation_result().status == ZX_OK);
  for (auto node_properties : pruned_sub_tree) {
    ZX_DEBUG_ASSERT(!node_properties->buffers_logically_allocated());
    node_properties->node()->OnBuffersAllocated(allocation_result());
    ZX_DEBUG_ASSERT(node_properties->buffers_logically_allocated());
  }
}

void LogicalBufferCollection::BindSharedCollectionInternal(BufferCollectionToken* token,
                                                           zx::channel buffer_collection_request) {
  ZX_DEBUG_ASSERT(buffer_collection_request.get());
  auto self = fbl::RefPtr(this);
  // This links the new collection into the tree under root_ in the same place as the token was, and
  // deletes the token.
  //
  // ~BufferCollectionToken calls UntrackTokenKoid().
  auto& collection = BufferCollection::EmplaceInTree(self, token);
  token = nullptr;
  collection.SetErrorHandler([this, &collection](zx_status_t status) {
    // status passed to an error handler is never ZX_OK.  Clean close is
    // ZX_ERR_PEER_CLOSED.
    ZX_DEBUG_ASSERT(status != ZX_OK);

    // The dispatcher shut down before we were able to Bind(...)
    if (status == ZX_ERR_BAD_STATE) {
      LogAndFailRootNode(FROM_HERE, status, "sysmem dispatcher shutting down - status: %d", status);
      return;
    }

    // We know collection is still alive because collection is still under root_.  We know "this"
    // is still alive because collection has a fbl::RefPtr<> to "this".
    //
    // If collection isn't under root_, this check isn't going to be robust, but it's better than
    // nothing.  We could iterate the tree to verify it contains collection, but that'd be a lot for
    // just an assert.
    ZX_DEBUG_ASSERT(collection.node_properties().parent() ||
                    &collection.node_properties() == root_.get());

    // The BufferCollection may have had Close() called on it, in which case closure of the
    // BufferCollection doesn't cause LogicalBufferCollection failure.  Or, Close() wasn't called
    // and the BufferCollection node needs to fail, along with its failure domain and any child
    // failure domains.

    if (!(status == ZX_ERR_PEER_CLOSED && collection.is_done())) {
      // LogAndFailDownFrom() will also remove any no-longer-needed Node(s) from the tree.
      //
      // A collection whose error handler sees anything other than clean Close() (is_done() true)
      // implies LogicalBufferCollection failure.  The ability to detect unexpected closure is a
      // main reason we use a channel for BufferCollection instead of an eventpair.
      //
      // If a participant for some reason finds itself with an extra BufferCollection it doesn't
      // need, the participant should use Close() to avoid triggering this failure.
      NodeProperties* tree_to_fail = FindTreeToFail(&collection.node_properties());
      if (tree_to_fail == root_.get()) {
        // A LogicalBufferCollection intentionally treats any error (other than errors explicitly
        // ignored using SetDispensable() or AttachToken()) that might be triggered by a client
        // failure as a LogicalBufferCollection failure, because a LogicalBufferCollection can
        // use a lot of RAM and can tend to block creating a replacement LogicalBufferCollection.
        //
        // In rare cases, an initiator might choose to use Close() to avoid this failure, but more
        // typically initiators will just close their BufferCollection view without Close() first,
        // and this failure results.  This is considered acceptable partly because it helps exercise
        // code in participants that may see BufferCollection channel closure before closure of
        // related channels, and it helps get the VMO handles closed ASAP to avoid letting those
        // continue to use space of a MemoryAllocator's pool of pre-reserved space (for example).
        //
        // We only log if a participant closed before the initiator, as the initiator closing first
        // is considered normal.
        if (&collection.node_properties() == root_.get()) {
          // Normal for initiator to close first, in which case this isn't necessarily a failure.
          // If we had client to server epitaphs, we'd be able to tell the difference and log only
          // if a participant closed the channel without sending a ZX_OK epitaph.  But we don't have
          // client to server epitaphs so far.
          FailDownFrom(tree_to_fail, status);
        } else {
          // If this is too noisy, we can just do FailDownFrom() for both, but it'd be nice if
          // participants other than the initiator wouldn't be the first to cause
          // LogicalBufferCollection failure.
          LogAndFailDownFrom(FROM_HERE, tree_to_fail, status,
                             "Child BufferCollection failure causing LogicalBufferCollection "
                             "failure (to silence this, initiator can close first) - status: %d",
                             status);
        }
      } else {
        // This also removes the sub-tree, which can reduce SUM(min_buffer_count_for_camping) (or
        // similar for other constraints) to make room for a replacement sub-tree.  The replacement
        // sub-tree can be created with AttachToken().  The initial sub-tree may have been placed
        // in a separate failure domain by using SetDispensable() or AttachToken().
        //
        // Hopefully this won't be too noisy.
        LogAndFailDownFrom(FROM_HERE, tree_to_fail, status,
                           "BufferCollection failure causing sub-tree failure (SetDispensable() or "
                           "AttachToken() was used) - status: %d",
                           status);
      }
      return;
    }

    // At this point we know the collection is cleanly done (Close() was sent from client).  We keep
    // the NodeProperties for now though, in case there are children, and in case the
    // BufferCollection had SetDispensable() called on the token that led to this BufferCollection.
    //
    // We keep the collection's constraints (if any), as those are still relevant; this lets a
    // participant do SetConstraints() followed by Close() followed by closing the participant's
    // BufferCollection channel, which is convenient for some participants.
    //
    // If this causes zero remaining BufferCollectionToken(s) and zero remaining BufferCollection(s)
    // then LogicalBufferCollection can be deleted below.
    ZX_DEBUG_ASSERT(collection.is_done());
    auto self = fbl::RefPtr(this);
    ZX_DEBUG_ASSERT(self.get() == this);
    ZX_DEBUG_ASSERT(collection.shared_logical_buffer_collection().get() == this);
    // This also de-refs collection.
    (void)OrphanedNode::EmplaceInTree(self, &collection.node_properties());
    MaybeAllocate();
    // ~self may delete "this"
    return;
  });
  collection.Bind(std::move(buffer_collection_request));
  // ~self
}

bool LogicalBufferCollection::IsMinBufferSizeSpecifiedByAnyParticipant(
    const ConstraintsList& constraints_list) {
  ZX_DEBUG_ASSERT(root_->connected_client_count() != 0);
  ZX_DEBUG_ASSERT(!constraints_list.empty());
  for (auto& entry : constraints_list) {
    auto& constraints = entry.constraints();
    if (constraints.has_buffer_memory_constraints() &&
        constraints.buffer_memory_constraints().has_min_size_bytes() &&
        constraints.buffer_memory_constraints().min_size_bytes() > 0) {
      return true;
    }
    if (constraints.has_image_format_constraints()) {
      for (auto& image_format_constraints : constraints.image_format_constraints()) {
        if (image_format_constraints.has_min_coded_width() &&
            image_format_constraints.has_min_coded_height() &&
            image_format_constraints.min_coded_width() > 0 &&
            image_format_constraints.min_coded_height() > 0) {
          return true;
        }
        if (image_format_constraints.has_required_max_coded_width() &&
            image_format_constraints.has_required_max_coded_height() &&
            image_format_constraints.required_max_coded_width() > 0 &&
            image_format_constraints.required_max_coded_height() > 0) {
          return true;
        }
      }
    }
  }
  return false;
}

std::vector<NodeProperties*> LogicalBufferCollection::FailureDomainSubtrees() {
  if (!root_) {
    return std::vector<NodeProperties*>();
  }
  return root_->BreadthFirstOrder([](const NodeProperties& node_properties) {
    if (!node_properties.parent()) {
      return NodeFilterResult{.keep_node = true, .iterate_children = true};
    }
    if (node_properties.error_propagation_mode() != ErrorPropagationMode::kPropagate) {
      return NodeFilterResult{.keep_node = true, .iterate_children = true};
    }
    return NodeFilterResult{.keep_node = false, .iterate_children = true};
  });
}

// This could be more efficient, but should be fast enough as-is.
std::vector<NodeProperties*> LogicalBufferCollection::PrunedSubtreesEligibleForLogicalAllocation() {
  if (!root_) {
    return std::vector<NodeProperties*>();
  }
  return root_->BreadthFirstOrder([](const NodeProperties& node_properties) {
    if (node_properties.buffers_logically_allocated()) {
      return NodeFilterResult{.keep_node = false, .iterate_children = true};
    }
    if (node_properties.parent() && !node_properties.parent()->buffers_logically_allocated()) {
      return NodeFilterResult{.keep_node = false, .iterate_children = false};
    }
    ZX_DEBUG_ASSERT(!node_properties.parent() || node_properties.error_propagation_mode() ==
                                                     ErrorPropagationMode::kDoNotPropagate);
    return NodeFilterResult{.keep_node = true, .iterate_children = false};
  });
}

std::vector<NodeProperties*>
LogicalBufferCollection::NodesOfPrunedSubtreeEligibleForLogicalAllocation(NodeProperties& subtree) {
  ZX_DEBUG_ASSERT(!subtree.buffers_logically_allocated());
  ZX_DEBUG_ASSERT((&subtree == root_.get()) || subtree.parent()->buffers_logically_allocated());
  return subtree.BreadthFirstOrder([&subtree](const NodeProperties& node_properties) {
    bool in_subtree = false;
    bool iterate_children = true;
    for (const NodeProperties* iter = &node_properties; iter; iter = iter->parent()) {
      if (iter == &subtree) {
        in_subtree = true;
        break;
      }
      if (iter->error_propagation_mode() == ErrorPropagationMode::kDoNotPropagate) {
        iterate_children = false;
        break;
      }
    }
    return NodeFilterResult{.keep_node = in_subtree, .iterate_children = iterate_children};
  });
}

void LogicalBufferCollection::LogTreeForDebugOnly(NodeProperties* node) {
  LOG(INFO, "node: 0x%p", node);
  LOG(INFO, "node->error_propagation_mode(): %u", node->error_propagation_mode());
  LOG(INFO, "node->buffers_logically_allocated(): %u", node->buffers_logically_allocated());
  for (auto& [child_ptr, child_smart_ptr] : node->children_) {
    LOG(INFO, "child: 0x%p", child_ptr);
    LogTreeForDebugOnly(child_ptr);
  }
}

fit::result<fuchsia_sysmem2::wire::BufferCollectionConstraints, void>
LogicalBufferCollection::CombineConstraints(ConstraintsList* constraints_list) {
  // This doesn't necessarily mean that any of the clients have
  // set non-empty constraints though.  We do require that at least one
  // participant (probably the initiator) retains an open channel to its
  // BufferCollection until allocation is done, else allocation won't be
  // attempted.
  ZX_DEBUG_ASSERT(root_->connected_client_count() != 0);
  ZX_DEBUG_ASSERT(!constraints_list->empty());

  // At least one participant must specify min buffer size (in terms of non-zero min buffer size or
  // non-zero min image size or non-zero potential max image size).
  //
  // This also enforces that at least one participant must specify non-empty constraints.
  if (!IsMinBufferSizeSpecifiedByAnyParticipant(*constraints_list)) {
    // Too unconstrained...  We refuse to allocate buffers without any min size
    // bounds from any participant.  At least one particpant must provide
    // some form of size bounds (in terms of buffer size bounds or in terms
    // of image size bounds).
    LogError(FROM_HERE,
             "At least one participant must specify buffer_memory_constraints or "
             "image_format_constraints that implies non-zero min buffer size.");
    return fit::error();
  }

  // Start with empty constraints / unconstrained.
  fuchsia_sysmem2::wire::BufferCollectionConstraints acc(table_set_.allocator());
  // Sanitize initial accumulation target to keep accumulation simpler.  This is guaranteed to
  // succeed; the input is always the same.
  bool result = CheckSanitizeBufferCollectionConstraints(CheckSanitizeStage::kInitial, acc);
  ZX_DEBUG_ASSERT(result);
  // Accumulate each participant's constraints.
  while (!constraints_list->empty()) {
    Constraints constraints_entry = std::move(constraints_list->front());
    constraints_list->pop_front();
    current_node_properties_ = &constraints_entry.node_properties();
    auto defer_reset = fit::defer([this] { current_node_properties_ = nullptr; });
    if (!CheckSanitizeBufferCollectionConstraints(CheckSanitizeStage::kNotAggregated,
                                                  constraints_entry.mutate_constraints())) {
      return fit::error();
    }
    if (!AccumulateConstraintBufferCollection(&acc,
                                              std::move(constraints_entry.mutate_constraints()))) {
      // This is a failure.  The space of permitted settings contains no
      // points.
      return fit::error();
    }
  }

  if (!CheckSanitizeBufferCollectionConstraints(CheckSanitizeStage::kAggregated, acc)) {
    return fit::error();
  }

  return fit::ok(std::move(acc));
}

// TODO(dustingreen): Consider rejecting secure_required + any non-secure heaps, including the
// potentially-implicit SYSTEM_RAM heap.
//
// TODO(dustingreen): From a particular participant, CPU usage without
// IsCpuAccessibleHeapPermitted() should fail.
//
// TODO(dustingreen): From a particular participant, be more picky about which domains are supported
// vs. which heaps are supported.
static bool IsHeapPermitted(const fuchsia_sysmem2::wire::BufferMemoryConstraints& constraints,
                            fuchsia_sysmem2::wire::HeapType heap) {
  if (constraints.heap_permitted().count()) {
    auto begin = constraints.heap_permitted().begin();
    auto end = constraints.heap_permitted().end();
    return std::find(begin, end, heap) != end;
  }
  // Zero heaps in heap_permitted() means any heap is ok.
  return true;
}

static bool IsSecurePermitted(const fuchsia_sysmem2::wire::BufferMemoryConstraints& constraints) {
  // TODO(fxbug.dev/37452): Generalize this by finding if there's a heap that maps to secure
  // MemoryAllocator in the permitted heaps.
  return constraints.inaccessible_domain_supported() &&
         (IsHeapPermitted(constraints, fuchsia_sysmem2::wire::HeapType::AMLOGIC_SECURE) ||
          IsHeapPermitted(constraints, fuchsia_sysmem2::wire::HeapType::AMLOGIC_SECURE_VDEC));
}

static bool IsCpuAccessSupported(
    const fuchsia_sysmem2::wire::BufferMemoryConstraints& constraints) {
  return constraints.cpu_domain_supported() || constraints.ram_domain_supported();
}

bool LogicalBufferCollection::CheckSanitizeBufferUsage(
    CheckSanitizeStage stage, fuchsia_sysmem2::wire::BufferUsage& buffer_usage) {
  FIELD_DEFAULT_ZERO(buffer_usage, none);
  FIELD_DEFAULT_ZERO(buffer_usage, cpu);
  FIELD_DEFAULT_ZERO(buffer_usage, vulkan);
  FIELD_DEFAULT_ZERO(buffer_usage, display);
  FIELD_DEFAULT_ZERO(buffer_usage, video);
  switch (stage) {
    case CheckSanitizeStage::kInitial:
      // empty usage is allowed for kInitial
      break;
    case CheckSanitizeStage::kNotAggregated:
      // At least one usage bit must be specified by any participant that
      // specifies constraints.  The "none" usage bit can be set by a participant
      // that doesn't directly use the buffers, so we know that the participant
      // didn't forget to set usage.
      if (buffer_usage.none() == 0 && buffer_usage.cpu() == 0 && buffer_usage.vulkan() == 0 &&
          buffer_usage.display() == 0 && buffer_usage.video() == 0) {
        LogError(FROM_HERE, "At least one usage bit must be set by a participant.");
        return false;
      }
      if (buffer_usage.none() != 0) {
        if (buffer_usage.cpu() != 0 || buffer_usage.vulkan() != 0 || buffer_usage.display() != 0 ||
            buffer_usage.video() != 0) {
          LogError(FROM_HERE,
                   "A participant indicating 'none' usage can't specify any other usage.");
          return false;
        }
      }
      break;
    case CheckSanitizeStage::kAggregated:
      if (buffer_usage.cpu() == 0 && buffer_usage.vulkan() == 0 && buffer_usage.display() == 0 &&
          buffer_usage.video() == 0) {
        LogError(FROM_HERE,
                 "At least one non-'none' usage bit must be set across all participants.");
        return false;
      }
      break;
  }
  return true;
}

size_t LogicalBufferCollection::InitialCapacityOrZero(CheckSanitizeStage stage,
                                                      size_t initial_capacity) {
  return (stage == CheckSanitizeStage::kInitial) ? initial_capacity : 0;
}

// Nearly all constraint checks must go under here or under ::Allocate() (not in
// the Accumulate* methods), else we could fail to notice a single participant
// providing unsatisfiable constraints, where no Accumulate* happens.  The
// constraint checks that are present under Accumulate* are commented explaining
// why it's ok for them to be there.
bool LogicalBufferCollection::CheckSanitizeBufferCollectionConstraints(
    CheckSanitizeStage stage, fuchsia_sysmem2::wire::BufferCollectionConstraints& constraints) {
  bool was_empty = constraints.IsEmpty();
  FIELD_DEFAULT_SET(constraints, usage);
  if (was_empty) {
    // Completely empty constraints are permitted, so convert to NONE_USAGE to avoid triggering the
    // check applied to non-empty constraints where at least one usage bit must be set (NONE_USAGE
    // counts for that check, and doesn't constrain anything).
    FIELD_DEFAULT(constraints.usage(), none, fuchsia_sysmem2::wire::NONE_USAGE);
  }
  FIELD_DEFAULT_ZERO(constraints, min_buffer_count_for_camping);
  FIELD_DEFAULT_ZERO(constraints, min_buffer_count_for_dedicated_slack);
  FIELD_DEFAULT_ZERO(constraints, min_buffer_count_for_shared_slack);
  FIELD_DEFAULT_ZERO(constraints, min_buffer_count);
  FIELD_DEFAULT_MAX(constraints, max_buffer_count);
  ZX_DEBUG_ASSERT(constraints.has_buffer_memory_constraints() ||
                  stage != CheckSanitizeStage::kAggregated);
  FIELD_DEFAULT_SET(constraints, buffer_memory_constraints);
  ZX_DEBUG_ASSERT(constraints.has_buffer_memory_constraints());
  FIELD_DEFAULT_SET_VECTOR(constraints, image_format_constraints, InitialCapacityOrZero(stage, 64));
  FIELD_DEFAULT_FALSE(constraints, need_clear_aux_buffers_for_secure);
  FIELD_DEFAULT(constraints, allow_clear_aux_buffers_for_secure,
                !IsWriteUsage(constraints.usage()));
  if (!CheckSanitizeBufferUsage(stage, constraints.usage())) {
    LogError(FROM_HERE, "CheckSanitizeBufferUsage() failed");
    return false;
  }
  if (constraints.max_buffer_count() == 0) {
    LogError(FROM_HERE, "max_buffer_count == 0");
    return false;
  }
  if (constraints.min_buffer_count() > constraints.max_buffer_count()) {
    LogError(FROM_HERE, "min_buffer_count > max_buffer_count");
    return false;
  }
  if (!CheckSanitizeBufferMemoryConstraints(stage, constraints.usage(),
                                            constraints.buffer_memory_constraints())) {
    return false;
  }
  if (stage != CheckSanitizeStage::kAggregated) {
    if (IsCpuUsage(constraints.usage())) {
      if (!IsCpuAccessSupported(constraints.buffer_memory_constraints())) {
        LogError(FROM_HERE, "IsCpuUsage() && !IsCpuAccessSupported()");
        return false;
      }
      // From a single participant, reject secure_required in combination with CPU usage, since CPU
      // usage isn't possible given secure memory.
      if (constraints.buffer_memory_constraints().secure_required()) {
        LogError(FROM_HERE, "IsCpuUsage() && secure_required");
        return false;
      }
      // It's fine if a participant sets CPU usage but also permits inaccessible domain and possibly
      // IsSecurePermitted().  In that case the participant is expected to pay attention to the
      // coherency domain and is_secure and realize that it shouldn't attempt to read/write the
      // VMOs.
    }
    if (constraints.buffer_memory_constraints().secure_required() &&
        IsCpuAccessSupported(constraints.buffer_memory_constraints())) {
      // This is a little picky, but easier to be less picky later than more picky later.
      LogError(FROM_HERE, "secure_required && IsCpuAccessSupported()");
      return false;
    }
  }
  for (uint32_t i = 0; i < constraints.image_format_constraints().count(); ++i) {
    if (!CheckSanitizeImageFormatConstraints(stage, constraints.image_format_constraints()[i])) {
      return false;
    }
  }

  if (stage == CheckSanitizeStage::kNotAggregated) {
    // As an optimization, only check the unaggregated inputs.
    for (uint32_t i = 0; i < constraints.image_format_constraints().count(); ++i) {
      for (uint32_t j = i + 1; j < constraints.image_format_constraints().count(); ++j) {
        if (ImageFormatIsPixelFormatEqual(
                constraints.image_format_constraints()[i].pixel_format(),
                constraints.image_format_constraints()[j].pixel_format())) {
          LogError(FROM_HERE, "image format constraints %d and %d have identical formats", i, j);
          return false;
        }
      }
    }
  }
  return true;
}

bool LogicalBufferCollection::CheckSanitizeBufferMemoryConstraints(
    CheckSanitizeStage stage, const fuchsia_sysmem2::wire::BufferUsage& buffer_usage,
    fuchsia_sysmem2::wire::BufferMemoryConstraints& constraints) {
  FIELD_DEFAULT_ZERO(constraints, min_size_bytes);
  FIELD_DEFAULT_MAX(constraints, max_size_bytes);
  FIELD_DEFAULT_FALSE(constraints, physically_contiguous_required);
  FIELD_DEFAULT_FALSE(constraints, secure_required);
  // The CPU domain is supported by default.
  FIELD_DEFAULT(constraints, cpu_domain_supported, true);
  // If !usage.cpu, then participant doesn't care what domain, so indicate support
  // for RAM and inaccessible domains in that case.
  FIELD_DEFAULT(constraints, ram_domain_supported, !buffer_usage.cpu());
  FIELD_DEFAULT(constraints, inaccessible_domain_supported, !buffer_usage.cpu());
  if (stage != CheckSanitizeStage::kAggregated) {
    if (constraints.has_heap_permitted() && !constraints.heap_permitted().count()) {
      LogError(FROM_HERE,
               "constraints.has_heap_permitted() && !constraints.heap_permitted().count()");
      return false;
    }
  }
  // TODO(dustingreen): When 0 heaps specified, constrain heap list based on other constraints.
  // For now 0 heaps means any heap.
  FIELD_DEFAULT_SET_VECTOR(constraints, heap_permitted, 0);
  ZX_DEBUG_ASSERT(stage != CheckSanitizeStage::kInitial ||
                  constraints.heap_permitted().count() == 0);
  if (constraints.min_size_bytes() > constraints.max_size_bytes()) {
    LogError(FROM_HERE, "min_size_bytes > max_size_bytes");
    return false;
  }
  if (constraints.secure_required() && !IsSecurePermitted(constraints)) {
    LogError(FROM_HERE, "secure memory required but not permitted");
    return false;
  }
  return true;
}

bool LogicalBufferCollection::CheckSanitizeImageFormatConstraints(
    CheckSanitizeStage stage, fuchsia_sysmem2::wire::ImageFormatConstraints& constraints) {
  // We never CheckSanitizeImageFormatConstraints() on empty (aka initial) constraints.
  ZX_DEBUG_ASSERT(stage != CheckSanitizeStage::kInitial);

  FIELD_DEFAULT_SET(constraints, pixel_format);
  FIELD_DEFAULT_ZERO(constraints.pixel_format(), type);
  FIELD_DEFAULT_ZERO(constraints.pixel_format(), format_modifier_value);

  FIELD_DEFAULT_SET_VECTOR(constraints, color_spaces, 0);

  FIELD_DEFAULT_ZERO(constraints, min_coded_width);
  FIELD_DEFAULT_MAX(constraints, max_coded_width);
  FIELD_DEFAULT_ZERO(constraints, min_coded_height);
  FIELD_DEFAULT_MAX(constraints, max_coded_height);
  FIELD_DEFAULT_ZERO(constraints, min_bytes_per_row);
  FIELD_DEFAULT_MAX(constraints, max_bytes_per_row);
  FIELD_DEFAULT_MAX(constraints, max_coded_width_times_coded_height);

  FIELD_DEFAULT_1(constraints, coded_width_divisor);
  FIELD_DEFAULT_1(constraints, coded_height_divisor);
  FIELD_DEFAULT_1(constraints, bytes_per_row_divisor);
  FIELD_DEFAULT_1(constraints, start_offset_divisor);
  FIELD_DEFAULT_1(constraints, display_width_divisor);
  FIELD_DEFAULT_1(constraints, display_height_divisor);

  FIELD_DEFAULT_MAX(constraints, required_min_coded_width);
  FIELD_DEFAULT_ZERO(constraints, required_max_coded_width);
  FIELD_DEFAULT_MAX(constraints, required_min_coded_height);
  FIELD_DEFAULT_ZERO(constraints, required_max_coded_height);
  FIELD_DEFAULT_MAX(constraints, required_min_bytes_per_row);
  FIELD_DEFAULT_ZERO(constraints, required_max_bytes_per_row);

  if (constraints.pixel_format().type() == fuchsia_sysmem2::wire::PixelFormatType::INVALID) {
    LogError(FROM_HERE, "PixelFormatType INVALID not allowed");
    return false;
  }
  if (!ImageFormatIsSupported(constraints.pixel_format())) {
    LogError(FROM_HERE, "Unsupported pixel format");
    return false;
  }

  uint32_t min_bytes_per_row_given_min_width =
      ImageFormatStrideBytesPerWidthPixel(constraints.pixel_format()) *
      constraints.min_coded_width();
  constraints.min_bytes_per_row() =
      std::max(constraints.min_bytes_per_row(), min_bytes_per_row_given_min_width);

  if (!constraints.color_spaces().count()) {
    LogError(FROM_HERE, "color_spaces.count() == 0 not allowed");
    return false;
  }

  if (constraints.min_coded_width() > constraints.max_coded_width()) {
    LogError(FROM_HERE, "min_coded_width > max_coded_width");
    return false;
  }
  if (constraints.min_coded_height() > constraints.max_coded_height()) {
    LogError(FROM_HERE, "min_coded_height > max_coded_height");
    return false;
  }
  if (constraints.min_bytes_per_row() > constraints.max_bytes_per_row()) {
    LogError(FROM_HERE, "min_bytes_per_row > max_bytes_per_row");
    return false;
  }
  if (constraints.min_coded_width() * constraints.min_coded_height() >
      constraints.max_coded_width_times_coded_height()) {
    LogError(FROM_HERE,
             "min_coded_width * min_coded_height > "
             "max_coded_width_times_coded_height");
    return false;
  }

  if (!IsNonZeroPowerOf2(constraints.coded_width_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 coded_width_divisor not supported");
    return false;
  }
  if (!IsNonZeroPowerOf2(constraints.coded_height_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 coded_width_divisor not supported");
    return false;
  }
  if (!IsNonZeroPowerOf2(constraints.bytes_per_row_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 bytes_per_row_divisor not supported");
    return false;
  }
  if (!IsNonZeroPowerOf2(constraints.start_offset_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 start_offset_divisor not supported");
    return false;
  }
  if (constraints.start_offset_divisor() > PAGE_SIZE) {
    LogError(FROM_HERE, "support for start_offset_divisor > PAGE_SIZE not yet implemented");
    return false;
  }
  if (!IsNonZeroPowerOf2(constraints.display_width_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 display_width_divisor not supported");
    return false;
  }
  if (!IsNonZeroPowerOf2(constraints.display_height_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 display_height_divisor not supported");
    return false;
  }

  for (uint32_t i = 0; i < constraints.color_spaces().count(); ++i) {
    if (!ImageFormatIsSupportedColorSpaceForPixelFormat(constraints.color_spaces()[i],
                                                        constraints.pixel_format())) {
      auto colorspace_type = constraints.color_spaces()[i].has_type()
                                 ? constraints.color_spaces()[i].type()
                                 : fuchsia_sysmem2::wire::ColorSpaceType::INVALID;
      LogError(FROM_HERE,
               "!ImageFormatIsSupportedColorSpaceForPixelFormat() "
               "color_space.type: %u "
               "pixel_format.type: %u",
               colorspace_type, constraints.pixel_format().type());
      return false;
    }
  }

  if (constraints.required_min_coded_width() == 0) {
    LogError(FROM_HERE, "required_min_coded_width == 0");
    return false;
  }
  ZX_DEBUG_ASSERT(constraints.required_min_coded_width() != 0);
  if (constraints.required_min_coded_width() < constraints.min_coded_width()) {
    LogError(FROM_HERE, "required_min_coded_width < min_coded_width");
    return false;
  }
  if (constraints.required_max_coded_width() > constraints.max_coded_width()) {
    LogError(FROM_HERE, "required_max_coded_width > max_coded_width");
    return false;
  }
  if (constraints.required_min_coded_height() == 0) {
    LogError(FROM_HERE, "required_min_coded_height == 0");
    return false;
  }
  ZX_DEBUG_ASSERT(constraints.required_min_coded_height() != 0);
  if (constraints.required_min_coded_height() < constraints.min_coded_height()) {
    LogError(FROM_HERE, "required_min_coded_height < min_coded_height");
    return false;
  }
  if (constraints.required_max_coded_height() > constraints.max_coded_height()) {
    LogError(FROM_HERE, "required_max_coded_height > max_coded_height");
    return false;
  }
  if (constraints.required_min_bytes_per_row() == 0) {
    LogError(FROM_HERE, "required_min_bytes_per_row == 0");
    return false;
  }
  ZX_DEBUG_ASSERT(constraints.required_min_bytes_per_row() != 0);
  if (constraints.required_min_bytes_per_row() < constraints.min_bytes_per_row()) {
    LogError(FROM_HERE, "required_min_bytes_per_row < min_bytes_per_row");
    return false;
  }
  if (constraints.required_max_bytes_per_row() > constraints.max_bytes_per_row()) {
    LogError(FROM_HERE, "required_max_bytes_per_row > max_bytes_per_row");
    return false;
  }

  // TODO(dustingreen): Check compatibility of color_space[] entries vs. the
  // pixel_format.  In particular, 2020 and 2100 don't have 8 bpp, only 10 or
  // 12 bpp, while a given PixelFormat.type is a specific bpp.  There's
  // probably no reason to allow 2020 or 2100 to be specified along with a
  // PixelFormat.type that's 8 bpp for example.

  return true;
}

bool LogicalBufferCollection::AccumulateConstraintsBufferUsage(
    fuchsia_sysmem2::wire::BufferUsage* acc, fuchsia_sysmem2::wire::BufferUsage c) {
  // We accumulate "none" usage just like other usages, to make aggregation and CheckSanitize
  // consistent/uniform.
  acc->none() |= c.none();
  acc->cpu() |= c.cpu();
  acc->vulkan() |= c.vulkan();
  acc->display() |= c.display();
  acc->video() |= c.video();
  return true;
}

// |acc| accumulated constraints so far
//
// |c| additional constraint to aggregate into acc
bool LogicalBufferCollection::AccumulateConstraintBufferCollection(
    fuchsia_sysmem2::wire::BufferCollectionConstraints* acc,
    fuchsia_sysmem2::wire::BufferCollectionConstraints c) {
  if (!AccumulateConstraintsBufferUsage(&acc->usage(), std::move(c.usage()))) {
    return false;
  }

  acc->min_buffer_count_for_camping() += c.min_buffer_count_for_camping();
  acc->min_buffer_count_for_dedicated_slack() += c.min_buffer_count_for_dedicated_slack();

  acc->min_buffer_count_for_shared_slack() =
      std::max(acc->min_buffer_count_for_shared_slack(), c.min_buffer_count_for_shared_slack());
  acc->min_buffer_count() = std::max(acc->min_buffer_count(), c.min_buffer_count());

  // 0 is replaced with 0xFFFFFFFF in
  // CheckSanitizeBufferCollectionConstraints.
  ZX_DEBUG_ASSERT(acc->max_buffer_count() != 0);
  ZX_DEBUG_ASSERT(c.max_buffer_count() != 0);
  acc->max_buffer_count() = std::min(acc->max_buffer_count(), c.max_buffer_count());

  // CheckSanitizeBufferCollectionConstraints() takes care of setting a default
  // buffer_collection_constraints, so we can assert that both acc and c "has_" one.
  ZX_DEBUG_ASSERT(acc->has_buffer_memory_constraints());
  ZX_DEBUG_ASSERT(c.has_buffer_memory_constraints());
  if (!AccumulateConstraintBufferMemory(&acc->buffer_memory_constraints(),
                                        std::move(c.buffer_memory_constraints()))) {
    return false;
  }

  if (!acc->image_format_constraints().count()) {
    // Take the whole VectorView<>, as the count() can only go down later, so the capacity of
    // c.image_format_constraints() is fine.
    acc->set_image_format_constraints(table_set_.allocator(),
                                      std::move(c.image_format_constraints()));
  } else {
    ZX_DEBUG_ASSERT(acc->image_format_constraints().count());
    if (c.image_format_constraints().count()) {
      if (!AccumulateConstraintImageFormats(&acc->image_format_constraints(),
                                            std::move(c.image_format_constraints()))) {
        // We return false if we've seen non-zero
        // image_format_constraint_count from at least one participant
        // but among non-zero image_format_constraint_count participants
        // since then the overlap has dropped to empty set.
        //
        // This path is taken when there are completely non-overlapping
        // PixelFormats and also when PixelFormat(s) overlap but none
        // of those have any non-empty settings space remaining.  In
        // that case we've removed the PixelFormat from consideration
        // despite it being common among participants (so far).
        return false;
      }
      ZX_DEBUG_ASSERT(acc->image_format_constraints().count());
    }
  }

  acc->need_clear_aux_buffers_for_secure() =
      acc->need_clear_aux_buffers_for_secure() || c.need_clear_aux_buffers_for_secure();
  acc->allow_clear_aux_buffers_for_secure() =
      acc->allow_clear_aux_buffers_for_secure() && c.allow_clear_aux_buffers_for_secure();
  // We check for consistency of these later only if we're actually attempting to allocate secure
  // buffers.

  // acc->image_format_constraints().count() == 0 is allowed here, when all
  // participants had image_format_constraints().count() == 0.
  return true;
}

bool LogicalBufferCollection::AccumulateConstraintHeapPermitted(
    fidl::VectorView<fuchsia_sysmem2::wire::HeapType>* acc,
    fidl::VectorView<fuchsia_sysmem2::wire::HeapType> c) {
  // Remove any heap in acc that's not in c.  If zero heaps
  // remain in acc, return false.
  ZX_DEBUG_ASSERT(acc->count() > 0);

  for (uint32_t ai = 0; ai < acc->count(); ++ai) {
    uint32_t ci;
    for (ci = 0; ci < c.count(); ++ci) {
      if ((*acc)[ai] == c[ci]) {
        // We found heap in c.  Break so we can move on to
        // the next heap.
        break;
      }
    }
    if (ci == c.count()) {
      // Remove from acc because not found in c.
      //
      // Copy formerly last item on top of the item being removed, if not the same item.
      if (ai != acc->count() - 1) {
        (*acc)[ai] = (*acc)[acc->count() - 1];
      }
      // remove last item
      acc->set_count(acc->count() - 1);
      // adjust ai to force current index to be processed again as it's
      // now a different item
      --ai;
    }
  }

  if (!acc->count()) {
    LogError(FROM_HERE, "Zero heap permitted overlap");
    return false;
  }

  return true;
}

bool LogicalBufferCollection::AccumulateConstraintBufferMemory(
    fuchsia_sysmem2::wire::BufferMemoryConstraints* acc,
    fuchsia_sysmem2::wire::BufferMemoryConstraints c) {
  acc->min_size_bytes() = std::max(acc->min_size_bytes(), c.min_size_bytes());

  // Don't permit 0 as the overall min_size_bytes; that would be nonsense.  No
  // particular initiator should feel that it has to specify 1 in this field;
  // that's just built into sysmem instead.  While a VMO will have a minimum
  // actual size of page size, we do permit treating buffers as if they're 1
  // byte, mainly for testing reasons, and to avoid any unnecessary dependence
  // or assumptions re. page size.
  acc->min_size_bytes() = std::max(acc->min_size_bytes(), 1u);
  acc->max_size_bytes() = std::min(acc->max_size_bytes(), c.max_size_bytes());

  acc->physically_contiguous_required() =
      acc->physically_contiguous_required() || c.physically_contiguous_required();

  acc->secure_required() = acc->secure_required() || c.secure_required();

  acc->ram_domain_supported() = acc->ram_domain_supported() && c.ram_domain_supported();
  acc->cpu_domain_supported() = acc->cpu_domain_supported() && c.cpu_domain_supported();
  acc->inaccessible_domain_supported() =
      acc->inaccessible_domain_supported() && c.inaccessible_domain_supported();

  if (!acc->heap_permitted().count()) {
    acc->set_heap_permitted(table_set_.allocator(), std::move(c.heap_permitted()));
  } else {
    if (c.heap_permitted().count()) {
      if (!AccumulateConstraintHeapPermitted(&acc->heap_permitted(),
                                             std::move(c.heap_permitted()))) {
        return false;
      }
    }
  }
  return true;
}

bool LogicalBufferCollection::AccumulateConstraintImageFormats(
    fidl::VectorView<fuchsia_sysmem2::wire::ImageFormatConstraints>* acc,
    fidl::VectorView<fuchsia_sysmem2::wire::ImageFormatConstraints> c) {
  // Remove any pixel_format in acc that's not in c.  Process any format
  // that's in both.  If processing the format results in empty set for that
  // format, pretend as if the format wasn't in c and remove that format from
  // acc.  If acc ends up with zero formats, return false.

  // This method doesn't get called unless there's at least one format in
  // acc.
  ZX_DEBUG_ASSERT(acc->count());

  for (uint32_t ai = 0; ai < acc->count(); ++ai) {
    bool is_found_in_c = false;
    for (size_t ci = 0; ci < c.count(); ++ci) {
      if (ImageFormatIsPixelFormatEqual((*acc)[ai].pixel_format(), c[ci].pixel_format())) {
        // Move last entry into the entry we're consuming, since LLCPP FIDL tables don't have any
        // way to detect that they've been moved out of, so we need to keep c tightly packed with
        // not-moved-out-of entries.  We don't need to adjust ci to stay at the same entry for the
        // next iteration of the loop because by this point we know we're done scanning c in this
        // iteration of the ai loop.
        fuchsia_sysmem2::wire::ImageFormatConstraints old_c_ci = std::move(c[ci]);
        if (ci != c.count() - 1) {
          c[ci] = std::move(c[c.count() - 1]);
        }
        c.set_count(c.count() - 1);
        if (!AccumulateConstraintImageFormat(&(*acc)[ai], std::move(old_c_ci))) {
          // Pretend like the format wasn't in c to begin with, so
          // this format gets removed from acc.  Only if this results
          // in zero formats in acc do we end up returning false.
          ZX_DEBUG_ASSERT(!is_found_in_c);
          break;
        }
        // We found the format in c and processed the format without
        // that resulting in empty set; break so we can move on to the
        // next format.
        is_found_in_c = true;
        break;
      }
    }
    if (!is_found_in_c) {
      // Remove from acc because not found in c.
      //
      // Move last item on top of the item being removed, if not the same item.
      if (ai != acc->count() - 1) {
        (*acc)[ai] = std::move((*acc)[acc->count() - 1]);
      } else {
        // Stuff under this item would get deleted later anyway, but delete now to avoid keeping
        // cruft we don't need.
        (*acc)[ai] = fuchsia_sysmem2::wire::ImageFormatConstraints();
      }
      // remove last item
      acc->set_count(acc->count() - 1);
      // adjust ai to force current index to be processed again as it's
      // now a different item
      --ai;
    }
  }

  if (!acc->count()) {
    // It's ok for this check to be under Accumulate* because it's permitted
    // for a given participant to have zero image_format_constraints_count.
    // It's only when the count becomes non-zero then drops back to zero
    // (checked here), or if we end up with no image format constraints and
    // no buffer constraints (checked in ::Allocate()), that we care.
    LogError(FROM_HERE, "all pixel_format(s) eliminated");
    return false;
  }

  return true;
}

bool LogicalBufferCollection::AccumulateConstraintImageFormat(
    fuchsia_sysmem2::wire::ImageFormatConstraints* acc,
    fuchsia_sysmem2::wire::ImageFormatConstraints c) {
  ZX_DEBUG_ASSERT(ImageFormatIsPixelFormatEqual(acc->pixel_format(), c.pixel_format()));
  // Checked previously.
  ZX_DEBUG_ASSERT(acc->color_spaces().count());
  // Checked previously.
  ZX_DEBUG_ASSERT(c.color_spaces().count());

  if (!AccumulateConstraintColorSpaces(&acc->color_spaces(), std::move(c.color_spaces()))) {
    return false;
  }
  // Else AccumulateConstraintColorSpaces() would have returned false.
  ZX_DEBUG_ASSERT(acc->color_spaces().count());

  acc->min_coded_width() = std::max(acc->min_coded_width(), c.min_coded_width());
  acc->max_coded_width() = std::min(acc->max_coded_width(), c.max_coded_width());
  acc->min_coded_height() = std::max(acc->min_coded_height(), c.min_coded_height());
  acc->max_coded_height() = std::min(acc->max_coded_height(), c.max_coded_height());
  acc->min_bytes_per_row() = std::max(acc->min_bytes_per_row(), c.min_bytes_per_row());
  acc->max_bytes_per_row() = std::min(acc->max_bytes_per_row(), c.max_bytes_per_row());
  acc->max_coded_width_times_coded_height() =
      std::min(acc->max_coded_width_times_coded_height(), c.max_coded_width_times_coded_height());

  acc->coded_width_divisor() = std::max(acc->coded_width_divisor(), c.coded_width_divisor());
  acc->coded_width_divisor() =
      std::max(acc->coded_width_divisor(), ImageFormatCodedWidthMinDivisor(acc->pixel_format()));

  acc->coded_height_divisor() = std::max(acc->coded_height_divisor(), c.coded_height_divisor());
  acc->coded_height_divisor() =
      std::max(acc->coded_height_divisor(), ImageFormatCodedHeightMinDivisor(acc->pixel_format()));

  acc->bytes_per_row_divisor() = std::max(acc->bytes_per_row_divisor(), c.bytes_per_row_divisor());
  acc->bytes_per_row_divisor() =
      std::max(acc->bytes_per_row_divisor(), ImageFormatSampleAlignment(acc->pixel_format()));

  acc->start_offset_divisor() = std::max(acc->start_offset_divisor(), c.start_offset_divisor());
  acc->start_offset_divisor() =
      std::max(acc->start_offset_divisor(), ImageFormatSampleAlignment(acc->pixel_format()));

  acc->display_width_divisor() = std::max(acc->display_width_divisor(), c.display_width_divisor());
  acc->display_height_divisor() =
      std::max(acc->display_height_divisor(), c.display_height_divisor());

  // The required_ space is accumulated by taking the union, and must be fully
  // within the non-required_ space, else fail.  For example, this allows a
  // video decoder to indicate that it's capable of outputting a wide range of
  // output dimensions, but that it has specific current dimensions that are
  // presently required_ (min == max) for decode to proceed.
  ZX_DEBUG_ASSERT(acc->required_min_coded_width() != 0);
  ZX_DEBUG_ASSERT(c.required_min_coded_width() != 0);
  acc->required_min_coded_width() =
      std::min(acc->required_min_coded_width(), c.required_min_coded_width());
  acc->required_max_coded_width() =
      std::max(acc->required_max_coded_width(), c.required_max_coded_width());
  ZX_DEBUG_ASSERT(acc->required_min_coded_height() != 0);
  ZX_DEBUG_ASSERT(c.required_min_coded_height() != 0);
  acc->required_min_coded_height() =
      std::min(acc->required_min_coded_height(), c.required_min_coded_height());
  acc->required_max_coded_height() =
      std::max(acc->required_max_coded_height(), c.required_max_coded_height());
  ZX_DEBUG_ASSERT(acc->required_min_bytes_per_row() != 0);
  ZX_DEBUG_ASSERT(c.required_min_bytes_per_row() != 0);
  acc->required_min_bytes_per_row() =
      std::min(acc->required_min_bytes_per_row(), c.required_min_bytes_per_row());
  acc->required_max_bytes_per_row() =
      std::max(acc->required_max_bytes_per_row(), c.required_max_bytes_per_row());

  return true;
}

bool LogicalBufferCollection::AccumulateConstraintColorSpaces(
    fidl::VectorView<fuchsia_sysmem2::wire::ColorSpace>* acc,
    fidl::VectorView<fuchsia_sysmem2::wire::ColorSpace> c) {
  // Remove any color_space in acc that's not in c.  If zero color spaces
  // remain in acc, return false.

  for (uint32_t ai = 0; ai < acc->count(); ++ai) {
    uint32_t ci;
    for (ci = 0; ci < c.count(); ++ci) {
      if (IsColorSpaceEqual((*acc)[ai], c[ci])) {
        // We found the color space in c.  Break so we can move on to
        // the next color space.
        break;
      }
    }
    if (ci == c.count()) {
      // Remove from acc because not found in c.
      //
      // Move formerly last item on top of the item being removed, if not same item.
      if (ai != acc->count() - 1) {
        (*acc)[ai] = std::move((*acc)[acc->count() - 1]);
      } else {
        // Stuff under this item would get deleted later anyway, but delete now to avoid keeping
        // cruft we don't need.
        (*acc)[ai] = fuchsia_sysmem2::wire::ColorSpace();
      }
      // remove last item
      acc->set_count(acc->count() - 1);
      // adjust ai to force current index to be processed again as it's
      // now a different item
      --ai;
    }
  }

  if (!acc->count()) {
    // It's ok for this check to be under Accumulate* because it's also
    // under CheckSanitize().  It's fine to provide a slightly more helpful
    // error message here and early out here.
    LogError(FROM_HERE, "Zero color_space overlap");
    return false;
  }

  return true;
}

bool LogicalBufferCollection::IsColorSpaceEqual(const fuchsia_sysmem2::wire::ColorSpace& a,
                                                const fuchsia_sysmem2::wire::ColorSpace& b) {
  return a.type() == b.type();
}

static fit::result<fuchsia_sysmem2::wire::HeapType, zx_status_t> GetHeap(
    const fuchsia_sysmem2::wire::BufferMemoryConstraints& constraints, Device* device) {
  if (constraints.secure_required()) {
    // TODO(fxbug.dev/37452): Generalize this.
    //
    // checked previously
    ZX_DEBUG_ASSERT(!constraints.secure_required() || IsSecurePermitted(constraints));
    if (IsHeapPermitted(constraints, fuchsia_sysmem2::wire::HeapType::AMLOGIC_SECURE)) {
      return fit::ok(fuchsia_sysmem2::wire::HeapType::AMLOGIC_SECURE);
    } else {
      ZX_DEBUG_ASSERT(
          IsHeapPermitted(constraints, fuchsia_sysmem2::wire::HeapType::AMLOGIC_SECURE_VDEC));
      return fit::ok(fuchsia_sysmem2::wire::HeapType::AMLOGIC_SECURE_VDEC);
    }
  }
  if (IsHeapPermitted(constraints, fuchsia_sysmem2::wire::HeapType::SYSTEM_RAM)) {
    return fit::ok(fuchsia_sysmem2::wire::HeapType::SYSTEM_RAM);
  }

  for (size_t i = 0; i < constraints.heap_permitted().count(); ++i) {
    auto heap = constraints.heap_permitted()[i];
    const auto& heap_properties = device->GetHeapProperties(heap);
    if (heap_properties.has_coherency_domain_support() &&
        ((heap_properties.coherency_domain_support().cpu_supported() &&
          constraints.cpu_domain_supported()) ||
         (heap_properties.coherency_domain_support().ram_supported() &&
          constraints.ram_domain_supported()) ||
         (heap_properties.coherency_domain_support().inaccessible_supported() &&
          constraints.inaccessible_domain_supported()))) {
      return fit::ok(heap);
    }
  }
  return fit::error(ZX_ERR_NOT_FOUND);
}

static fit::result<fuchsia_sysmem2::wire::CoherencyDomain> GetCoherencyDomain(
    const fuchsia_sysmem2::wire::BufferCollectionConstraints& constraints,
    MemoryAllocator* memory_allocator) {
  ZX_DEBUG_ASSERT(constraints.has_buffer_memory_constraints());

  using fuchsia_sysmem2::wire::CoherencyDomain;
  const auto& heap_properties = memory_allocator->heap_properties();
  ZX_DEBUG_ASSERT(heap_properties.has_coherency_domain_support());

  // Display prefers RAM coherency domain for now.
  if (constraints.usage().display() != 0) {
    if (constraints.buffer_memory_constraints().ram_domain_supported()) {
      // Display controllers generally aren't cache coherent, so prefer
      // RAM coherency domain.
      //
      // TODO - base on the system in use.
      return fit::ok(fuchsia_sysmem2::wire::CoherencyDomain::RAM);
    }
  }

  if (heap_properties.coherency_domain_support().cpu_supported() &&
      constraints.buffer_memory_constraints().cpu_domain_supported()) {
    return fit::ok(CoherencyDomain::CPU);
  }

  if (heap_properties.coherency_domain_support().ram_supported() &&
      constraints.buffer_memory_constraints().ram_domain_supported()) {
    return fit::ok(CoherencyDomain::RAM);
  }

  if (heap_properties.coherency_domain_support().inaccessible_supported() &&
      constraints.buffer_memory_constraints().inaccessible_domain_supported()) {
    // Intentionally permit treating as Inaccessible if we reach here, even
    // if the heap permits CPU access.  Only domain in common among
    // participants is Inaccessible.
    return fit::ok(fuchsia_sysmem2::wire::CoherencyDomain::INACCESSIBLE);
  }

  return fit::error();
}

fit::result<fuchsia_sysmem2::wire::BufferCollectionInfo, zx_status_t>
LogicalBufferCollection::GenerateUnpopulatedBufferCollectionInfo(
    const fuchsia_sysmem2::wire::BufferCollectionConstraints& constraints) {
  TRACE_DURATION("gfx", "LogicalBufferCollection:GenerateUnpopulatedBufferCollectionInfo", "this",
                 this);

  fidl::AnyAllocator& fidl_allocator = table_set_.allocator();

  fuchsia_sysmem2::wire::BufferCollectionInfo result(fidl_allocator);

  uint32_t min_buffer_count = constraints.min_buffer_count_for_camping() +
                              constraints.min_buffer_count_for_dedicated_slack() +
                              constraints.min_buffer_count_for_shared_slack();
  min_buffer_count = std::max(min_buffer_count, constraints.min_buffer_count());
  uint32_t max_buffer_count = constraints.max_buffer_count();
  if (min_buffer_count > max_buffer_count) {
    LogError(FROM_HERE,
             "aggregate min_buffer_count > aggregate max_buffer_count - "
             "min: %u max: %u",
             min_buffer_count, max_buffer_count);
    return fit::error(ZX_ERR_NOT_SUPPORTED);
  }
  if (min_buffer_count > fuchsia_sysmem::wire::MAX_COUNT_BUFFER_COLLECTION_INFO_BUFFERS) {
    LogError(FROM_HERE,
             "aggregate min_buffer_count (%d) > MAX_COUNT_BUFFER_COLLECTION_INFO_BUFFERS (%d)",
             min_buffer_count, fuchsia_sysmem::wire::MAX_COUNT_BUFFER_COLLECTION_INFO_BUFFERS);
    return fit::error(ZX_ERR_NOT_SUPPORTED);
  }

  result.set_buffers(fidl_allocator, fidl_allocator, min_buffer_count);
  ZX_DEBUG_ASSERT(result.buffers().count() == min_buffer_count);
  ZX_DEBUG_ASSERT(result.buffers().count() <= max_buffer_count);

  uint64_t min_size_bytes = 0;
  uint64_t max_size_bytes = std::numeric_limits<uint64_t>::max();

  result.set_settings(fidl_allocator, fidl_allocator);
  fuchsia_sysmem2::wire::SingleBufferSettings& settings = result.settings();
  settings.set_buffer_settings(fidl_allocator, fidl_allocator);
  fuchsia_sysmem2::wire::BufferMemorySettings& buffer_settings = settings.buffer_settings();

  ZX_DEBUG_ASSERT(constraints.has_buffer_memory_constraints());
  const fuchsia_sysmem2::wire::BufferMemoryConstraints& buffer_constraints =
      constraints.buffer_memory_constraints();
  buffer_settings.set_is_physically_contiguous(fidl_allocator,
                                               buffer_constraints.physically_contiguous_required());
  // checked previously
  ZX_DEBUG_ASSERT(IsSecurePermitted(buffer_constraints) || !buffer_constraints.secure_required());
  buffer_settings.set_is_secure(fidl_allocator, buffer_constraints.secure_required());
  if (buffer_settings.is_secure()) {
    if (constraints.need_clear_aux_buffers_for_secure() &&
        !constraints.allow_clear_aux_buffers_for_secure()) {
      LogError(
          FROM_HERE,
          "is_secure && need_clear_aux_buffers_for_secure && !allow_clear_aux_buffers_for_secure");
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }
  }

  auto result_get_heap = GetHeap(buffer_constraints, parent_device_);
  if (!result_get_heap.is_ok()) {
    LogError(FROM_HERE, "Can not find a heap permitted by buffer constraints, error %d",
             result_get_heap.error());
    return fit::error(result_get_heap.error());
  }
  buffer_settings.set_heap(fidl_allocator, result_get_heap.value());

  // We can't fill out buffer_settings yet because that also depends on
  // ImageFormatConstraints.  We do need the min and max from here though.
  min_size_bytes = buffer_constraints.min_size_bytes();
  max_size_bytes = buffer_constraints.max_size_bytes();

  // Get memory allocator for settings.
  MemoryAllocator* allocator = parent_device_->GetAllocator(buffer_settings);
  if (!allocator) {
    LogError(FROM_HERE, "No memory allocator for buffer settings");
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  auto coherency_domain_result = GetCoherencyDomain(constraints, allocator);
  if (!coherency_domain_result.is_ok()) {
    LogError(FROM_HERE, "No coherency domain found for buffer constraints");
    return fit::error(ZX_ERR_NOT_SUPPORTED);
  }
  buffer_settings.set_coherency_domain(fidl_allocator, coherency_domain_result.value());

  // It's allowed for zero participants to have any ImageFormatConstraint(s),
  // in which case the combined constraints_ will have zero (and that's fine,
  // when allocating raw buffers that don't need any ImageFormatConstraint).
  //
  // At least for now, we pick which PixelFormat to use before determining if
  // the constraints associated with that PixelFormat imply a buffer size
  // range in min_size_bytes..max_size_bytes.
  if (constraints.image_format_constraints().count()) {
    // Pick the best ImageFormatConstraints.
    uint32_t best_index = UINT32_MAX;
    bool found_unsupported_when_protected = false;
    for (uint32_t i = 0; i < constraints.image_format_constraints().count(); ++i) {
      if (buffer_settings.is_secure() &&
          !ImageFormatCompatibleWithProtectedMemory(
              constraints.image_format_constraints()[i].pixel_format())) {
        found_unsupported_when_protected = true;
        continue;
      }
      if (best_index == UINT32_MAX) {
        best_index = i;
      } else {
        if (CompareImageFormatConstraintsByIndex(constraints, i, best_index) < 0) {
          best_index = i;
        }
      }
    }
    if (best_index == UINT32_MAX) {
      ZX_DEBUG_ASSERT(found_unsupported_when_protected);
      LogError(FROM_HERE, "No formats were compatible with protected memory.");
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }
    // clone from constraints to settings.
    settings.set_image_format_constraints(
        fidl_allocator,
        sysmem::V2CloneImageFormatConstraints(table_set_.allocator(),
                                              constraints.image_format_constraints()[best_index]));
  }

  // Compute the min buffer size implied by image_format_constraints, so we ensure the buffers can
  // hold the min-size image.
  if (settings.has_image_format_constraints()) {
    const fuchsia_sysmem2::wire::ImageFormatConstraints& image_format_constraints =
        settings.image_format_constraints();
    fuchsia_sysmem2::wire::ImageFormat min_image(fidl_allocator);
    min_image.set_pixel_format(
        fidl_allocator,
        sysmem::V2ClonePixelFormat(fidl_allocator, image_format_constraints.pixel_format()));
    // We use required_max_coded_width because that's the max width that the producer (or
    // initiator) wants these buffers to be able to hold.
    min_image.set_coded_width(fidl_allocator,
                              AlignUp(std::max(image_format_constraints.min_coded_width(),
                                               image_format_constraints.required_max_coded_width()),
                                      image_format_constraints.coded_width_divisor()));
    if (min_image.coded_width() > image_format_constraints.max_coded_width()) {
      LogError(FROM_HERE, "coded_width_divisor caused coded_width > max_coded_width");
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }
    // We use required_max_coded_height because that's the max height that the producer (or
    // initiator) wants these buffers to be able to hold.
    min_image.set_coded_height(
        fidl_allocator, AlignUp(std::max(image_format_constraints.min_coded_height(),
                                         image_format_constraints.required_max_coded_height()),
                                image_format_constraints.coded_height_divisor()));
    if (min_image.coded_height() > image_format_constraints.max_coded_height()) {
      LogError(FROM_HERE, "coded_height_divisor caused coded_height > max_coded_height");
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }
    min_image.set_bytes_per_row(
        fidl_allocator,
        AlignUp(
            std::max(image_format_constraints.min_bytes_per_row(),
                     ImageFormatStrideBytesPerWidthPixel(image_format_constraints.pixel_format()) *
                         min_image.coded_width()),
            image_format_constraints.bytes_per_row_divisor()));
    if (min_image.bytes_per_row() > image_format_constraints.max_bytes_per_row()) {
      LogError(FROM_HERE,
               "bytes_per_row_divisor caused bytes_per_row > "
               "max_bytes_per_row");
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }

    if (min_image.coded_width() * min_image.coded_height() >
        image_format_constraints.max_coded_width_times_coded_height()) {
      LogError(FROM_HERE,
               "coded_width * coded_height > "
               "max_coded_width_times_coded_height");
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }

    // These don't matter for computing size in bytes.
    ZX_DEBUG_ASSERT(!min_image.has_display_width());
    ZX_DEBUG_ASSERT(!min_image.has_display_height());

    // Checked previously.
    ZX_DEBUG_ASSERT(image_format_constraints.color_spaces().count() >= 1);
    // This doesn't matter for computing size in bytes, as we trust the pixel_format to fully
    // specify the image size.  But set it to the first ColorSpace anyway, just so the
    // color_space.type is a valid value.
    min_image.set_color_space(
        fidl_allocator,
        sysmem::V2CloneColorSpace(fidl_allocator, image_format_constraints.color_spaces()[0]));

    uint64_t image_min_size_bytes = ImageFormatImageSize(min_image);

    if (image_min_size_bytes > min_size_bytes) {
      if (image_min_size_bytes > max_size_bytes) {
        LogError(FROM_HERE, "image_min_size_bytes > max_size_bytes");
        return fit::error(ZX_ERR_NOT_SUPPORTED);
      }
      min_size_bytes = image_min_size_bytes;
      ZX_DEBUG_ASSERT(min_size_bytes <= max_size_bytes);
    }
  }

  // Currently redundant with earlier checks, but just in case...
  if (min_size_bytes == 0) {
    LogError(FROM_HERE, "min_size_bytes == 0");
    return fit::error(ZX_ERR_NOT_SUPPORTED);
  }
  ZX_DEBUG_ASSERT(min_size_bytes != 0);

  // For purposes of enforcing max_size_bytes, we intentionally don't care that a VMO can only be a
  // multiple of page size.

  uint64_t total_size_bytes = min_size_bytes * result.buffers().count();
  if (total_size_bytes > kMaxTotalSizeBytesPerCollection) {
    LogError(FROM_HERE, "total_size_bytes > kMaxTotalSizeBytesPerCollection");
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  if (min_size_bytes > kMaxSizeBytesPerBuffer) {
    LogError(FROM_HERE, "min_size_bytes > kMaxSizeBytesPerBuffer");
    return fit::error(ZX_ERR_NO_MEMORY);
  }
  ZX_DEBUG_ASSERT(min_size_bytes <= std::numeric_limits<uint32_t>::max());

  // Now that min_size_bytes accounts for any ImageFormatConstraints, we can just allocate
  // min_size_bytes buffers.
  //
  // If an initiator (or a participant) wants to force buffers to be larger than the size implied by
  // minimum image dimensions, the initiator can use BufferMemorySettings.min_size_bytes to force
  // allocated buffers to be large enough.
  buffer_settings.set_size_bytes(table_set_.allocator(), static_cast<uint32_t>(min_size_bytes));

  if (buffer_settings.size_bytes() > parent_device_->settings().max_allocation_size) {
    // This is different than max_size_bytes.  While max_size_bytes is part of the constraints,
    // max_allocation_size isn't part of the constraints.  The latter is used for simulating OOM or
    // preventing unpredictable memory pressure caused by a fuzzer or similar source of
    // unpredictability in tests.
    LogError(FROM_HERE,
             "GenerateUnpopulatedBufferCollectionInfo() failed because size %u > "
             "max_allocation_size %ld",
             buffer_settings.size_bytes(), parent_device_->settings().max_allocation_size);
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  // We initially set vmo_usable_start to bit-fields indicating whether vmo and aux_vmo fields will
  // be set to valid handles later.  This is for purposes of comparison with a later
  // BufferCollectionInfo after an AttachToken().  Before sending to the client, the
  // vmo_usable_start is set to 0.  Even if later we need a non-zero vmo_usable_start to be compared
  // we are extremely unlikely to want a buffer to start at an offset that isn't divisible by 4, so
  // using the two low-order bits for this seems reasonable enough.
  for (uint32_t i = 0; i < result.buffers().count(); ++i) {
    auto vmo_buffer = fuchsia_sysmem2::wire::VmoBuffer(table_set_.allocator());
    vmo_buffer.set_vmo_usable_start(table_set_.allocator(), 0ul);
    if (buffer_settings.is_secure() && constraints.need_clear_aux_buffers_for_secure()) {
      vmo_buffer.vmo_usable_start() |= kNeedAuxVmoAlso;
    }
    result.buffers()[i] = std::move(vmo_buffer);
  }

  return fit::ok(std::move(result));
}

fit::result<fuchsia_sysmem2::wire::BufferCollectionInfo, zx_status_t>
LogicalBufferCollection::Allocate(
    const fuchsia_sysmem2::wire::BufferCollectionConstraints& constraints,
    fuchsia_sysmem2::wire::BufferCollectionInfo* builder) {
  TRACE_DURATION("gfx", "LogicalBufferCollection:Allocate", "this", this);

  fuchsia_sysmem2::wire::BufferCollectionInfo& result = *builder;

  fuchsia_sysmem2::wire::SingleBufferSettings& settings = result.settings();
  fuchsia_sysmem2::wire::BufferMemorySettings& buffer_settings = settings.buffer_settings();

  // Get memory allocator for settings.
  MemoryAllocator* allocator = parent_device_->GetAllocator(buffer_settings);
  if (!allocator) {
    LogError(FROM_HERE, "No memory allocator for buffer settings");
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  if (settings.has_image_format_constraints()) {
    const fuchsia_sysmem2::wire::ImageFormatConstraints& image_format_constraints =
        settings.image_format_constraints();
    inspect_node_.CreateUint("pixel_format",
                             static_cast<uint64_t>(image_format_constraints.pixel_format().type()),
                             &vmo_properties_);
    if (image_format_constraints.pixel_format().has_format_modifier_value()) {
      inspect_node_.CreateUint("pixel_format_modifier",
                               image_format_constraints.pixel_format().format_modifier_value(),
                               &vmo_properties_);
    }
    if (image_format_constraints.min_coded_width() > 0) {
      inspect_node_.CreateUint("min_coded_width", image_format_constraints.min_coded_width(),
                               &vmo_properties_);
    }
    if (image_format_constraints.min_coded_height() > 0) {
      inspect_node_.CreateUint("min_coded_height", image_format_constraints.min_coded_height(),
                               &vmo_properties_);
    }
    if (image_format_constraints.required_max_coded_width() > 0) {
      inspect_node_.CreateUint("required_max_coded_width",
                               image_format_constraints.required_max_coded_width(),
                               &vmo_properties_);
    }
    if (image_format_constraints.required_max_coded_height() > 0) {
      inspect_node_.CreateUint("required_max_coded_height",
                               image_format_constraints.required_max_coded_height(),
                               &vmo_properties_);
    }
  }

  inspect_node_.CreateUint("allocator_id", allocator->id(), &vmo_properties_);
  inspect_node_.CreateUint("size_bytes", buffer_settings.size_bytes(), &vmo_properties_);
  inspect_node_.CreateUint("heap", static_cast<uint64_t>(buffer_settings.heap()), &vmo_properties_);

  // Get memory allocator for aux buffers, if needed.
  MemoryAllocator* maybe_aux_allocator = nullptr;
  std::optional<fuchsia_sysmem2::wire::SingleBufferSettings> maybe_aux_settings;
  ZX_DEBUG_ASSERT(!!(result.buffers()[0].vmo_usable_start() & kNeedAuxVmoAlso) ==
                  (buffer_settings.is_secure() && constraints.need_clear_aux_buffers_for_secure()));
  if (result.buffers()[0].vmo_usable_start() & kNeedAuxVmoAlso) {
    maybe_aux_settings.emplace(fuchsia_sysmem2::wire::SingleBufferSettings(table_set_.allocator()));
    maybe_aux_settings->set_buffer_settings(table_set_.allocator(), table_set_.allocator());
    auto& aux_buffer_settings = maybe_aux_settings->buffer_settings();
    aux_buffer_settings.set_size_bytes(table_set_.allocator(), buffer_settings.size_bytes());
    aux_buffer_settings.set_is_physically_contiguous(table_set_.allocator(), false);
    aux_buffer_settings.set_is_secure(table_set_.allocator(), false);
    aux_buffer_settings.set_coherency_domain(table_set_.allocator(),
                                             fuchsia_sysmem2::wire::CoherencyDomain::CPU);
    aux_buffer_settings.set_heap(table_set_.allocator(),
                                 fuchsia_sysmem2::wire::HeapType::SYSTEM_RAM);
    maybe_aux_allocator = parent_device_->GetAllocator(aux_buffer_settings);
    ZX_DEBUG_ASSERT(maybe_aux_allocator);
  }

  ZX_DEBUG_ASSERT(buffer_settings.size_bytes() <= parent_device_->settings().max_allocation_size);

  for (uint32_t i = 0; i < result.buffers().count(); ++i) {
    auto allocate_result = AllocateVmo(allocator, settings, i);
    if (!allocate_result.is_ok()) {
      LogError(FROM_HERE, "AllocateVmo() failed");
      return fit::error(ZX_ERR_NO_MEMORY);
    }
    zx::vmo vmo = allocate_result.take_value();
    auto& vmo_buffer = result.buffers()[i];
    vmo_buffer.set_vmo(table_set_.allocator(), std::move(vmo));
    if (maybe_aux_allocator) {
      ZX_DEBUG_ASSERT(maybe_aux_settings);
      auto aux_allocate_result = AllocateVmo(maybe_aux_allocator, maybe_aux_settings.value(), i);
      if (!aux_allocate_result.is_ok()) {
        LogError(FROM_HERE, "AllocateVmo() failed (aux)");
        return fit::error(ZX_ERR_NO_MEMORY);
      }
      zx::vmo aux_vmo = aux_allocate_result.take_value();
      vmo_buffer.set_aux_vmo(table_set_.allocator(), std::move(aux_vmo));
    }
    ZX_DEBUG_ASSERT(vmo_buffer.has_vmo_usable_start());
    // In case kNeedAuxVmoAlso was set.
    vmo_buffer.vmo_usable_start() = 0;
  }
  vmo_count_property_ = inspect_node_.CreateUint("vmo_count", result.buffers().count());
  // Make sure we have sufficient barrier after allocating/clearing/flushing any VMO newly allocated
  // by allocator above.
  BarrierAfterFlush();

  // Register failure handler with memory allocator.
  allocator->AddDestroyCallback(reinterpret_cast<intptr_t>(this), [this]() {
    LogAndFailRootNode(FROM_HERE, ZX_ERR_BAD_STATE,
                       "LogicalBufferCollection memory allocator gone - now auto-failing self.");
  });
  memory_allocator_ = allocator;

  return fit::ok(std::move(result));
}

fit::result<zx::vmo> LogicalBufferCollection::AllocateVmo(
    MemoryAllocator* allocator, const fuchsia_sysmem2::wire::SingleBufferSettings& settings,
    uint32_t index) {
  TRACE_DURATION("gfx", "LogicalBufferCollection::AllocateVmo", "size_bytes",
                 settings.buffer_settings().size_bytes());
  zx::vmo child_vmo;
  // Physical VMOs only support slices where the size (and offset) are page_size aligned,
  // so we should also round up when allocating.
  size_t rounded_size_bytes = fbl::round_up(settings.buffer_settings().size_bytes(), ZX_PAGE_SIZE);
  if (rounded_size_bytes < settings.buffer_settings().size_bytes()) {
    LogError(FROM_HERE, "size_bytes overflows when rounding to multiple of page_size");
    return fit::error();
  }

  // raw_vmo may itself be a child VMO of an allocator's overall contig VMO,
  // but that's an internal detail of the allocator.  The ZERO_CHILDREN signal
  // will only be set when all direct _and indirect_ child VMOs are fully
  // gone (not just handles closed, but the kernel object is deleted, which
  // avoids races with handle close, and means there also aren't any
  // mappings left).
  zx::vmo raw_parent_vmo;
  std::optional<std::string> name;
  if (name_) {
    name = fbl::StringPrintf("%s:%d", name_->name.c_str(), index).c_str();
  }
  zx_status_t status = allocator->Allocate(rounded_size_bytes, name, &raw_parent_vmo);
  if (status != ZX_OK) {
    LogError(FROM_HERE,
             "allocator.Allocate failed - size_bytes: %zu "
             "status: %d",
             rounded_size_bytes, status);
    return fit::error();
  }

  zx_info_vmo_t info;
  status = raw_parent_vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    LogError(FROM_HERE, "raw_parent_vmo.get_info(ZX_INFO_VMO) failed - status %d", status);
    return fit::error();
  }

  auto node = inspect_node_.CreateChild(fbl::StringPrintf("vmo-%ld", info.koid).c_str());
  node.CreateUint("koid", info.koid, &vmo_properties_);
  vmo_properties_.emplace(std::move(node));

  // Write zeroes to the VMO, so that the allocator doesn't need to.  Also flush those zeroes to
  // RAM so the newly-allocated VMO is fully zeroed in both RAM and CPU coherency domains.
  //
  // This is measured to be significantly more than half the overall time cost when repeatedly
  // allocating and deallocating a buffer collection with 4MiB buffer space per collection.  On
  // astro this was measured to be ~2100us out of ~2550us per-cycle duration.  Larger buffer space
  // per collection would take longer here.
  //
  // If we find this is taking too long, we could ask the allocator if it's already providing
  // pre-zeroed VMOs.  And/or zero allocator backing space async during deallocation, but wait on
  // deallocations to be done before failing an new allocation.
  //
  // TODO(fxbug.dev/34590): Zero secure/protected VMOs.
  const auto& heap_properties = allocator->heap_properties();
  ZX_DEBUG_ASSERT(heap_properties.has_coherency_domain_support());
  ZX_DEBUG_ASSERT(heap_properties.has_need_clear());
  if (heap_properties.need_clear()) {
    uint64_t offset = 0;
    while (offset < info.size_bytes) {
      uint64_t bytes_to_write = std::min(sizeof(kZeroes), info.size_bytes - offset);
      // TODO(fxbug.dev/59796): Use ZX_VMO_OP_ZERO instead.
      status = raw_parent_vmo.write(kZeroes, offset, bytes_to_write);
      if (status != ZX_OK) {
        LogError(FROM_HERE, "raw_parent_vmo.write() failed - status: %d", status);
        return fit::error();
      }
      offset += bytes_to_write;
    }
    // Flush out the zeroes.
    status = raw_parent_vmo.op_range(ZX_VMO_OP_CACHE_CLEAN, 0, info.size_bytes, nullptr, 0);
    if (status != ZX_OK) {
      LogError(FROM_HERE, "raw_parent_vmo.op_range(ZX_VMO_OP_CACHE_CLEAN) failed - status: %d",
               status);
      return fit::error();
    }
  }

  // We immediately create the ParentVmo instance so it can take care of calling allocator.Delete()
  // if this method returns early.  We intentionally don't emplace into parent_vmos_ until
  // StartWait() has succeeded.  In turn, StartWait() requires a child VMO to have been created
  // already (else ZX_VMO_ZERO_CHILDREN would trigger too soon).
  //
  // We need to keep the raw_parent_vmo around so we can wait for ZX_VMO_ZERO_CHILDREN, and so we
  // can call allocator.Delete(raw_parent_vmo).
  //
  // Until that happens, we can't let LogicalBufferCollection itself go away, because it needs to
  // stick around to tell allocator that the allocator's VMO can be deleted/reclaimed.
  //
  // We let cooked_parent_vmo go away before returning from this method, since it's only purpose
  // was to attenuate the rights of local_child_vmo.  The local_child_vmo counts as a child of
  // raw_parent_vmo for ZX_VMO_ZERO_CHILDREN.
  //
  // The fbl::RefPtr(this) is fairly similar (in this usage) to shared_from_this().
  auto tracked_parent_vmo = std::unique_ptr<TrackedParentVmo>(new TrackedParentVmo(
      fbl::RefPtr(this), std::move(raw_parent_vmo),
      [this, allocator](TrackedParentVmo* tracked_parent_vmo) mutable {
        auto node_handle = parent_vmos_.extract(tracked_parent_vmo->vmo().get());
        ZX_DEBUG_ASSERT(!node_handle || node_handle.mapped().get() == tracked_parent_vmo);
        allocator->Delete(tracked_parent_vmo->TakeVmo());
        // ~node_handle may delete "this".
      }));

  zx::vmo cooked_parent_vmo;
  status = tracked_parent_vmo->vmo().duplicate(kSysmemVmoRights, &cooked_parent_vmo);
  if (status != ZX_OK) {
    LogError(FROM_HERE, "zx::object::duplicate() failed - status: %d", status);
    return fit::error();
  }

  zx::vmo local_child_vmo;
  status =
      cooked_parent_vmo.create_child(ZX_VMO_CHILD_SLICE, 0, rounded_size_bytes, &local_child_vmo);
  if (status != ZX_OK) {
    LogError(FROM_HERE, "zx::vmo::create_child() failed - status: %d", status);
    return fit::error();
  }

  zx_info_handle_basic_t child_info{};
  local_child_vmo.get_info(ZX_INFO_HANDLE_BASIC, &child_info, sizeof(child_info), nullptr, nullptr);
  tracked_parent_vmo->set_child_koid(child_info.koid);
  TRACE_INSTANT("gfx", "Child VMO created", TRACE_SCOPE_THREAD, "koid", child_info.koid);

  // Now that we know at least one child of raw_parent_vmo exists, we can StartWait() and add to
  // map.  From this point, ZX_VMO_ZERO_CHILDREN is the only way that allocator.Delete() gets
  // called.
  status = tracked_parent_vmo->StartWait(parent_device_->dispatcher());
  if (status != ZX_OK) {
    LogError(FROM_HERE, "tracked_parent->StartWait() failed - status: %d", status);
    // ~tracked_parent_vmo calls allocator.Delete().
    return fit::error();
  }
  zx_handle_t raw_parent_vmo_handle = tracked_parent_vmo->vmo().get();
  TrackedParentVmo& parent_vmo_ref = *tracked_parent_vmo;
  auto emplace_result = parent_vmos_.emplace(raw_parent_vmo_handle, std::move(tracked_parent_vmo));
  ZX_DEBUG_ASSERT(emplace_result.second);

  // Now inform the allocator about the child VMO before we return it.
  status = allocator->SetupChildVmo(
      parent_vmo_ref.vmo(), local_child_vmo,
      sysmem::V2CloneSingleBufferSettings(table_set_.allocator(), settings));
  if (status != ZX_OK) {
    LogError(FROM_HERE, "allocator.SetupChildVmo() failed - status: %d", status);
    // In this path, the ~local_child_vmo will async trigger parent_vmo_ref::OnZeroChildren()
    // which will call allocator.Delete() via above do_delete lambda passed to
    // ParentVmo::ParentVmo().
    return fit::error();
  }
  if (name) {
    local_child_vmo.set_property(ZX_PROP_NAME, name->c_str(), name->size());
  }

  // ~cooked_parent_vmo is fine, since local_child_vmo counts as a child of raw_parent_vmo for
  // ZX_VMO_ZERO_CHILDREN purposes.
  return fit::ok(std::move(local_child_vmo));
}

void LogicalBufferCollection::CreationTimedOut(async_dispatcher_t* dispatcher,
                                               async::TaskBase* task, zx_status_t status) {
  if (status != ZX_OK)
    return;

  // It's possible for the timer to fire after the root_ has been deleted, but before "this" has
  // been deleted (which also cancels the timer if it's still pending).  The timer doesn't need to
  // take any action in this case.
  if (!root_) {
    return;
  }

  std::string name = name_ ? name_->name : "Unknown";

  LogError(FROM_HERE, "Allocation of %s timed out. Waiting for tokens: ", name.c_str());
  ZX_DEBUG_ASSERT(root_);
  auto token_nodes = root_->BreadthFirstOrder([](const NodeProperties& node) {
    ZX_DEBUG_ASSERT(node.node());
    bool potentially_included_in_initial_allocation =
        IsPotentiallyIncludedInInitialAllocation(node);
    return NodeFilterResult{.keep_node = potentially_included_in_initial_allocation &&
                                         !!node.node()->buffer_collection_token(),
                            .iterate_children = potentially_included_in_initial_allocation};
  });
  for (auto node_properties : token_nodes) {
    if (!node_properties->client_debug_info().name.empty()) {
      LogError(FROM_HERE, "Name %s id %ld", node_properties->client_debug_info().name.c_str(),
               node_properties->client_debug_info().id);
    } else {
      LogError(FROM_HERE, "Unknown token");
    }
  }

  LogError(FROM_HERE, "Collections:");
  auto collection_nodes = root_->BreadthFirstOrder([](const NodeProperties& node) {
    ZX_DEBUG_ASSERT(node.node());
    bool potentially_included_in_initial_allocation =
        IsPotentiallyIncludedInInitialAllocation(node);
    return NodeFilterResult{.keep_node = potentially_included_in_initial_allocation &&
                                         !!node.node()->buffer_collection(),
                            .iterate_children = potentially_included_in_initial_allocation};
  });
  for (auto node_properties : collection_nodes) {
    const char* constraints_state =
        node_properties->node()->buffer_collection()->has_constraints() ? "set" : "unset";
    if (!node_properties->client_debug_info().name.empty()) {
      LogError(FROM_HERE, "Name \"%s\" id %ld (constraints %s)",
               node_properties->client_debug_info().name.c_str(),
               node_properties->client_debug_info().id, constraints_state);
    } else {
      LogError(FROM_HERE, "Name unknown (constraints %s)", constraints_state);
    }
  }
}

static int32_t clamp_difference(int32_t a, int32_t b) {
  int32_t raw_result = a - b;

  int32_t cooked_result = raw_result;
  if (cooked_result > 0) {
    cooked_result = 1;
  } else if (cooked_result < 0) {
    cooked_result = -1;
  }
  ZX_DEBUG_ASSERT(cooked_result == 0 || cooked_result == 1 || cooked_result == -1);
  return cooked_result;
}

// 1 means a > b, 0 means ==, -1 means a < b.
//
// TODO(dustingreen): Pay attention to constraints_->usage, by checking any
// overrides that prefer particular PixelFormat based on a usage / usage
// combination.
int32_t LogicalBufferCollection::CompareImageFormatConstraintsTieBreaker(
    const fuchsia_sysmem2::wire::ImageFormatConstraints& a,
    const fuchsia_sysmem2::wire::ImageFormatConstraints& b) {
  // If there's not any cost difference, fall back to choosing the
  // pixel_format that has the larger type enum value as a tie-breaker.

  int32_t result = clamp_difference(static_cast<int32_t>(a.pixel_format().type()),
                                    static_cast<int32_t>(b.pixel_format().type()));

  if (result != 0)
    return result;

  result = clamp_difference(static_cast<int32_t>(a.pixel_format().has_format_modifier_value()),
                            static_cast<int32_t>(b.pixel_format().has_format_modifier_value()));

  if (result != 0)
    return result;

  if (a.pixel_format().has_format_modifier_value() &&
      b.pixel_format().has_format_modifier_value()) {
    result = clamp_difference(static_cast<int32_t>(a.pixel_format().format_modifier_value()),
                              static_cast<int32_t>(b.pixel_format().format_modifier_value()));
  }

  return result;
}

int32_t LogicalBufferCollection::CompareImageFormatConstraintsByIndex(
    const fuchsia_sysmem2::wire::BufferCollectionConstraints& constraints, uint32_t index_a,
    uint32_t index_b) {
  int32_t cost_compare = UsagePixelFormatCost::Compare(parent_device_->pdev_device_info_vid(),
                                                       parent_device_->pdev_device_info_pid(),
                                                       constraints, index_a, index_b);
  if (cost_compare != 0) {
    return cost_compare;
  }

  // If we get this far, there's no known reason to choose one PixelFormat
  // over another, so just pick one based on a tie-breaker that'll distinguish
  // between PixelFormat(s).

  int32_t tie_breaker_compare =
      CompareImageFormatConstraintsTieBreaker(constraints.image_format_constraints()[index_a],
                                              constraints.image_format_constraints()[index_b]);
  return tie_breaker_compare;
}

LogicalBufferCollection::TrackedParentVmo::TrackedParentVmo(
    fbl::RefPtr<LogicalBufferCollection> buffer_collection, zx::vmo vmo,
    LogicalBufferCollection::TrackedParentVmo::DoDelete do_delete)
    : buffer_collection_(std::move(buffer_collection)),
      vmo_(std::move(vmo)),
      do_delete_(std::move(do_delete)),
      zero_children_wait_(this, vmo_.get(), ZX_VMO_ZERO_CHILDREN) {
  ZX_DEBUG_ASSERT(buffer_collection_);
  ZX_DEBUG_ASSERT(vmo_);
  ZX_DEBUG_ASSERT(do_delete_);
}

LogicalBufferCollection::TrackedParentVmo::~TrackedParentVmo() {
  // We avoid relying on LogicalBufferCollection member destruction order by cancelling explicitly
  // before the end of ~LogicalBufferCollection, so we're never waiting_ by this point.
  ZX_DEBUG_ASSERT(!waiting_);
  if (do_delete_) {
    do_delete_(this);
  }
}

zx_status_t LogicalBufferCollection::TrackedParentVmo::StartWait(async_dispatcher_t* dispatcher) {
  LogInfo(FROM_HERE, "LogicalBufferCollection::TrackedParentVmo::StartWait()");
  // The current thread is the dispatcher thread.
  ZX_DEBUG_ASSERT(!waiting_);
  zx_status_t status = zero_children_wait_.Begin(dispatcher);
  if (status != ZX_OK) {
    LogErrorStatic(FROM_HERE, nullptr, "zero_children_wait_.Begin() failed - status: %d", status);
    return status;
  }
  waiting_ = true;
  return ZX_OK;
}

zx_status_t LogicalBufferCollection::TrackedParentVmo::CancelWait() {
  waiting_ = false;
  return zero_children_wait_.Cancel();
}

zx::vmo LogicalBufferCollection::TrackedParentVmo::TakeVmo() {
  ZX_DEBUG_ASSERT(!waiting_);
  ZX_DEBUG_ASSERT(vmo_);
  return std::move(vmo_);
}

const zx::vmo& LogicalBufferCollection::TrackedParentVmo::vmo() const {
  ZX_DEBUG_ASSERT(vmo_);
  return vmo_;
}

void LogicalBufferCollection::TrackedParentVmo::OnZeroChildren(async_dispatcher_t* dispatcher,
                                                               async::WaitBase* wait,
                                                               zx_status_t status,
                                                               const zx_packet_signal_t* signal) {
  TRACE_DURATION("gfx", "LogicalBufferCollection::TrackedParentVmo::OnZeroChildren",
                 "buffer_collection", buffer_collection_.get(), "child_koid", child_koid_);
  LogInfo(FROM_HERE, "LogicalBufferCollection::TrackedParentVmo::OnZeroChildren()");
  ZX_DEBUG_ASSERT(waiting_);
  waiting_ = false;
  if (status == ZX_ERR_CANCELED) {
    // The collection canceled all of these waits as part of destruction, do nothing.
    return;
  }
  ZX_DEBUG_ASSERT(status == ZX_OK);
  ZX_DEBUG_ASSERT(signal->trigger & ZX_VMO_ZERO_CHILDREN);
  ZX_DEBUG_ASSERT(do_delete_);
  LogicalBufferCollection::TrackedParentVmo::DoDelete local_do_delete = std::move(do_delete_);
  ZX_DEBUG_ASSERT(!do_delete_);
  // will delete "this"
  local_do_delete(this);
  ZX_DEBUG_ASSERT(!local_do_delete);
}

void LogicalBufferCollection::AddCountsForNode(const Node& node) {
  AdjustRelevantNodeCounts(node, [](uint32_t& count) { ++count; });
}

void LogicalBufferCollection::RemoveCountsForNode(const Node& node) {
  AdjustRelevantNodeCounts(node, [](uint32_t& count) { --count; });
}

void LogicalBufferCollection::AdjustRelevantNodeCounts(
    const Node& node, fit::function<void(uint32_t& count)> visitor) {
  for (NodeProperties* iter = &node.node_properties(); iter; iter = iter->parent()) {
    visitor(iter->node_count_);
    if (node.is_connected()) {
      visitor(iter->connected_client_count_);
    }
    if (node.buffer_collection()) {
      visitor(iter->buffer_collection_count_);
    }
    if (node.buffer_collection_token()) {
      visitor(iter->buffer_collection_token_count_);
    }
  }
}

// Only for use by NodeProperties.
void LogicalBufferCollection::DeleteRoot() { root_.reset(); }

NodeProperties* LogicalBufferCollection::FindTreeToFail(NodeProperties* failing_node) {
  ZX_DEBUG_ASSERT(failing_node);
  for (NodeProperties* iter = failing_node; iter; iter = iter->parent()) {
    if (!iter->parent()) {
      LogClientInfo(FROM_HERE, iter, "The root should fail.");
      return iter;
    }
    NodeProperties* parent = iter->parent();
    ErrorPropagationMode mode = iter->error_propagation_mode();
    switch (mode) {
      case ErrorPropagationMode::kPropagate:
        // keep propagating the failure upward
        LogClientInfo(FROM_HERE, iter, "Propagate node failure to parent because kPropagate");
        continue;
      case ErrorPropagationMode::kPropagateBeforeAllocation:
        // Propagate failure if before allocation.  We also know in this case that parent and child
        // will allocate together.
        ZX_DEBUG_ASSERT(parent->buffers_logically_allocated() ==
                        iter->buffers_logically_allocated());
        if (!iter->buffers_logically_allocated()) {
          LogClientInfo(FROM_HERE, iter,
                        "Propagate node failure to parent because kPropagateBeforeAllocation and "
                        "!BuffersLogicallyAllocated");
          continue;
        } else {
          LogClientInfo(FROM_HERE, iter,
                        "Do not propagate node failure to parent because "
                        "kPropagateBeforeAllocation and BuffersLogicallyAllocated");
          return iter;
        }
      case ErrorPropagationMode::kDoNotPropagate:
        LogClientInfo(FROM_HERE, iter,
                      "Do not propagate node failure to parent because kDoNotPropagate");
        return iter;
      default:
        ZX_PANIC("unreachable 1");
        return nullptr;
    }
  }
  ZX_PANIC("unreachable 2");
  return nullptr;
}

// For tests.
std::vector<const BufferCollection*> LogicalBufferCollection::collection_views() const {
  std::vector<const BufferCollection*> result;
  if (!root_) {
    return result;
  }
  auto nodes = root_->BreadthFirstOrder();
  for (auto node_properties : nodes) {
    if (node_properties->node()->buffer_collection()) {
      result.push_back(node_properties->node()->buffer_collection());
    }
  }
  return result;
}

}  // namespace sysmem_driver
