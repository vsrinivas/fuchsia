// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logical_buffer_collection.h"

#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/image-format/image_format.h>
#include <lib/sysmem-make-tracking/make_tracking.h>
#include <lib/sysmem-version/sysmem-version.h>
#include <limits.h>  // PAGE_SIZE
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <limits>  // std::numeric_limits

#include <ddk/trace/event.h>
#include <fbl/algorithm.h>
#include <fbl/string_printf.h>

#include "buffer_collection.h"
#include "buffer_collection_token.h"
#include "device.h"
#include "koid_util.h"
#include "macros.h"
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
#define FIELD_DEFAULT_1(builder_ptr_name, field_name)                                              \
  do {                                                                                             \
    auto builder_ptr = (builder_ptr_name);                                                         \
    static_assert(fidl::IsTableBuilder<std::remove_pointer<decltype(builder_ptr)>::type>::value);  \
    using FieldType = std::remove_reference<decltype((builder_ptr->field_name()))>::type;          \
    if (!builder_ptr->has_##field_name()) {                                                        \
      builder_ptr->set_##field_name(sysmem::MakeTracking(&allocator_, static_cast<FieldType>(1))); \
      ZX_DEBUG_ASSERT(builder_ptr->field_name() == 1);                                             \
    }                                                                                              \
    ZX_DEBUG_ASSERT(builder_ptr->has_##field_name());                                              \
  } while (false)

// TODO(fxbug.dev/50590): It'd be nice if this could be a function template over FIDL scalar field
// types.
#define FIELD_DEFAULT_MAX(builder_ptr_name, field_name)                                           \
  do {                                                                                            \
    auto builder_ptr = (builder_ptr_name);                                                        \
    static_assert(fidl::IsTableBuilder<std::remove_pointer<decltype(builder_ptr)>::type>::value); \
    using FieldType = std::remove_reference<decltype((builder_ptr->field_name()))>::type;         \
    if (!builder_ptr->has_##field_name()) {                                                       \
      builder_ptr->set_##field_name(                                                              \
          sysmem::MakeTracking(&allocator_, std::numeric_limits<FieldType>::max()));              \
      ZX_DEBUG_ASSERT(builder_ptr->field_name() == std::numeric_limits<FieldType>::max());        \
    }                                                                                             \
    ZX_DEBUG_ASSERT(builder_ptr->has_##field_name());                                             \
  } while (false)

// TODO(fxbug.dev/50590): It'd be nice if this could be a function template over FIDL scalar field
// types.
#define FIELD_DEFAULT_ZERO(builder_ptr_name, field_name)                                           \
  do {                                                                                             \
    auto builder_ptr = (builder_ptr_name);                                                         \
    static_assert(fidl::IsTableBuilder<std::remove_pointer_t<decltype(builder_ptr)>>::value);      \
    using FieldType = std::remove_reference<decltype((builder_ptr->field_name()))>::type;          \
    if (!builder_ptr->has_##field_name()) {                                                        \
      builder_ptr->set_##field_name(sysmem::MakeTracking(&allocator_, static_cast<FieldType>(0))); \
      ZX_DEBUG_ASSERT(!static_cast<bool>(builder_ptr->field_name()));                              \
    }                                                                                              \
    ZX_DEBUG_ASSERT(builder_ptr->has_##field_name());                                              \
  } while (false)

#define FIELD_DEFAULT_FALSE(builder_ptr_name, field_name)                                     \
  do {                                                                                        \
    auto builder_ptr = (builder_ptr_name);                                                    \
    static_assert(fidl::IsTableBuilder<std::remove_pointer_t<decltype(builder_ptr)>>::value); \
    using FieldType = std::remove_reference<decltype((builder_ptr->field_name()))>::type;     \
    static_assert(std::is_same<FieldType, bool>::value);                                      \
    if (!builder_ptr->has_##field_name()) {                                                   \
      builder_ptr->set_##field_name(sysmem::MakeTracking(&allocator_, false));                \
      ZX_DEBUG_ASSERT(!builder_ptr->field_name());                                            \
    }                                                                                         \
    ZX_DEBUG_ASSERT(builder_ptr->has_##field_name());                                         \
  } while (false)

#define FIELD_DEFAULT(builder_ptr_name, field_name, value_name)                               \
  do {                                                                                        \
    auto builder_ptr = (builder_ptr_name);                                                    \
    static_assert(fidl::IsTableBuilder<std::remove_pointer_t<decltype(builder_ptr)>>::value); \
    using FieldType = std::remove_reference<decltype((builder_ptr->field_name()))>::type;     \
    static_assert(!fidl::IsFidlObject<FieldType>::value);                                     \
    static_assert(!fidl::IsVectorView<FieldType>::value);                                     \
    static_assert(!fidl::IsStringView<FieldType>::value);                                     \
    if (!builder_ptr->has_##field_name()) {                                                   \
      auto field_value = (value_name);                                                        \
      builder_ptr->set_##field_name(sysmem::MakeTracking(&allocator_, field_value));          \
      ZX_DEBUG_ASSERT(builder_ptr->field_name() == field_value);                              \
    }                                                                                         \
    ZX_DEBUG_ASSERT(builder_ptr->has_##field_name());                                         \
  } while (false)

template <typename FieldRefType, typename Enable = void>
struct FieldDefaultCreator : std::false_type {};
template <typename TableType>
struct FieldDefaultCreator<TableType, std::enable_if_t<fidl::IsTable<TableType>::value>> {
  static auto Create(fidl::Allocator* allocator) {
    return sysmem::MakeTracking<TableType>(allocator);
  }
};
template <typename VectorItemType>
struct FieldDefaultCreator<fidl::VectorView<VectorItemType>, void> {
  static auto Create(fidl::Allocator* allocator, size_t count, size_t capacity) {
    return sysmem::MakeTracking<VectorItemType[]>(allocator, count, capacity);
  }
};

#define FIELD_DEFAULT_SET(builder_ptr_name, field_name)                                       \
  do {                                                                                        \
    auto builder_ptr = (builder_ptr_name);                                                    \
    static_assert(fidl::IsTableBuilder<std::remove_pointer_t<decltype(builder_ptr)>>::value); \
    using TableType = std::remove_reference_t<decltype((builder_ptr->field_name()))>;         \
    static_assert(fidl::IsTable<TableType>::value);                                           \
    if (!builder_ptr->has_##field_name()) {                                                   \
      builder_ptr->set_##field_name(                                                          \
          sysmem::MakeTracking(&allocator_, allocator_.make_table_builder<TableType>()));     \
    }                                                                                         \
    ZX_DEBUG_ASSERT(builder_ptr->has_##field_name());                                         \
  } while (false)

// regardless of capacity, initial count is always 0
#define FIELD_DEFAULT_SET_VECTOR(builder_ptr_name, field_name, capacity_param)                \
  do {                                                                                        \
    auto builder_ptr = (builder_ptr_name);                                                    \
    static_assert(fidl::IsTableBuilder<std::remove_pointer_t<decltype(builder_ptr)>>::value); \
    using VectorFieldType = std::remove_reference_t<decltype((builder_ptr->field_name()))>;   \
    static_assert(fidl::IsVectorView<VectorFieldType>::value);                                \
    using ElementType = typename VectorFieldType::elem_type;                                  \
    if (!builder_ptr->has_##field_name()) {                                                   \
      size_t capacity = (capacity_param);                                                     \
      builder_ptr->set_##field_name(allocator_.make_vec_ptr<ElementType>(0, capacity));       \
    }                                                                                         \
    ZX_DEBUG_ASSERT(builder_ptr->has_##field_name());                                         \
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

}  // namespace

// static
void LogicalBufferCollection::Create(zx::channel buffer_collection_token_request,
                                     Device* parent_device) {
  fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection =
      fbl::AdoptRef<LogicalBufferCollection>(new LogicalBufferCollection(parent_device));
  // The existence of a channel-owned BufferCollectionToken adds a
  // fbl::RefPtr<> ref to LogicalBufferCollection.
  LogInfo(FROM_HERE, "LogicalBufferCollection::Create()");
  logical_buffer_collection->CreateBufferCollectionToken(
      logical_buffer_collection, std::numeric_limits<uint32_t>::max(),
      std::move(buffer_collection_token_request), nullptr);
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
                                                   const ClientInfo* client_info) {
  ZX_DEBUG_ASSERT(buffer_collection_token);
  ZX_DEBUG_ASSERT(buffer_collection_request);

  zx_koid_t token_client_koid;
  zx_koid_t token_server_koid;
  zx_status_t status =
      get_channel_koids(buffer_collection_token, &token_client_koid, &token_server_koid);
  if (status != ZX_OK) {
    LogErrorStatic(FROM_HERE, client_info, "Failed to get channel koids");
    // ~buffer_collection_token
    // ~buffer_collection_request
    return;
  }

  BufferCollectionToken* token = parent_device->FindTokenByServerChannelKoid(token_server_koid);
  if (!token) {
    // The most likely scenario for why the token was not found is that Sync() was not called on
    // either the BufferCollectionToken or the BufferCollection.
    LogErrorStatic(FROM_HERE, client_info,
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

  if (client_info) {
    // The info will be propagated into the logcial buffer collection when the token closes.
    token->SetDebugClientInfo(client_info->name, client_info->id);
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
    fbl::RefPtr<LogicalBufferCollection> self, uint32_t rights_attenuation_mask,
    zx::channel buffer_collection_token_request, const ClientInfo* client_info) {
  ZX_DEBUG_ASSERT(buffer_collection_token_request.get());
  auto token = BufferCollectionToken::Create(parent_device_, self, rights_attenuation_mask);
  token->SetErrorHandler([this, token_ptr = token.get()](zx_status_t status) {
    // Clean close from FIDL channel point of view is ZX_ERR_PEER_CLOSED,
    // and ZX_OK is never passed to the error handler.
    ZX_DEBUG_ASSERT(status != ZX_OK);

    // The dispatcher shut down before we were able to Bind(...)
    if (status == ZX_ERR_BAD_STATE) {
      LogAndFail(FROM_HERE, "sysmem dispatcher shutting down - status: %d", status);
      return;
    }

    // We know |this| is alive because the token is alive and the token has
    // a fbl::RefPtr<LogicalBufferCollection>.  The token is alive because
    // the token is still in token_views_.
    //
    // Any other deletion of the token_ptr out of token_views_ (outside of
    // this error handler) doesn't run this error handler.
    //
    // TODO(dustingreen): Switch to contains() when C++20.
    ZX_DEBUG_ASSERT(token_views_.find(token_ptr) != token_views_.end());

    zx::channel buffer_collection_request = token_ptr->TakeBufferCollectionRequest();

    if (!(status == ZX_ERR_PEER_CLOSED && (token_ptr->is_done() || buffer_collection_request))) {
      // We don't have to explicitly remove token from token_views_
      // because Fail() will token_views_.clear().
      //
      // A token whose error handler sees anything other than clean close
      // with is_done() implies LogicalBufferCollection failure.  The
      // ability to detect unexpected closure of a token is a main reason
      // we use a channel for BufferCollectionToken instead of an
      // eventpair.
      //
      // If a participant for some reason finds itself with an extra token it doesn't need, the
      // participant should use Close() to avoid triggering this failure.
      LogAndFail(FROM_HERE, "Token failure causing LogicalBufferCollection failure - status: %d",
                 status);
      return;
    }

    // At this point we know the token channel was closed cleanly, and that
    // before the client's closing the channel, the client did a
    // token::Close() or allocator::BindSharedCollection().
    ZX_DEBUG_ASSERT(status == ZX_ERR_PEER_CLOSED &&
                    (token_ptr->is_done() || buffer_collection_request));
    // BufferCollectionToken enforces that these never both become true; the
    // BufferCollectionToken will fail instead.
    ZX_DEBUG_ASSERT(!(token_ptr->is_done() && buffer_collection_request));

    if (!buffer_collection_request) {
      // This was a token::Close().  In this case we want to stop tracking the token now that we've
      // processed all its previously-queued inbound messages.  This might be the last token, so we
      // MaybeAllocate().  This path isn't a failure (unless there are also zero BufferCollection
      // views in which case MaybeAllocate() calls Fail()).
      auto self = token_ptr->parent_shared();
      ZX_DEBUG_ASSERT(self.get() == this);
      token_views_.erase(token_ptr);
      MaybeAllocate();
      // ~self may delete "this"
    } else {
      // At this point we know that this was a BindSharedCollection().  We
      // need to convert the BufferCollectionToken into a BufferCollection.
      //
      // ~token_ptr during this call
      BindSharedCollectionInternal(token_ptr, std::move(buffer_collection_request));
    }
  });
  auto token_ptr = token.get();
  token_views_.insert({token_ptr, std::move(token)});

  zx_koid_t server_koid;
  zx_koid_t client_koid;
  zx_status_t status =
      get_channel_koids(buffer_collection_token_request, &server_koid, &client_koid);
  if (status != ZX_OK) {
    LogAndFail(FROM_HERE, "get_channel_koids() failed - status: %d", status);
    return;
  }
  token_ptr->SetServerKoid(server_koid);
  if (token_ptr->was_unfound_token()) {
    LogClientError(FROM_HERE, client_info,
                   "BufferCollectionToken.Duplicate() received for creating token with server koid"
                   "%ld after BindSharedCollection() previously received attempting to use same"
                   "token.  Client sequence should be Duplicate(), Sync(), BindSharedCollection()."
                   "Missing Sync()?",
                   server_koid);
  }

  LogInfo(FROM_HERE, "CreateBufferCollectionToken() - server_koid: %lu", token_ptr->server_koid());
  token_ptr->Bind(std::move(buffer_collection_token_request));
}

void LogicalBufferCollection::OnSetConstraints() {
  MaybeAllocate();
  return;
}

void LogicalBufferCollection::SetName(uint32_t priority, std::string name) {
  if (!name_ || (priority > name_->priority)) {
    name_ = CollectionName{priority, name};
    name_property_ = node_.CreateString("name", name);
  }
}

void LogicalBufferCollection::SetDebugTimeoutLogDeadline(int64_t deadline) {
  creation_timer_.Cancel();
  zx_status_t status =
      creation_timer_.PostForTime(parent_device_->dispatcher(), zx::time(deadline));
  ZX_ASSERT(status == ZX_OK);
}

LogicalBufferCollection::AllocationResult LogicalBufferCollection::allocation_result() {
  ZX_DEBUG_ASSERT(has_allocation_result_ ||
                  (allocation_result_status_ == ZX_OK && !allocation_result_info_));
  // If this assert fails, it mean we've already done ::Fail().  This should be impossible since
  // Fail() clears all BufferCollection views so they shouldn't be able to call
  // ::allocation_result().
  ZX_DEBUG_ASSERT(
      !(has_allocation_result_ && allocation_result_status_ == ZX_OK && !allocation_result_info_));
  return {
      .buffer_collection_info =
          allocation_result_info_ ? &allocation_result_info_.value() : nullptr,
      .status = allocation_result_status_,
  };
}

LogicalBufferCollection::LogicalBufferCollection(Device* parent_device)
    : parent_device_(parent_device) {
  TRACE_DURATION("gfx", "LogicalBufferCollection::LogicalBufferCollection", "this", this);
  LogInfo(FROM_HERE, "LogicalBufferCollection::LogicalBufferCollection()");
  parent_device_->AddLogicalBufferCollection(this);
  node_ = parent_device_->collections_node().CreateChild(CreateUniqueName("logical-collection-"));

  zx_status_t status = creation_timer_.PostDelayed(parent_device_->dispatcher(), zx::sec(5));
  ZX_ASSERT(status == ZX_OK);
  // nothing else to do here
}

LogicalBufferCollection::~LogicalBufferCollection() {
  TRACE_DURATION("gfx", "LogicalBufferCollection::~LogicalBufferCollection", "this", this);
  LogInfo(FROM_HERE, "~LogicalBufferCollection");
  // Every entry in these collections keeps a
  // fbl::RefPtr<LogicalBufferCollection>, so these should both already be
  // empty.
  ZX_DEBUG_ASSERT(token_views_.empty());
  ZX_DEBUG_ASSERT(collection_views_.empty());

  // Cancel all TrackedParentVmo waits to avoid a use-after-free of |this|
  for (auto& tracked : parent_vmos_) {
    tracked.second->CancelWait();
  }

  if (memory_allocator_) {
    memory_allocator_->RemoveDestroyCallback(reinterpret_cast<intptr_t>(this));
  }
  parent_device_->RemoveLogicalBufferCollection(this);

  // LOG(INFO, "~LogicalBufferCollection allocator_.debug_needed_buffer_size(): %zu\n",
  //    allocator_.inner_allocator().debug_needed_buffer_size());
}

void LogicalBufferCollection::LogAndFail(Location location, const char* format, ...) {
  ZX_DEBUG_ASSERT(format);
  va_list args;
  va_start(args, format);
  vLog(true, location.file(), location.line(), "LogicalBufferCollection", "fail", format, args);
  va_end(args);
  Fail();
}

void LogicalBufferCollection::Fail() {
  // Close all the associated channels.  We do this by swapping into local
  // collections and clearing those, since deleting the items in the
  // collections will delete |this|.
  TokenMap local_token_views;
  token_views_.swap(local_token_views);
  CollectionMap local_collection_views;
  collection_views_.swap(local_collection_views);

  // Since all the token views and collection views will shortly be gone, there
  // will be no way for any client to be sent the VMOs again, so we can close
  // the handles to the VMOs here.  This is necessary in order to get
  // ZX_VMO_ZERO_CHILDREN to happen in TrackedParentVmo, but not sufficient
  // alone (clients must also close their VMO(s)).
  //
  // We can't just allocation_result_info_.reset() here, because we're using a
  // BufferThenHeapAllocator<> that'll delay close of the VMOs until during
  // ~LogicalBufferCollection (a deadlock) unless we dig into the structure and
  // close these VMOs directly.
  if (allocation_result_info_) {
    for (uint32_t i = 0; i < allocation_result_info_->buffers().count(); ++i) {
      if (allocation_result_info_->buffers()[i].has_vmo()) {
        allocation_result_info_->buffers()[i].vmo().reset();
      }
      if (allocation_result_info_->buffers()[i].has_aux_vmo()) {
        allocation_result_info_->buffers()[i].aux_vmo().reset();
      }
    }
  }
  allocation_result_info_.reset();

  // |this| can be deleted during these calls to clear(), unless parent_vmos_
  // isn't empty yet, or unless the caller of Fail() has its own temporary
  // fbl::RefPtr<LogicalBufferCollection> on the stack.
  //
  // These clear() calls will close the channels, which in turn will inform
  // the participants to close their child VMO handles.  We don't revoke the
  // child VMOs, so the LogicalBufferCollection will stick around until
  // parent_vmo_map_ becomes empty thanks to participants closing their child
  // VMOs.
  local_token_views.clear();
  local_collection_views.clear();
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

void LogicalBufferCollection::LogErrorStatic(Location location, const ClientInfo* client_info,
                                             const char* format, ...) {
  va_list args;
  va_start(args, format);
  fbl::String formatted = fbl::StringVPrintf(format, args);
  if (client_info && !client_info->name.empty()) {
    fbl::String client_name =
        fbl::StringPrintf(" - client \"%s\" id %ld", client_info->name.c_str(), client_info->id);

    formatted = fbl::String::Concat({formatted, client_name});
  }
  LogErrorInternal(location, "%s", formatted.c_str());
  va_end(args);
}

void LogicalBufferCollection::VLogClientError(Location location, const ClientInfo* client_info,
                                              const char* format, va_list args) {
  const char* collection_name = name_ ? name_->name.c_str() : "Unknown";
  fbl::String formatted = fbl::StringVPrintf(format, args);
  if (client_info && !client_info->name.empty()) {
    fbl::String client_name =
        fbl::StringPrintf(" - collection \"%s\" - client \"%s\" id %ld", collection_name,
                          client_info->name.c_str(), client_info->id);

    formatted = fbl::String::Concat({formatted, client_name});
  } else {
    fbl::String client_name = fbl::StringPrintf(" - collection \"%s\"", collection_name);

    formatted = fbl::String::Concat({formatted, client_name});
  }
  LogErrorInternal(location, "%s", formatted.c_str());
  va_end(args);
}

void LogicalBufferCollection::LogClientError(Location location, const ClientInfo* client_info,
                                             const char* format, ...) {
  va_list args;
  va_start(args, format);
  VLogClientError(location, client_info, format, args);
  va_end(args);
}

void LogicalBufferCollection::LogError(Location location, const char* format, ...) {
  va_list args;
  va_start(args, format);
  VLogError(location, format, args);
  va_end(args);
}

void LogicalBufferCollection::VLogError(Location location, const char* format, va_list args) {
  VLogClientError(location, current_client_info_, format, args);
}

void LogicalBufferCollection::InitializeConstraintSnapshots(
    const ConstraintsList& constraints_list) {
  ZX_DEBUG_ASSERT(constraints_at_allocation_.empty());
  ZX_DEBUG_ASSERT(!constraints_list.empty());
  for (auto& constraints : constraints_list) {
    ConstraintInfoSnapshot snapshot;
    snapshot.node = node().CreateChild(CreateUniqueName("collection-at-allocation-"));
    if (constraints.builder.has_min_buffer_count_for_camping()) {
      snapshot.node.CreateUint("min_buffer_count_for_camping",
                               constraints.builder.min_buffer_count_for_camping(),
                               &snapshot.node_constraints);
    }
    if (constraints.builder.has_min_buffer_count_for_shared_slack()) {
      snapshot.node.CreateUint("min_buffer_count_for_shared_slack",
                               constraints.builder.min_buffer_count_for_shared_slack(),
                               &snapshot.node_constraints);
    }
    if (constraints.builder.has_min_buffer_count_for_dedicated_slack()) {
      snapshot.node.CreateUint("min_buffer_count_for_dedicated_slack",
                               constraints.builder.min_buffer_count_for_dedicated_slack(),
                               &snapshot.node_constraints);
    }
    if (constraints.builder.has_min_buffer_count()) {
      snapshot.node.CreateUint("min_buffer_count", constraints.builder.min_buffer_count(),
                               &snapshot.node_constraints);
    }
    snapshot.node.CreateUint("debug_id", constraints.client.id, &snapshot.node_constraints);
    snapshot.node.CreateString("debug_name", constraints.client.name, &snapshot.node_constraints);
    constraints_at_allocation_.push_back(std::move(snapshot));
  }
}

void LogicalBufferCollection::MaybeAllocate() {
  if (!token_views_.empty()) {
    // All tokens must be converted into BufferCollection views or Close()ed
    // before allocation will happen.
    return;
  }
  if (collection_views_.empty()) {
    // The LogicalBufferCollection should be failed because there are no clients left, despite only
    // getting here if all of the clients did a clean Close().
    if (is_allocate_attempted_) {
      // Only log as info because this is a normal way to destroy the buffer collection.
      LogInfo(FROM_HERE,
              "All clients called Close(), but now zero clients remain (after allocation).");
      Fail();
    } else {
      LogAndFail(FROM_HERE,
                 "All clients called Close(), but now zero clients remain (before allocation).");
    }
    return;
  }
  if (is_allocate_attempted_) {
    // Allocate was already attempted.
    return;
  }
  // Sweep looking for any views that don't have constraints.
  for (auto& [key, value] : collection_views_) {
    if (!key->has_constraints()) {
      return;
    }
  }
  // All the views have seen SetConstraints(), and there are no tokens left.
  // Regardless of whether allocation succeeds or fails, we remember we've
  // started an attempt to allocate so we don't attempt again.
  is_allocate_attempted_ = true;
  TryAllocate();
  return;
}

// This only runs on a clean stack.
void LogicalBufferCollection::TryAllocate() {
  TRACE_DURATION("gfx", "LogicalBufferCollection::TryAllocate", "this", this);
  // If we're here it means we still have collection_views_, because if the
  // last collection view disappeared we would have run ~this which would have
  // cleared the Post() canary so this method woudn't be running.
  ZX_DEBUG_ASSERT(!collection_views_.empty());

  // Currently only BufferCollection(s) that have already done a clean Close()
  // have their constraints in constraints_list_.  The rest of the constraints
  // are still with collection_views_.  Move all constraints into
  // constraints_list_.
  for (auto& [key, value] : collection_views_) {
    ZX_DEBUG_ASSERT(key->has_constraints());
    constraints_list_.emplace_back(key->TakeConstraints(),
                                   ClientInfo{key->debug_name(), key->debug_id()});
    ZX_DEBUG_ASSERT(!key->has_constraints());
  }

  InitializeConstraintSnapshots(constraints_list_);
  if (!CombineConstraints()) {
    // It's impossible to combine the constraints due to incompatible
    // constraints, or all participants set null constraints.
    SetFailedAllocationResult(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  ZX_DEBUG_ASSERT(!!constraints_);

  fit::result<llcpp::fuchsia::sysmem2::BufferCollectionInfo, zx_status_t> result = Allocate();
  if (!result.is_ok()) {
    ZX_DEBUG_ASSERT(result.error() != ZX_OK);
    SetFailedAllocationResult(result.error());
    return;
  }
  ZX_DEBUG_ASSERT(result.is_ok());

  SetAllocationResult(result.take_value());
  return;
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
  SendAllocationResult();
  return;
}

void LogicalBufferCollection::SetAllocationResult(
    llcpp::fuchsia::sysmem2::BufferCollectionInfo&& info) {
  // Setting empty constraints as the success case isn't allowed.  That's
  // considered a failure.  At least one participant must specify non-empty
  // constraints.
  ZX_DEBUG_ASSERT(!info.IsEmpty());

  // Only set result once.
  ZX_DEBUG_ASSERT(!has_allocation_result_);
  // allocation_result_status_ is initialized to ZX_OK, so should still be set
  // that way.
  ZX_DEBUG_ASSERT(allocation_result_status_ == ZX_OK);

  creation_timer_.Cancel();
  allocation_result_status_ = ZX_OK;
  allocation_result_info_ = std::move(info);
  has_allocation_result_ = true;
  SendAllocationResult();
  return;
}

void LogicalBufferCollection::SendAllocationResult() {
  ZX_DEBUG_ASSERT(has_allocation_result_);
  ZX_DEBUG_ASSERT(token_views_.empty());
  ZX_DEBUG_ASSERT(!collection_views_.empty());

  for (auto& [key, value] : collection_views_) {
    key->OnBuffersAllocated();
  }

  if (allocation_result_status_ != ZX_OK) {
    LogAndFail(FROM_HERE,
               "LogicalBufferCollection::SendAllocationResult() done sending "
               "allocation failure - now auto-failing self.");
    return;
  }
}

void LogicalBufferCollection::BindSharedCollectionInternal(BufferCollectionToken* token,
                                                           zx::channel buffer_collection_request) {
  ZX_DEBUG_ASSERT(buffer_collection_request.get());
  auto self = token->parent_shared();
  ZX_DEBUG_ASSERT(self.get() == this);
  auto collection = BufferCollection::Create(self);
  collection->SetDebugClientInfo(token->debug_name().data(), token->debug_name().size(),
                                 token->debug_id());
  collection->SetErrorHandler([this, collection_ptr = collection.get()](zx_status_t status) {
    // status passed to an error handler is never ZX_OK.  Clean close is
    // ZX_ERR_PEER_CLOSED.
    ZX_DEBUG_ASSERT(status != ZX_OK);

    // The dispatcher shut down before we were able to Bind(...)
    if (status == ZX_ERR_BAD_STATE) {
      LogAndFail(FROM_HERE, "sysmem dispatcher shutting down - status: %d", status);
      return;
    }

    // We know collection_ptr is still alive because collection_ptr is
    // still in collection_views_.  We know this is still alive because
    // this has a RefPtr<> ref from collection_ptr.
    //
    // TODO(dustingreen): Switch to contains() when C++20.
    ZX_DEBUG_ASSERT(collection_views_.find(collection_ptr) != collection_views_.end());

    // The BufferCollection may have had Close() called on it, in which
    // case closure of the BufferCollection doesn't cause
    // LogicalBufferCollection failure.  Or, Close() wasn't called and
    // the LogicalBufferCollection is out of here.

    if (!(status == ZX_ERR_PEER_CLOSED && collection_ptr->is_done())) {
      // We don't have to explicitly remove collection from collection_views_ because Fail() will
      // collection_views_.clear().
      //
      // A BufferCollection view whose error handler runs implies LogicalBufferCollection failure.
      //
      // A LogicalBufferCollection intentionally treats any error that might be triggered by a
      // client failure as a LogicalBufferCollection failure, because a LogicalBufferCollection can
      // use a lot of RAM and can tend to block creating a replacement LogicalBufferCollection.
      //
      // If a participant is cleanly told to be done with a BufferCollection, the participant can
      // send Close() before BufferCollection channel close to avoid triggering this failure, in
      // case the initiator might want to continue using the BufferCollection without the
      // participant.
      //
      // TODO(fxbug.dev/33670): Provide a way to mark a BufferCollection view as expendable without
      // implying that the channel is closing, so that the client can still detect when the
      // BufferCollection VMOs need to be closed based on BufferCollection channel closure by
      // sysmem.
      //
      // In rare cases, an initiator might choose to use Close() to avoid this failure, but more
      // typically initiators will just close their BufferCollection view without Close() first, and
      // this failure results.  This is considered acceptable partly because it helps exercise code
      // in participants that may see BufferCollection channel closure before closure of related
      // channels, and it helps get the VMO handles closed ASAP to avoid letting those continue to
      // use space of a MemoryAllocator's pool of pre-reserved space (for example).
      //
      // TODO(fxbug.dev/45878): Provide a way to distinguish between BufferCollection clean/unclean
      // close so that we print an error if participant closes before initiator
      Fail();
      return;
    }

    // At this point we know the collection_ptr is cleanly done (Close()
    // was sent from client) and can be removed from the set of tracked
    // collections.  We keep the collection's constraints (if any), as
    // those are still relevant - this lets a participant do
    // SetConstraints() followed by Close() followed by closing the
    // participant's BufferCollection channel, which is convenient for
    // some participants.
    //
    // If this causes collection_tokens_.empty() and collection_views_.empty(),
    // MaybeAllocate() takes care of calling Fail().

    if (collection_ptr->has_constraints()) {
      constraints_list_.emplace_back(
          collection_ptr->TakeConstraints(),
          ClientInfo{collection_ptr->debug_name(), collection_ptr->debug_id()});
    }

    auto self = collection_ptr->parent_shared();
    ZX_DEBUG_ASSERT(self.get() == this);
    collection_views_.erase(collection_ptr);
    MaybeAllocate();
    // ~self may delete "this"
    return;
  });
  auto collection_ptr = collection.get();
  collection_views_.insert({collection_ptr, std::move(collection)});
  // ~BufferCollectionToken calls UntrackTokenKoid().
  token_views_.erase(token);
  collection_ptr->Bind(std::move(buffer_collection_request));
}

bool LogicalBufferCollection::IsMinBufferSizeSpecifiedByAnyParticipant() {
  ZX_DEBUG_ASSERT(!collection_views_.empty());
  ZX_DEBUG_ASSERT(collection_views_.end() ==
                  std::find_if(collection_views_.begin(), collection_views_.end(),
                               [](auto& item_pair) { return item_pair.first->has_constraints(); }));
  ZX_DEBUG_ASSERT(!constraints_list_.empty());
  for (auto& [constraints, client_info_unused] : constraints_list_) {
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

bool LogicalBufferCollection::CombineConstraints() {
  // This doesn't necessarily mean that any of the collection_views_ have
  // set non-empty constraints though.  We do require that at least one
  // participant (probably the initiator) retains an open channel to its
  // BufferCollection until allocation is done, else allocation won't be
  // attempted.
  ZX_DEBUG_ASSERT(!collection_views_.empty());
  // Caller is supposed to move all constraints into constraints_list_ before calling.
  ZX_DEBUG_ASSERT(collection_views_.end() ==
                  std::find_if(collection_views_.begin(), collection_views_.end(),
                               [](auto& item_pair) { return item_pair.first->has_constraints(); }));
  // We also know that all the constraints are in constraints_list_ now,
  // including all constraints from collection_views_.
  ZX_DEBUG_ASSERT(!constraints_list_.empty());

  // At least one participant must specify min buffer size (in terms of non-zero min buffer size or
  // non-zero min image size or non-zero potential max image size).
  //
  // This also enforces that at least one participant must specify non-empty constraints.
  if (!IsMinBufferSizeSpecifiedByAnyParticipant()) {
    // Too unconstrained...  We refuse to allocate buffers without any min size
    // bounds from any participant.  At least one particpant must provide
    // some form of size bounds (in terms of buffer size bounds or in terms
    // of image size bounds).
    LogError(FROM_HERE,
             "At least one participant must specify buffer_memory_constraints or "
             "image_format_constraints that implies non-zero min buffer size.");
    return false;
  }

  // Start with empty constraints / unconstrained.
  auto acc = allocator_.make_table_builder<llcpp::fuchsia::sysmem2::BufferCollectionConstraints>();
  // Sanitize initial accumulation target to keep accumulation simpler.  This is guaranteed to
  // succeed; the input is always the same.
  bool result = CheckSanitizeBufferCollectionConstraints(CheckSanitizeStage::kInitial, &acc);
  ZX_DEBUG_ASSERT(result);
  // Accumulate each participant's constraints.
  while (!constraints_list_.empty()) {
    Constraints constraints_entry = std::move(constraints_list_.front());
    constraints_list_.pop_front();
    current_client_info_ = &constraints_entry.client;
    auto defer_reset = fit::defer([this] { current_client_info_ = nullptr; });
    if (!CheckSanitizeBufferCollectionConstraints(CheckSanitizeStage::kNotAggregated,
                                                  &constraints_entry.builder)) {
      return false;
    }
    auto constraints = constraints_entry.builder.build();
    if (!AccumulateConstraintBufferCollection(&acc, &constraints)) {
      // This is a failure.  The space of permitted settings contains no
      // points.
      return false;
    }
  }

  if (!CheckSanitizeBufferCollectionConstraints(CheckSanitizeStage::kAggregated, &acc)) {
    return false;
  }

  constraints_ = std::move(acc);
  return true;
}

// TODO(dustingreen): Consider rejecting secure_required + any non-secure heaps, including the
// potentially-implicit SYSTEM_RAM heap.
//
// TODO(dustingreen): From a particular participant, CPU usage without
// IsCpuAccessibleHeapPermitted() should fail.
//
// TODO(dustingreen): From a particular participant, be more picky about which domains are supported
// vs. which heaps are supported.
static bool IsHeapPermitted(
    const llcpp::fuchsia::sysmem2::BufferMemoryConstraints::Builder& constraints,
    llcpp::fuchsia::sysmem2::HeapType heap) {
  if (constraints.heap_permitted().count()) {
    auto begin = constraints.heap_permitted().begin();
    auto end = constraints.heap_permitted().end();
    return std::find(begin, end, heap) != end;
  }
  // Zero heaps in heap_permitted() means any heap is ok.
  return true;
}

static bool IsSecurePermitted(
    const llcpp::fuchsia::sysmem2::BufferMemoryConstraints::Builder& constraints) {
  // TODO(fxbug.dev/37452): Generalize this by finding if there's a heap that maps to secure
  // MemoryAllocator in the permitted heaps.
  return constraints.inaccessible_domain_supported() &&
         (IsHeapPermitted(constraints, llcpp::fuchsia::sysmem2::HeapType::AMLOGIC_SECURE) ||
          IsHeapPermitted(constraints, llcpp::fuchsia::sysmem2::HeapType::AMLOGIC_SECURE_VDEC));
}

static bool IsCpuAccessSupported(
    const llcpp::fuchsia::sysmem2::BufferMemoryConstraints& constraints) {
  return constraints.cpu_domain_supported() || constraints.ram_domain_supported();
}

bool LogicalBufferCollection::CheckSanitizeBufferUsage(
    CheckSanitizeStage stage, llcpp::fuchsia::sysmem2::BufferUsage::Builder* buffer_usage) {
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
      if (buffer_usage->none() == 0 && buffer_usage->cpu() == 0 && buffer_usage->vulkan() == 0 &&
          buffer_usage->display() == 0 && buffer_usage->video() == 0) {
        LogError(FROM_HERE, "At least one usage bit must be set by a participant.");
        return false;
      }
      if (buffer_usage->none() != 0) {
        if (buffer_usage->cpu() != 0 || buffer_usage->vulkan() != 0 ||
            buffer_usage->display() != 0 || buffer_usage->video() != 0) {
          LogError(FROM_HERE,
                   "A participant indicating 'none' usage can't specify any other usage.");
          return false;
        }
      }
      break;
    case CheckSanitizeStage::kAggregated:
      if (buffer_usage->cpu() == 0 && buffer_usage->vulkan() == 0 && buffer_usage->display() == 0 &&
          buffer_usage->video() == 0) {
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
    CheckSanitizeStage stage,
    llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder* constraints) {
  bool was_empty = constraints->IsEmpty();
  FIELD_DEFAULT_SET(constraints, usage);
  if (was_empty) {
    // Completely empty constraints are permitted, so convert to NONE_USAGE to avoid triggering the
    // check applied to non-empty constraints where at least one usage bit must be set (NONE_USAGE
    // counts for that check, and doesn't constrain anything).
    FIELD_DEFAULT(&constraints->get_builder_usage(), none, llcpp::fuchsia::sysmem2::NONE_USAGE);
  }
  FIELD_DEFAULT_ZERO(constraints, min_buffer_count_for_camping);
  FIELD_DEFAULT_ZERO(constraints, min_buffer_count_for_dedicated_slack);
  FIELD_DEFAULT_ZERO(constraints, min_buffer_count_for_shared_slack);
  FIELD_DEFAULT_ZERO(constraints, min_buffer_count);
  FIELD_DEFAULT_MAX(constraints, max_buffer_count);
  ZX_DEBUG_ASSERT(constraints->has_buffer_memory_constraints() ||
                  stage != CheckSanitizeStage::kAggregated);
  FIELD_DEFAULT_SET(constraints, buffer_memory_constraints);
  ZX_DEBUG_ASSERT(constraints->has_buffer_memory_constraints());
  FIELD_DEFAULT_SET_VECTOR(constraints, image_format_constraints, InitialCapacityOrZero(stage, 64));
  FIELD_DEFAULT_FALSE(constraints, need_clear_aux_buffers_for_secure);
  FIELD_DEFAULT(constraints, allow_clear_aux_buffers_for_secure,
                !IsWriteUsage(constraints->usage()));
  if (!CheckSanitizeBufferUsage(stage, &constraints->get_builder_usage())) {
    LogError(FROM_HERE, "CheckSanitizeBufferUsage() failed");
    return false;
  }
  if (constraints->max_buffer_count() == 0) {
    LogError(FROM_HERE, "max_buffer_count == 0");
    return false;
  }
  if (constraints->min_buffer_count() > constraints->max_buffer_count()) {
    LogError(FROM_HERE, "min_buffer_count > max_buffer_count");
    return false;
  }
  if (!CheckSanitizeBufferMemoryConstraints(
          stage, constraints->usage(), &constraints->get_builder_buffer_memory_constraints())) {
    return false;
  }
  if (stage != CheckSanitizeStage::kAggregated) {
    if (IsCpuUsage(constraints->usage())) {
      if (!IsCpuAccessSupported(constraints->buffer_memory_constraints())) {
        LogError(FROM_HERE, "IsCpuUsage() && !IsCpuAccessSupported()");
        return false;
      }
      // From a single participant, reject secure_required in combination with CPU usage, since CPU
      // usage isn't possible given secure memory.
      if (constraints->buffer_memory_constraints().secure_required()) {
        LogError(FROM_HERE, "IsCpuUsage() && secure_required");
        return false;
      }
      // It's fine if a participant sets CPU usage but also permits inaccessible domain and possibly
      // IsSecurePermitted().  In that case the participant is expected to pay attetion to the
      // coherency domain and is_secure and realize that it shouldn't attempt to read/write the
      // VMOs.
    }
    if (constraints->buffer_memory_constraints().secure_required() &&
        IsCpuAccessSupported(constraints->buffer_memory_constraints())) {
      // This is a little picky, but easier to be less picky later than more picky later.
      LogError(FROM_HERE, "secure_required && IsCpuAccessSupported()");
      return false;
    }
  }
  for (uint32_t i = 0; i < constraints->image_format_constraints().count(); ++i) {
    if (!CheckSanitizeImageFormatConstraints(
            stage, &constraints->get_builders_image_format_constraints()[i])) {
      return false;
    }
  }

  if (stage == CheckSanitizeStage::kNotAggregated) {
    // As an optimization, only check the unaggregated inputs.
    for (uint32_t i = 0; i < constraints->image_format_constraints().count(); ++i) {
      for (uint32_t j = i + 1; j < constraints->image_format_constraints().count(); ++j) {
        if (ImageFormatIsPixelFormatEqual(
                constraints->image_format_constraints()[i].pixel_format(),
                constraints->image_format_constraints()[j].pixel_format())) {
          LogError(FROM_HERE, "image format constraints %d and %d have identical formats", i, j);
          return false;
        }
      }
    }
  }
  return true;
}

bool LogicalBufferCollection::CheckSanitizeBufferMemoryConstraints(
    CheckSanitizeStage stage, const llcpp::fuchsia::sysmem2::BufferUsage& buffer_usage,
    llcpp::fuchsia::sysmem2::BufferMemoryConstraints::Builder* constraints) {
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
    if (constraints->has_heap_permitted() && !constraints->heap_permitted().count()) {
      LogError(FROM_HERE,
               "constraints->has_heap_permitted() && !constraints->heap_permitted().count()");
      return false;
    }
  }
  // TODO(dustingreen): When 0 heaps specified, constrain heap list based on other constraints.
  // For now 0 heaps means any heap.
  FIELD_DEFAULT_SET_VECTOR(constraints, heap_permitted, 0);
  ZX_DEBUG_ASSERT(stage != CheckSanitizeStage::kInitial ||
                  constraints->heap_permitted().count() == 0);
  if (constraints->min_size_bytes() > constraints->max_size_bytes()) {
    LogError(FROM_HERE, "min_size_bytes > max_size_bytes");
    return false;
  }
  if (constraints->secure_required() && !IsSecurePermitted(*constraints)) {
    LogError(FROM_HERE, "secure memory required but not permitted");
    return false;
  }
  return true;
}

bool LogicalBufferCollection::CheckSanitizeImageFormatConstraints(
    CheckSanitizeStage stage,
    llcpp::fuchsia::sysmem2::ImageFormatConstraints::Builder* constraints) {
  // We never CheckSanitizeImageFormatConstraints() on empty (aka initial) constraints.
  ZX_DEBUG_ASSERT(stage != CheckSanitizeStage::kInitial);

  FIELD_DEFAULT_SET(constraints, pixel_format);
  FIELD_DEFAULT_ZERO(&constraints->get_builder_pixel_format(), type);
  FIELD_DEFAULT_ZERO(&constraints->get_builder_pixel_format(), format_modifier_value);

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

  if (constraints->pixel_format().type() == llcpp::fuchsia::sysmem2::PixelFormatType::INVALID) {
    LogError(FROM_HERE, "PixelFormatType INVALID not allowed");
    return false;
  }
  if (!ImageFormatIsSupported(constraints->pixel_format())) {
    LogError(FROM_HERE, "Unsupported pixel format");
    return false;
  }

  uint32_t min_bytes_per_row_given_min_width =
      ImageFormatStrideBytesPerWidthPixel(constraints->pixel_format()) *
      constraints->min_coded_width();
  constraints->min_bytes_per_row() =
      std::max(constraints->min_bytes_per_row(), min_bytes_per_row_given_min_width);

  if (!constraints->color_spaces().count()) {
    LogError(FROM_HERE, "color_spaces.count() == 0 not allowed");
    return false;
  }

  if (constraints->min_coded_width() > constraints->max_coded_width()) {
    LogError(FROM_HERE, "min_coded_width > max_coded_width");
    return false;
  }
  if (constraints->min_coded_height() > constraints->max_coded_height()) {
    LogError(FROM_HERE, "min_coded_height > max_coded_height");
    return false;
  }
  if (constraints->min_bytes_per_row() > constraints->max_bytes_per_row()) {
    LogError(FROM_HERE, "min_bytes_per_row > max_bytes_per_row");
    return false;
  }
  if (constraints->min_coded_width() * constraints->min_coded_height() >
      constraints->max_coded_width_times_coded_height()) {
    LogError(FROM_HERE,
             "min_coded_width * min_coded_height > "
             "max_coded_width_times_coded_height");
    return false;
  }

  if (!IsNonZeroPowerOf2(constraints->coded_width_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 coded_width_divisor not supported");
    return false;
  }
  if (!IsNonZeroPowerOf2(constraints->coded_height_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 coded_width_divisor not supported");
    return false;
  }
  if (!IsNonZeroPowerOf2(constraints->bytes_per_row_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 bytes_per_row_divisor not supported");
    return false;
  }
  if (!IsNonZeroPowerOf2(constraints->start_offset_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 start_offset_divisor not supported");
    return false;
  }
  if (constraints->start_offset_divisor() > PAGE_SIZE) {
    LogError(FROM_HERE, "support for start_offset_divisor > PAGE_SIZE not yet implemented");
    return false;
  }
  if (!IsNonZeroPowerOf2(constraints->display_width_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 display_width_divisor not supported");
    return false;
  }
  if (!IsNonZeroPowerOf2(constraints->display_height_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 display_height_divisor not supported");
    return false;
  }

  for (uint32_t i = 0; i < constraints->color_spaces().count(); ++i) {
    if (!ImageFormatIsSupportedColorSpaceForPixelFormat(constraints->color_spaces()[i],
                                                        constraints->pixel_format())) {
      auto colorspace_type = constraints->color_spaces()[i].has_type()
                                 ? constraints->color_spaces()[i].type()
                                 : llcpp::fuchsia::sysmem2::ColorSpaceType::INVALID;
      LogError(FROM_HERE,
               "!ImageFormatIsSupportedColorSpaceForPixelFormat() "
               "color_space.type: %u "
               "pixel_format.type: %u",
               colorspace_type, constraints->pixel_format().type());
      return false;
    }
  }

  if (constraints->required_min_coded_width() == 0) {
    LogError(FROM_HERE, "required_min_coded_width == 0");
    return false;
  }
  ZX_DEBUG_ASSERT(constraints->required_min_coded_width() != 0);
  if (constraints->required_min_coded_width() < constraints->min_coded_width()) {
    LogError(FROM_HERE, "required_min_coded_width < min_coded_width");
    return false;
  }
  if (constraints->required_max_coded_width() > constraints->max_coded_width()) {
    LogError(FROM_HERE, "required_max_coded_width > max_coded_width");
    return false;
  }
  if (constraints->required_min_coded_height() == 0) {
    LogError(FROM_HERE, "required_min_coded_height == 0");
    return false;
  }
  ZX_DEBUG_ASSERT(constraints->required_min_coded_height() != 0);
  if (constraints->required_min_coded_height() < constraints->min_coded_height()) {
    LogError(FROM_HERE, "required_min_coded_height < min_coded_height");
    return false;
  }
  if (constraints->required_max_coded_height() > constraints->max_coded_height()) {
    LogError(FROM_HERE, "required_max_coded_height > max_coded_height");
    return false;
  }
  if (constraints->required_min_bytes_per_row() == 0) {
    LogError(FROM_HERE, "required_min_bytes_per_row == 0");
    return false;
  }
  ZX_DEBUG_ASSERT(constraints->required_min_bytes_per_row() != 0);
  if (constraints->required_min_bytes_per_row() < constraints->min_bytes_per_row()) {
    LogError(FROM_HERE, "required_min_bytes_per_row < min_bytes_per_row");
    return false;
  }
  if (constraints->required_max_bytes_per_row() > constraints->max_bytes_per_row()) {
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
    llcpp::fuchsia::sysmem2::BufferUsage::Builder* acc, llcpp::fuchsia::sysmem2::BufferUsage* c) {
  // We accumulate "none" usage just like other usages, to make aggregation and CheckSanitize
  // consistent/uniform.
  acc->none() |= c->none();
  acc->cpu() |= c->cpu();
  acc->vulkan() |= c->vulkan();
  acc->display() |= c->display();
  acc->video() |= c->video();
  return true;
}

// |acc| accumulated constraints so far
//
// |c| additional constraint to aggregate into acc
bool LogicalBufferCollection::AccumulateConstraintBufferCollection(
    llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder* acc,
    llcpp::fuchsia::sysmem2::BufferCollectionConstraints* c) {
  if (!AccumulateConstraintsBufferUsage(&acc->get_builder_usage(), &c->usage())) {
    return false;
  }

  acc->min_buffer_count_for_camping() += c->min_buffer_count_for_camping();
  acc->min_buffer_count_for_dedicated_slack() += c->min_buffer_count_for_dedicated_slack();
  acc->min_buffer_count_for_shared_slack() =
      std::max(acc->min_buffer_count_for_shared_slack(), c->min_buffer_count_for_shared_slack());

  acc->min_buffer_count() = std::max(acc->min_buffer_count(), c->min_buffer_count());
  // 0 is replaced with 0xFFFFFFFF in
  // CheckSanitizeBufferCollectionConstraints.
  ZX_DEBUG_ASSERT(acc->max_buffer_count() != 0);
  ZX_DEBUG_ASSERT(c->max_buffer_count() != 0);
  acc->max_buffer_count() = std::min(acc->max_buffer_count(), c->max_buffer_count());

  // CheckSanitizeBufferCollectionConstraints() takes care of setting a default
  // buffer_collection_constraints, so we can assert that both acc and c "has_" one.
  ZX_DEBUG_ASSERT(acc->has_buffer_memory_constraints());
  ZX_DEBUG_ASSERT(c->has_buffer_memory_constraints());
  if (!AccumulateConstraintBufferMemory(&acc->get_builder_buffer_memory_constraints(),
                                        &c->buffer_memory_constraints())) {
    return false;
  }

  if (!acc->image_format_constraints().count()) {
    // Take the whole VectorView<>, as the count() can only go down later, so the capacity of
    // c.image_format_constraints() is fine.
    acc->set_image_format_constraints(
        sysmem::MakeTracking(&allocator_, std::move(c->image_format_constraints())));
  } else {
    ZX_DEBUG_ASSERT(acc->image_format_constraints().count());
    if (c->image_format_constraints().count()) {
      if (!AccumulateConstraintImageFormats(&acc->get_builders_image_format_constraints(),
                                            &c->image_format_constraints())) {
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
      acc->need_clear_aux_buffers_for_secure() || c->need_clear_aux_buffers_for_secure();
  acc->allow_clear_aux_buffers_for_secure() =
      acc->allow_clear_aux_buffers_for_secure() && c->allow_clear_aux_buffers_for_secure();
  // We check for consistency of these later only if we're actually attempting to allocate secure
  // buffers.

  // acc->image_format_constraints().count() == 0 is allowed here, when all
  // participants had image_format_constraints().count() == 0.
  return true;
}

bool LogicalBufferCollection::AccumulateConstraintHeapPermitted(
    fidl::VectorView<llcpp::fuchsia::sysmem2::HeapType>* acc,
    fidl::VectorView<llcpp::fuchsia::sysmem2::HeapType>* c) {
  // Remove any heap in acc that's not in c.  If zero heaps
  // remain in acc, return false.
  ZX_DEBUG_ASSERT(acc->count() > 0);

  for (uint32_t ai = 0; ai < acc->count(); ++ai) {
    uint32_t ci;
    for (ci = 0; ci < c->count(); ++ci) {
      if ((*acc)[ai] == (*c)[ci]) {
        // We found heap in c.  Break so we can move on to
        // the next heap.
        break;
      }
    }
    if (ci == c->count()) {
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
    llcpp::fuchsia::sysmem2::BufferMemoryConstraints::Builder* acc,
    llcpp::fuchsia::sysmem2::BufferMemoryConstraints* c) {
  acc->min_size_bytes() = std::max(acc->min_size_bytes(), c->min_size_bytes());

  // Don't permit 0 as the overall min_size_bytes; that would be nonsense.  No
  // particular initiator should feel that it has to specify 1 in this field;
  // that's just built into sysmem instead.  While a VMO will have a minimum
  // actual size of page size, we do permit treating buffers as if they're 1
  // byte, mainly for testing reasons, and to avoid any unnecessary dependence
  // or assumptions re. page size.
  acc->min_size_bytes() = std::max(acc->min_size_bytes(), 1u);
  acc->max_size_bytes() = std::min(acc->max_size_bytes(), c->max_size_bytes());

  acc->physically_contiguous_required() =
      acc->physically_contiguous_required() || c->physically_contiguous_required();

  acc->secure_required() = acc->secure_required() || c->secure_required();

  acc->ram_domain_supported() = acc->ram_domain_supported() && c->ram_domain_supported();
  acc->cpu_domain_supported() = acc->cpu_domain_supported() && c->cpu_domain_supported();
  acc->inaccessible_domain_supported() =
      acc->inaccessible_domain_supported() && c->inaccessible_domain_supported();

  if (!acc->heap_permitted().count()) {
    acc->set_heap_permitted(sysmem::MakeTracking(&allocator_, std::move(c->heap_permitted())));
  } else {
    if (c->heap_permitted().count()) {
      if (!AccumulateConstraintHeapPermitted(&acc->heap_permitted(), &c->heap_permitted())) {
        return false;
      }
    }
  }
  return true;
}

bool LogicalBufferCollection::AccumulateConstraintImageFormats(
    fidl::VectorView<llcpp::fuchsia::sysmem2::ImageFormatConstraints::Builder>* acc,
    fidl::VectorView<llcpp::fuchsia::sysmem2::ImageFormatConstraints>* c) {
  // Remove any pixel_format in acc that's not in c.  Process any format
  // that's in both.  If processing the format results in empty set for that
  // format, pretend as if the format wasn't in c and remove that format from
  // acc.  If acc ends up with zero formats, return false.

  // This method doesn't get called unless there's at least one format in
  // acc.
  ZX_DEBUG_ASSERT(acc->count());

  for (uint32_t ai = 0; ai < acc->count(); ++ai) {
    size_t ci;
    for (ci = 0; ci < c->count(); ++ci) {
      if (ImageFormatIsPixelFormatEqual((*acc)[ai].pixel_format(), (*c)[ci].pixel_format())) {
        if (!AccumulateConstraintImageFormat(&(*acc)[ai], &(*c)[ci])) {
          // Pretend like the format wasn't in c to begin with, so
          // this format gets removed from acc.  Only if this results
          // in zero formats in acc do we end up returning false.
          ci = c->count();
          break;
        }
        // We found the format in c and processed the format without
        // that resulting in empty set; break so we can move on to the
        // next format.
        break;
      }
    }
    if (ci == c->count()) {
      // Remove from acc because not found in c.
      //
      // Move last item on top of the item being removed, if not the same item.
      if (ai != acc->count() - 1) {
        (*acc)[ai] = std::move((*acc)[acc->count() - 1]);
      } else {
        // Stuff under this item would get deleted later anyway, but delete now to avoid keeping
        // cruft we don't need.
        (*acc)[ai] = llcpp::fuchsia::sysmem2::ImageFormatConstraints::Builder(nullptr);
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
    llcpp::fuchsia::sysmem2::ImageFormatConstraints::Builder* acc,
    llcpp::fuchsia::sysmem2::ImageFormatConstraints* c) {
  ZX_DEBUG_ASSERT(ImageFormatIsPixelFormatEqual(acc->pixel_format(), c->pixel_format()));
  // Checked previously.
  ZX_DEBUG_ASSERT(acc->color_spaces().count());
  // Checked previously.
  ZX_DEBUG_ASSERT(c->color_spaces().count());

  if (!AccumulateConstraintColorSpaces(&acc->get_builders_color_spaces(), &c->color_spaces())) {
    return false;
  }
  // Else AccumulateConstraintColorSpaces() would have returned false.
  ZX_DEBUG_ASSERT(acc->color_spaces().count());

  acc->min_coded_width() = std::max(acc->min_coded_width(), c->min_coded_width());
  acc->max_coded_width() = std::min(acc->max_coded_width(), c->max_coded_width());
  acc->min_coded_height() = std::max(acc->min_coded_height(), c->min_coded_height());
  acc->max_coded_height() = std::min(acc->max_coded_height(), c->max_coded_height());
  acc->min_bytes_per_row() = std::max(acc->min_bytes_per_row(), c->min_bytes_per_row());
  acc->max_bytes_per_row() = std::min(acc->max_bytes_per_row(), c->max_bytes_per_row());
  acc->max_coded_width_times_coded_height() =
      std::min(acc->max_coded_width_times_coded_height(), c->max_coded_width_times_coded_height());

  acc->coded_width_divisor() = std::max(acc->coded_width_divisor(), c->coded_width_divisor());
  acc->coded_width_divisor() =
      std::max(acc->coded_width_divisor(), ImageFormatCodedWidthMinDivisor(acc->pixel_format()));

  acc->coded_height_divisor() = std::max(acc->coded_height_divisor(), c->coded_height_divisor());
  acc->coded_height_divisor() =
      std::max(acc->coded_height_divisor(), ImageFormatCodedHeightMinDivisor(acc->pixel_format()));

  acc->bytes_per_row_divisor() = std::max(acc->bytes_per_row_divisor(), c->bytes_per_row_divisor());
  acc->bytes_per_row_divisor() =
      std::max(acc->bytes_per_row_divisor(), ImageFormatSampleAlignment(acc->pixel_format()));

  acc->start_offset_divisor() = std::max(acc->start_offset_divisor(), c->start_offset_divisor());
  acc->start_offset_divisor() =
      std::max(acc->start_offset_divisor(), ImageFormatSampleAlignment(acc->pixel_format()));

  acc->display_width_divisor() = std::max(acc->display_width_divisor(), c->display_width_divisor());
  acc->display_height_divisor() =
      std::max(acc->display_height_divisor(), c->display_height_divisor());

  // The required_ space is accumulated by taking the union, and must be fully
  // within the non-required_ space, else fail.  For example, this allows a
  // video decoder to indicate that it's capable of outputting a wide range of
  // output dimensions, but that it has specific current dimensions that are
  // presently required_ (min == max) for decode to proceed.
  ZX_DEBUG_ASSERT(acc->required_min_coded_width() != 0);
  ZX_DEBUG_ASSERT(c->required_min_coded_width() != 0);
  acc->required_min_coded_width() =
      std::min(acc->required_min_coded_width(), c->required_min_coded_width());
  acc->required_max_coded_width() =
      std::max(acc->required_max_coded_width(), c->required_max_coded_width());
  ZX_DEBUG_ASSERT(acc->required_min_coded_height() != 0);
  ZX_DEBUG_ASSERT(c->required_min_coded_height() != 0);
  acc->required_min_coded_height() =
      std::min(acc->required_min_coded_height(), c->required_min_coded_height());
  acc->required_max_coded_height() =
      std::max(acc->required_max_coded_height(), c->required_max_coded_height());
  ZX_DEBUG_ASSERT(acc->required_min_bytes_per_row() != 0);
  ZX_DEBUG_ASSERT(c->required_min_bytes_per_row() != 0);
  acc->required_min_bytes_per_row() =
      std::min(acc->required_min_bytes_per_row(), c->required_min_bytes_per_row());
  acc->required_max_bytes_per_row() =
      std::max(acc->required_max_bytes_per_row(), c->required_max_bytes_per_row());

  return true;
}

bool LogicalBufferCollection::AccumulateConstraintColorSpaces(
    fidl::VectorView<llcpp::fuchsia::sysmem2::ColorSpace::Builder>* acc,
    fidl::VectorView<llcpp::fuchsia::sysmem2::ColorSpace>* c) {
  // Remove any color_space in acc that's not in c.  If zero color spaces
  // remain in acc, return false.

  for (uint32_t ai = 0; ai < acc->count(); ++ai) {
    uint32_t ci;
    for (ci = 0; ci < c->count(); ++ci) {
      if (IsColorSpaceEqual((*acc)[ai], (*c)[ci])) {
        // We found the color space in c.  Break so we can move on to
        // the next color space.
        break;
      }
    }
    if (ci == c->count()) {
      // Remove from acc because not found in c.
      //
      // Move formerly last item on top of the item being removed, if not same item.
      if (ai != acc->count() - 1) {
        (*acc)[ai] = std::move((*acc)[acc->count() - 1]);
      } else {
        // Stuff under this item would get deleted later anyway, but delete now to avoid keeping
        // cruft we don't need.
        (*acc)[ai] = llcpp::fuchsia::sysmem2::ColorSpace::Builder(nullptr);
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

bool LogicalBufferCollection::IsColorSpaceEqual(
    const llcpp::fuchsia::sysmem2::ColorSpace::Builder& a,
    const llcpp::fuchsia::sysmem2::ColorSpace& b) {
  return a.type() == b.type();
}

static fit::result<llcpp::fuchsia::sysmem2::HeapType, zx_status_t> GetHeap(
    const llcpp::fuchsia::sysmem2::BufferMemoryConstraints::Builder& constraints, Device* device) {
  if (constraints.secure_required()) {
    // TODO(fxbug.dev/37452): Generalize this.
    //
    // checked previously
    ZX_DEBUG_ASSERT(!constraints.secure_required() || IsSecurePermitted(constraints));
    if (IsHeapPermitted(constraints, llcpp::fuchsia::sysmem2::HeapType::AMLOGIC_SECURE)) {
      return fit::ok(llcpp::fuchsia::sysmem2::HeapType::AMLOGIC_SECURE);
    } else {
      ZX_DEBUG_ASSERT(
          IsHeapPermitted(constraints, llcpp::fuchsia::sysmem2::HeapType::AMLOGIC_SECURE_VDEC));
      return fit::ok(llcpp::fuchsia::sysmem2::HeapType::AMLOGIC_SECURE_VDEC);
    }
  }
  if (IsHeapPermitted(constraints, llcpp::fuchsia::sysmem2::HeapType::SYSTEM_RAM)) {
    return fit::ok(llcpp::fuchsia::sysmem2::HeapType::SYSTEM_RAM);
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

static fit::result<llcpp::fuchsia::sysmem2::CoherencyDomain> GetCoherencyDomain(
    const llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder& constraints,
    MemoryAllocator* memory_allocator) {
  ZX_DEBUG_ASSERT(constraints.has_buffer_memory_constraints());

  using llcpp::fuchsia::sysmem2::CoherencyDomain;
  const auto& heap_properties = memory_allocator->heap_properties();
  ZX_DEBUG_ASSERT(heap_properties.has_coherency_domain_support());

  // Display prefers RAM coherency domain for now.
  if (constraints.usage().display() != 0) {
    if (constraints.buffer_memory_constraints().ram_domain_supported()) {
      // Display controllers generally aren't cache coherent, so prefer
      // RAM coherency domain.
      //
      // TODO - base on the system in use.
      return fit::ok(llcpp::fuchsia::sysmem2::CoherencyDomain::RAM);
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
    return fit::ok(llcpp::fuchsia::sysmem2::CoherencyDomain::INACCESSIBLE);
  }

  return fit::error();
}

fit::result<llcpp::fuchsia::sysmem2::BufferCollectionInfo, zx_status_t>
LogicalBufferCollection::Allocate() {
  TRACE_DURATION("gfx", "LogicalBufferCollection:Allocate", "this", this);
  ZX_DEBUG_ASSERT(constraints_);

  llcpp::fuchsia::sysmem2::BufferCollectionInfo::Builder result =
      allocator_.make_table_builder<llcpp::fuchsia::sysmem2::BufferCollectionInfo>();

  uint32_t min_buffer_count = constraints_->min_buffer_count_for_camping() +
                              constraints_->min_buffer_count_for_dedicated_slack() +
                              constraints_->min_buffer_count_for_shared_slack();
  min_buffer_count = std::max(min_buffer_count, constraints_->min_buffer_count());
  uint32_t max_buffer_count = constraints_->max_buffer_count();
  if (min_buffer_count > max_buffer_count) {
    LogError(FROM_HERE,
             "aggregate min_buffer_count > aggregate max_buffer_count - "
             "min: %u max: %u",
             min_buffer_count, max_buffer_count);
    return fit::error(ZX_ERR_NOT_SUPPORTED);
  }
  if (min_buffer_count > llcpp::fuchsia::sysmem::MAX_COUNT_BUFFER_COLLECTION_INFO_BUFFERS) {
    LogError(FROM_HERE,
             "aggregate min_buffer_count (%d) > MAX_COUNT_BUFFER_COLLECTION_INFO_BUFFERS (%d)",
             min_buffer_count, llcpp::fuchsia::sysmem::MAX_COUNT_BUFFER_COLLECTION_INFO_BUFFERS);
    return fit::error(ZX_ERR_NOT_SUPPORTED);
  }

  result.set_buffers(allocator_.make_vec_ptr<llcpp::fuchsia::sysmem2::VmoBuffer>(min_buffer_count));
  ZX_DEBUG_ASSERT(result.buffers().count() == min_buffer_count);
  ZX_DEBUG_ASSERT(result.buffers().count() <= max_buffer_count);

  uint64_t min_size_bytes = 0;
  uint64_t max_size_bytes = std::numeric_limits<uint64_t>::max();

  result.set_settings(
      sysmem::MakeTracking<llcpp::fuchsia::sysmem2::SingleBufferSettings>(&allocator_));
  llcpp::fuchsia::sysmem2::SingleBufferSettings::Builder& settings = result.get_builder_settings();
  settings.set_buffer_settings(
      sysmem::MakeTracking<llcpp::fuchsia::sysmem2::BufferMemorySettings>(&allocator_));
  llcpp::fuchsia::sysmem2::BufferMemorySettings::Builder& buffer_settings =
      settings.get_builder_buffer_settings();

  ZX_DEBUG_ASSERT(constraints_->has_buffer_memory_constraints());
  const llcpp::fuchsia::sysmem2::BufferMemoryConstraints::Builder& buffer_constraints =
      constraints_->get_builder_buffer_memory_constraints();
  buffer_settings.set_is_physically_contiguous(
      sysmem::MakeTracking(&allocator_, buffer_constraints.physically_contiguous_required()));
  // checked previously
  ZX_DEBUG_ASSERT(IsSecurePermitted(buffer_constraints) || !buffer_constraints.secure_required());
  buffer_settings.set_is_secure(
      sysmem::MakeTracking(&allocator_, buffer_constraints.secure_required()));
  if (buffer_settings.is_secure()) {
    if (constraints_->need_clear_aux_buffers_for_secure() &&
        !constraints_->allow_clear_aux_buffers_for_secure()) {
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
  buffer_settings.set_heap(sysmem::MakeTracking(&allocator_, result_get_heap.value()));

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

  auto coherency_domain_result = GetCoherencyDomain(*constraints_, allocator);
  if (!coherency_domain_result.is_ok()) {
    LogError(FROM_HERE, "No coherency domain found for buffer constraints");
    return fit::error(ZX_ERR_NOT_SUPPORTED);
  }
  buffer_settings.set_coherency_domain(
      sysmem::MakeTracking(&allocator_, coherency_domain_result.value()));

  // It's allowed for zero participants to have any ImageFormatConstraint(s),
  // in which case the combined constraints_ will have zero (and that's fine,
  // when allocating raw buffers that don't need any ImageFormatConstraint).
  //
  // At least for now, we pick which PixelFormat to use before determining if
  // the constraints associated with that PixelFormat imply a buffer size
  // range in min_size_bytes..max_size_bytes.
  if (constraints_->image_format_constraints().count()) {
    // Pick the best ImageFormatConstraints.
    uint32_t best_index = UINT32_MAX;
    bool found_unsupported_when_protected = false;
    for (uint32_t i = 0; i < constraints_->image_format_constraints().count(); ++i) {
      if (buffer_settings.is_secure() &&
          !ImageFormatCompatibleWithProtectedMemory(
              constraints_->image_format_constraints()[i].pixel_format())) {
        found_unsupported_when_protected = true;
        continue;
      }
      if (best_index == UINT32_MAX) {
        best_index = i;
      } else {
        if (CompareImageFormatConstraintsByIndex(i, best_index) < 0) {
          best_index = i;
        }
      }
    }
    if (best_index == UINT32_MAX) {
      ZX_DEBUG_ASSERT(found_unsupported_when_protected);
      LogError(FROM_HERE, "No formats were compatible with protected memory.");
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }
    // move from constraints_ to settings.
    settings.set_image_format_constraints(sysmem::MakeTracking(
        &allocator_, std::move(constraints_->image_format_constraints()[best_index])));
  }

  // Compute the min buffer size implied by image_format_constraints, so we ensure the buffers can
  // hold the min-size image.
  if (settings.has_image_format_constraints()) {
    const llcpp::fuchsia::sysmem2::ImageFormatConstraints& image_format_constraints =
        settings.image_format_constraints();
    llcpp::fuchsia::sysmem2::ImageFormat::Builder min_image =
        allocator_.make_table_builder<llcpp::fuchsia::sysmem2::ImageFormat>();
    min_image.set_pixel_format(sysmem::MakeTracking(
        &allocator_,
        sysmem::V2ClonePixelFormat(&allocator_, image_format_constraints.pixel_format()).build()));
    // We use required_max_coded_width because that's the max width that the producer (or
    // initiator) wants these buffers to be able to hold.
    min_image.set_coded_width(sysmem::MakeTracking(
        &allocator_, AlignUp(std::max(image_format_constraints.min_coded_width(),
                                      image_format_constraints.required_max_coded_width()),
                             image_format_constraints.coded_width_divisor())));
    if (min_image.coded_width() > image_format_constraints.max_coded_width()) {
      LogError(FROM_HERE, "coded_width_divisor caused coded_width > max_coded_width");
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }
    // We use required_max_coded_height because that's the max height that the producer (or
    // initiator) wants these buffers to be able to hold.
    min_image.set_coded_height(sysmem::MakeTracking(
        &allocator_, AlignUp(std::max(image_format_constraints.min_coded_height(),
                                      image_format_constraints.required_max_coded_height()),
                             image_format_constraints.coded_height_divisor())));
    if (min_image.coded_height() > image_format_constraints.max_coded_height()) {
      LogError(FROM_HERE, "coded_height_divisor caused coded_height > max_coded_height");
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }
    min_image.set_bytes_per_row(sysmem::MakeTracking(
        &allocator_, AlignUp(std::max(image_format_constraints.min_bytes_per_row(),
                                      ImageFormatStrideBytesPerWidthPixel(
                                          image_format_constraints.pixel_format()) *
                                          min_image.coded_width()),
                             image_format_constraints.bytes_per_row_divisor())));
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
    min_image.set_color_space(sysmem::MakeTracking(
        &allocator_,
        sysmem::V2CloneColorSpace(&allocator_, image_format_constraints.color_spaces()[0])
            .build()));

    uint64_t image_min_size_bytes = ImageFormatImageSize(min_image.build());

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

  if (settings.has_image_format_constraints()) {
    const llcpp::fuchsia::sysmem2::ImageFormatConstraints& image_format_constraints =
        settings.image_format_constraints();
    node_.CreateUint("pixel_format",
                     static_cast<uint64_t>(image_format_constraints.pixel_format().type()),
                     &vmo_properties_);
    if (image_format_constraints.pixel_format().has_format_modifier_value()) {
      node_.CreateUint("pixel_format_modifier",
                       image_format_constraints.pixel_format().format_modifier_value(),
                       &vmo_properties_);
    }
    if (image_format_constraints.min_coded_width() > 0) {
      node_.CreateUint("min_coded_width", image_format_constraints.min_coded_width(),
                       &vmo_properties_);
    }
    if (image_format_constraints.min_coded_height() > 0) {
      node_.CreateUint("min_coded_height", image_format_constraints.min_coded_height(),
                       &vmo_properties_);
    }
    if (image_format_constraints.required_max_coded_width() > 0) {
      node_.CreateUint("required_max_coded_width",
                       image_format_constraints.required_max_coded_width(), &vmo_properties_);
    }
    if (image_format_constraints.required_max_coded_height() > 0) {
      node_.CreateUint("required_max_coded_height",
                       image_format_constraints.required_max_coded_height(), &vmo_properties_);
    }
  }

  node_.CreateUint("allocator_id", allocator->id(), &vmo_properties_);
  node_.CreateUint("size_bytes", min_size_bytes, &vmo_properties_);
  node_.CreateUint("heap", static_cast<uint64_t>(buffer_settings.heap()), &vmo_properties_);

  // Now that min_size_bytes accounts for any ImageFormatConstraints, we can just allocate
  // min_size_bytes buffers.
  //
  // If an initiator (or a participant) wants to force buffers to be larger than the size implied by
  // minimum image dimensions, the initiator can use BufferMemorySettings.min_size_bytes to force
  // allocated buffers to be large enough.
  buffer_settings.set_size_bytes(
      sysmem::MakeTracking(&allocator_, static_cast<uint32_t>(min_size_bytes)));

  // Get memory allocator for aux buffers, if needed.
  MemoryAllocator* maybe_aux_allocator = nullptr;
  std::optional<llcpp::fuchsia::sysmem2::SingleBufferSettings::Builder> maybe_aux_settings;
  if (buffer_settings.is_secure() && constraints_->need_clear_aux_buffers_for_secure()) {
    maybe_aux_settings.emplace(
        allocator_.make_table_builder<llcpp::fuchsia::sysmem2::SingleBufferSettings>());
    maybe_aux_settings->set_buffer_settings(sysmem::MakeTracking(
        &allocator_,
        allocator_.make_table_builder<llcpp::fuchsia::sysmem2::BufferMemorySettings>().build()));
    auto& aux_buffer_settings = maybe_aux_settings->get_builder_buffer_settings();
    aux_buffer_settings.set_size_bytes(
        sysmem::MakeTracking(&allocator_, buffer_settings.size_bytes()));
    aux_buffer_settings.set_is_physically_contiguous(sysmem::MakeTracking(&allocator_, false));
    aux_buffer_settings.set_is_secure(sysmem::MakeTracking(&allocator_, false));
    aux_buffer_settings.set_coherency_domain(
        sysmem::MakeTracking(&allocator_, llcpp::fuchsia::sysmem2::CoherencyDomain::CPU));
    aux_buffer_settings.set_heap(
        sysmem::MakeTracking(&allocator_, llcpp::fuchsia::sysmem2::HeapType::SYSTEM_RAM));
    maybe_aux_allocator = parent_device_->GetAllocator(aux_buffer_settings);
    ZX_DEBUG_ASSERT(maybe_aux_allocator);
  }

  if (buffer_settings.size_bytes() > parent_device_->settings().max_allocation_size) {
    // This is different than max_size_bytes.  While max_size_bytes is part of the constraints,
    // max_allocation_size isn't part of the constraints.  The latter is used for simulating OOM or
    // preventing unpredictable memory pressure caused by a fuzzer or similar source of
    // unpredictability in tests.
    LogError(FROM_HERE, "AllocateVmo() failed because size %u > max_allocation_size %ld",
             buffer_settings.size_bytes(), parent_device_->settings().max_allocation_size);
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  for (uint32_t i = 0; i < result.buffers().count(); ++i) {
    auto allocate_result = AllocateVmo(allocator, settings, i);
    if (!allocate_result.is_ok()) {
      LogError(FROM_HERE, "AllocateVmo() failed");
      return fit::error(ZX_ERR_NO_MEMORY);
    }
    zx::vmo vmo = allocate_result.take_value();
    auto vmo_buffer = allocator_.make_table_builder<llcpp::fuchsia::sysmem2::VmoBuffer>();
    vmo_buffer.set_vmo(sysmem::MakeTracking(&allocator_, std::move(vmo)));
    vmo_buffer.set_vmo_usable_start(sysmem::MakeTracking(&allocator_, 0ul));
    if (maybe_aux_allocator) {
      ZX_DEBUG_ASSERT(maybe_aux_settings);
      auto aux_allocate_result = AllocateVmo(maybe_aux_allocator, maybe_aux_settings.value(), i);
      if (!aux_allocate_result.is_ok()) {
        LogError(FROM_HERE, "AllocateVmo() failed (aux)");
        return fit::error(ZX_ERR_NO_MEMORY);
      }
      zx::vmo aux_vmo = aux_allocate_result.take_value();
      vmo_buffer.set_aux_vmo(sysmem::MakeTracking(&allocator_, std::move(aux_vmo)));
    }
    result.buffers()[i] = vmo_buffer.build();
  }
  vmo_count_property_ = node_.CreateUint("vmo_count", result.buffers().count());
  // Make sure we have sufficient barrier after allocating/clearing/flushing any VMO newly allocated
  // by allocator above.
  BarrierAfterFlush();

  // Register failure handler with memory allocator.
  allocator->AddDestroyCallback(reinterpret_cast<intptr_t>(this), [this]() {
    LogAndFail(FROM_HERE, "LogicalBufferCollection memory allocator gone - now auto-failing self.");
  });
  memory_allocator_ = allocator;

  return fit::ok(result.build());
}

fit::result<zx::vmo> LogicalBufferCollection::AllocateVmo(
    MemoryAllocator* allocator,
    const llcpp::fuchsia::sysmem2::SingleBufferSettings::Builder& settings, uint32_t index) {
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
             "allocator->Allocate failed - size_bytes: %zu "
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

  auto node = node_.CreateChild(fbl::StringPrintf("vmo-%ld", info.koid).c_str());
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

  // We immediately create the ParentVmo instance so it can take care of calling allocator->Delete()
  // if this method returns early.  We intentionally don't emplace into parent_vmos_ until
  // StartWait() has succeeded.  In turn, StartWait() requires a child VMO to have been created
  // already (else ZX_VMO_ZERO_CHILDREN would trigger too soon).
  //
  // We need to keep the raw_parent_vmo around so we can wait for ZX_VMO_ZERO_CHILDREN, and so we
  // can call allocator->Delete(raw_parent_vmo).
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
  // map.  From this point, ZX_VMO_ZERO_CHILDREN is the only way that allocator->Delete() gets
  // called.
  status = tracked_parent_vmo->StartWait(parent_device_->dispatcher());
  if (status != ZX_OK) {
    LogError(FROM_HERE, "tracked_parent->StartWait() failed - status: %d", status);
    // ~tracked_parent_vmo calls allocator->Delete().
    return fit::error();
  }
  zx_handle_t raw_parent_vmo_handle = tracked_parent_vmo->vmo().get();
  TrackedParentVmo& parent_vmo_ref = *tracked_parent_vmo;
  auto emplace_result = parent_vmos_.emplace(raw_parent_vmo_handle, std::move(tracked_parent_vmo));
  ZX_DEBUG_ASSERT(emplace_result.second);

  // Now inform the allocator about the child VMO before we return it.
  status = allocator->SetupChildVmo(
      parent_vmo_ref.vmo(), local_child_vmo,
      sysmem::V2CloneSingleBufferSettingsBuilder(&allocator_, settings).build());
  if (status != ZX_OK) {
    LogError(FROM_HERE, "allocator->SetupChildVmo() failed - status: %d", status);
    // In this path, the ~local_child_vmo will async trigger parent_vmo_ref::OnZeroChildren()
    // which will call allocator->Delete() via above do_delete lambda passed to
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

  std::string name = name_ ? name_->name : "Unknown";

  LogError(FROM_HERE, "Allocation of %s timed out. Waiting for tokens: ", name.c_str());
  for (auto& token : token_views_) {
    if (token.second->debug_name() != "") {
      LogError(FROM_HERE, "Name %s id %ld", token.second->debug_name().c_str(),
               token.second->debug_id());
    } else {
      LogError(FROM_HERE, "Unknown token");
    }
  }
  LogError(FROM_HERE, "Collections:");
  for (auto& collection : collection_views_) {
    const char* constraints_state = collection.second->has_constraints() ? "set" : "unset";
    if (collection.second->debug_name() != "") {
      LogError(FROM_HERE, "Name \"%s\" id %ld (constraints %s)",
               collection.second->debug_name().c_str(), collection.second->debug_id(),
               constraints_state);
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
    const llcpp::fuchsia::sysmem2::ImageFormatConstraints& a,
    const llcpp::fuchsia::sysmem2::ImageFormatConstraints& b) {
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

int32_t LogicalBufferCollection::CompareImageFormatConstraintsByIndex(uint32_t index_a,
                                                                      uint32_t index_b) {
  // This method is allowed to look at constraints_.
  ZX_DEBUG_ASSERT(constraints_);

  int32_t cost_compare = UsagePixelFormatCost::Compare(parent_device_->pdev_device_info_vid(),
                                                       parent_device_->pdev_device_info_pid(),
                                                       *constraints_, index_a, index_b);
  if (cost_compare != 0) {
    return cost_compare;
  }

  // If we get this far, there's no known reason to choose one PixelFormat
  // over another, so just pick one based on a tie-breaker that'll distinguish
  // between PixelFormat(s).

  int32_t tie_breaker_compare =
      CompareImageFormatConstraintsTieBreaker(constraints_->image_format_constraints()[index_a],
                                              constraints_->image_format_constraints()[index_b]);
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

}  // namespace sysmem_driver
