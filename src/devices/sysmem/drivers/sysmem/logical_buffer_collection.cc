// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logical_buffer_collection.h"

#include <fidl/fuchsia.sysmem2/cpp/fidl.h>
#include <inttypes.h>
#include <lib/ddk/trace/event.h>
#include <lib/fidl/cpp/wire/arena.h>
#include <lib/fidl/cpp/wire/status.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/result.h>
#include <lib/image-format/image_format.h>
#include <lib/sysmem-version/sysmem-version.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <limits>  // std::numeric_limits
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <safemath/safe_conversions.h>

#include "buffer_collection.h"
#include "buffer_collection_token.h"
#include "buffer_collection_token_group.h"
#include "device.h"
#include "koid_util.h"
#include "logging.h"
#include "macros.h"
#include "node_properties.h"
#include "orphaned_node.h"
#include "usage_pixel_format_cost.h"

namespace sysmem_driver {

namespace {

// Sysmem is creating the VMOs, so sysmem can have all the rights and just not
// mis-use any rights.  Remove ZX_RIGHT_EXECUTE though.
const uint32_t kSysmemVmoRights = ZX_DEFAULT_VMO_RIGHTS & ~ZX_RIGHT_EXECUTE;
// 1 GiB cap for now.
const uint64_t kMaxTotalSizeBytesPerCollection = 1ull * 1024 * 1024 * 1024;
// 256 MiB cap for now.
const uint64_t kMaxSizeBytesPerBuffer = 256ull * 1024 * 1024;
// Give up on attempting to aggregate constraints after exactly this many group
// child combinations have been attempted.  This prevents sysmem getting stuck
// trying too many combinations.
const uint32_t kMaxGroupChildCombinations = 64;

// Map of all supported color spaces to an unique semi arbitrary number. A higher number means
// that the color space is less desirable and a lower number means that a color space is more
// desirable.
const std::unordered_map<sysmem::FidlUnderlyingTypeOrType_t<fuchsia_sysmem2::ColorSpaceType>,
                         uint32_t>
    kColorSpaceRanking = {
        {sysmem::fidl_underlying_cast(fuchsia_sysmem2::ColorSpaceType::kInvalid),
         std::numeric_limits<uint32_t>::max()},
        {sysmem::fidl_underlying_cast(fuchsia_sysmem2::ColorSpaceType::kSrgb), 1},
        {sysmem::fidl_underlying_cast(fuchsia_sysmem2::ColorSpaceType::kRec601Ntsc), 8},
        {sysmem::fidl_underlying_cast(fuchsia_sysmem2::ColorSpaceType::kRec601NtscFullRange), 7},
        {sysmem::fidl_underlying_cast(fuchsia_sysmem2::ColorSpaceType::kRec601Pal), 6},
        {sysmem::fidl_underlying_cast(fuchsia_sysmem2::ColorSpaceType::kRec601PalFullRange), 5},
        {sysmem::fidl_underlying_cast(fuchsia_sysmem2::ColorSpaceType::kRec709), 4},
        {sysmem::fidl_underlying_cast(fuchsia_sysmem2::ColorSpaceType::kRec2020), 3},
        {sysmem::fidl_underlying_cast(fuchsia_sysmem2::ColorSpaceType::kRec2100), 2},
        {sysmem::fidl_underlying_cast(fuchsia_sysmem2::ColorSpaceType::kPassThrough), 9}};

// Zero-initialized, so it shouldn't take up space on-disk.
constexpr uint64_t kZeroBytes = 8192;
const uint8_t kZeroes[kZeroBytes] = {};

constexpr uint32_t kNeedAuxVmoAlso = 1;

template <typename T>
bool IsNonZeroPowerOf2(T value) {
  static_assert(std::is_integral_v<T>);
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
#define FIELD_DEFAULT_1(table_ref_name, field_name)                                         \
  do {                                                                                      \
    auto& table_ref = (table_ref_name);                                                     \
    static_assert(IsNaturalFidlTable<std::remove_reference_t<decltype(table_ref)>>::value); \
    using FieldType = std::remove_reference_t<decltype((table_ref.field_name().value()))>;  \
    if (!table_ref.field_name().has_value()) {                                              \
      table_ref.field_name().emplace(safe_cast<FieldType>(1));                              \
      ZX_DEBUG_ASSERT(table_ref.field_name().value() == 1);                                 \
    }                                                                                       \
    ZX_DEBUG_ASSERT(table_ref.field_name().has_value());                                    \
  } while (false)

// TODO(fxbug.dev/50590): It'd be nice if this could be a function template over FIDL scalar field
// types.
#define FIELD_DEFAULT_MAX(table_ref_name, field_name)                                           \
  do {                                                                                          \
    auto& table_ref = (table_ref_name);                                                         \
    static_assert(IsNaturalFidlTable<std::remove_reference_t<decltype(table_ref)>>::value);     \
    using FieldType = std::remove_reference_t<decltype((table_ref.field_name().value()))>;      \
    if (!table_ref.field_name().has_value()) {                                                  \
      table_ref.field_name().emplace(std::numeric_limits<FieldType>::max());                    \
      ZX_DEBUG_ASSERT(table_ref.field_name().value() == std::numeric_limits<FieldType>::max()); \
    }                                                                                           \
    ZX_DEBUG_ASSERT(table_ref.field_name().has_value());                                        \
  } while (false)

// TODO(fxbug.dev/50590): It'd be nice if this could be a function template over FIDL scalar field
// types.
#define FIELD_DEFAULT_ZERO(table_ref_name, field_name)                                      \
  do {                                                                                      \
    auto& table_ref = (table_ref_name);                                                     \
    static_assert(IsNaturalFidlTable<std::remove_reference_t<decltype(table_ref)>>::value); \
    using FieldType = std::remove_reference_t<decltype((table_ref.field_name().value()))>;  \
    using UnderlyingType = sysmem::FidlUnderlyingTypeOrType_t<FieldType>;                   \
    if (!table_ref.field_name().has_value()) {                                              \
      table_ref.field_name().emplace(safe_cast<FieldType>(safe_cast<UnderlyingType>(0)));   \
      ZX_DEBUG_ASSERT(0 == safe_cast<UnderlyingType>(table_ref.field_name().value()));      \
    }                                                                                       \
    ZX_DEBUG_ASSERT(table_ref.field_name().has_value());                                    \
  } while (false)

// TODO(fxbug.dev/50590): It'd be nice if this could be a function template over FIDL scalar field
// types.
#define FIELD_DEFAULT_ZERO_64_BIT(table_ref_name, field_name)                               \
  do {                                                                                      \
    auto& table_ref = (table_ref_name);                                                     \
    static_assert(IsNaturalFidlTable<std::remove_reference_t<decltype(table_ref)>>::value); \
    using FieldType = std::remove_reference_t<decltype((table_ref.field_name().value()))>;  \
    using UnderlyingType = sysmem::FidlUnderlyingTypeOrType_t<FieldType>;                   \
    if (!table_ref.field_name().has_value()) {                                              \
      table_ref.field_name().emplace(safe_cast<FieldType>(0));                              \
      ZX_DEBUG_ASSERT(0 == safe_cast<UnderlyingType>(table_ref.field_name().value()));      \
    }                                                                                       \
    ZX_DEBUG_ASSERT(table_ref.field_name().has_value());                                    \
  } while (false)

#define FIELD_DEFAULT_FALSE(table_ref_name, field_name)                                     \
  do {                                                                                      \
    auto& table_ref = (table_ref_name);                                                     \
    static_assert(IsNaturalFidlTable<std::remove_reference_t<decltype(table_ref)>>::value); \
    using FieldType = std::remove_reference_t<decltype((table_ref.field_name().value()))>;  \
    static_assert(std::is_same_v<FieldType, bool>);                                         \
    if (!table_ref.field_name().has_value()) {                                              \
      table_ref.field_name().emplace(false);                                                \
      ZX_DEBUG_ASSERT(!table_ref.field_name().value());                                     \
    }                                                                                       \
    ZX_DEBUG_ASSERT(table_ref.field_name().has_value());                                    \
  } while (false)

template <typename Type>
class IsStdVector : public std::false_type {};
template <typename Type>
class IsStdVector<std::vector<Type>> : public std::true_type {};
template <typename Type>
inline constexpr bool IsStdVector_v = IsStdVector<Type>::value;

static_assert(IsStdVector_v<std::vector<uint32_t>>);
static_assert(!IsStdVector_v<uint32_t>);

template <typename Type, typename enable = void>
class IsStdString : public std::false_type {};
template <typename Type>
class IsStdString<Type, std::enable_if_t<std::is_same_v<std::string, std::decay_t<Type>>>>
    : public std::true_type {};
template <typename Type>
inline constexpr bool IsStdString_v = IsStdString<Type>::value;

static_assert(IsStdString_v<std::string>);
static_assert(!IsStdString_v<uint32_t>);

#define FIELD_DEFAULT(table_ref_name, field_name, value_name)                               \
  do {                                                                                      \
    auto& table_ref = (table_ref_name);                                                     \
    static_assert(IsNaturalFidlTable<std::remove_reference_t<decltype(table_ref)>>::value); \
    using FieldType = std::remove_reference_t<decltype((table_ref.field_name().value()))>;  \
    static_assert(!fidl::IsFidlObject<FieldType>::value);                                   \
    static_assert(!IsStdVector_v<FieldType>);                                               \
    static_assert(!IsStdString_v<FieldType>);                                               \
    if (!table_ref.field_name().has_value()) {                                              \
      auto field_value = (value_name);                                                      \
      table_ref.field_name().emplace(field_value);                                          \
      ZX_DEBUG_ASSERT(table_ref.field_name().value() == field_value);                       \
    }                                                                                       \
    ZX_DEBUG_ASSERT(table_ref.field_name().has_value());                                    \
  } while (false)

#define FIELD_DEFAULT_SET(table_ref_name, field_name)                                       \
  do {                                                                                      \
    auto& table_ref = (table_ref_name);                                                     \
    static_assert(IsNaturalFidlTable<std::remove_reference_t<decltype(table_ref)>>::value); \
    using TableType = std::remove_reference_t<decltype((table_ref.field_name().value()))>;  \
    static_assert(IsNaturalFidlTable<TableType>::value);                                    \
    if (!table_ref.field_name().has_value()) {                                              \
      table_ref.field_name().emplace();                                                     \
    }                                                                                       \
    ZX_DEBUG_ASSERT(table_ref.field_name().has_value());                                    \
  } while (false)

// regardless of capacity, initial count is always 0
#define FIELD_DEFAULT_SET_VECTOR(table_ref_name, field_name, capacity_param)                     \
  do {                                                                                           \
    auto& table_ref = (table_ref_name);                                                          \
    static_assert(IsNaturalFidlTable<std::remove_reference_t<decltype(table_ref)>>::value);      \
    using VectorFieldType = std::remove_reference_t<decltype((table_ref.field_name().value()))>; \
    static_assert(IsStdVector_v<VectorFieldType>);                                               \
    if (!table_ref.field_name().has_value()) {                                                   \
      size_t capacity = (capacity_param);                                                        \
      table_ref.field_name().emplace(0);                                                         \
      table_ref.field_name().value().reserve(capacity);                                          \
      ZX_DEBUG_ASSERT(table_ref.field_name().value().capacity() >= capacity);                    \
    }                                                                                            \
    ZX_DEBUG_ASSERT(table_ref.field_name().has_value());                                         \
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
  __asm__ volatile("dsb sy" : : : "memory");
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

bool IsSecureHeap(const fuchsia_sysmem2::HeapType heap_type) {
  // TODO(fxbug.dev/37452): Generalize this by finding if the heap_type maps to secure
  // MemoryAllocator.
  return heap_type == fuchsia_sysmem2::HeapType::kAmlogicSecure ||
         heap_type == fuchsia_sysmem2::HeapType::kAmlogicSecureVdec;
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

// Use IsImageFormatConstraintsArrayPixelFormatDoNotCare() instead where the array is available,
// since the array not having exactly 1 item means it's a malformed kDoNotCare, which this routine
// can't check.
fit::result<zx_status_t, bool> IsImageFormatConstraintsPixelFormatDoNotCare(
    const fuchsia_sysmem2::ImageFormatConstraints& x) {
  if (!x.pixel_format().has_value()) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  if (!x.pixel_format()->type().has_value()) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  if (*x.pixel_format()->type() != fuchsia_sysmem2::wire::PixelFormatType::kDoNotCare) {
    return fit::ok(false);
  }
  if (x.pixel_format()->format_modifier_value().has_value() &&
      *x.pixel_format()->format_modifier_value() != fuchsia_sysmem2::kFormatModifierNone) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  return fit::ok(true);
}

fit::result<zx_status_t, bool> IsImageFormatConstraintsArrayPixelFormatDoNotCare(
    const std::vector<fuchsia_sysmem2::ImageFormatConstraints>& x) {
  uint32_t do_not_care_count = 0;
  for (uint32_t i = 0; i < x.size(); ++i) {
    auto element_result = IsImageFormatConstraintsPixelFormatDoNotCare(x[i]);
    if (element_result.is_error()) {
      return element_result;
    }
    if (element_result.value()) {
      ++do_not_care_count;
    }
  }
  if (do_not_care_count >= 1 && x.size() != 1) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  ZX_DEBUG_ASSERT(do_not_care_count <= 1);
  return fit::ok(do_not_care_count != 0);
}

fit::result<zx_status_t, bool> IsColorSpaceArrayDoNotCare(
    const std::vector<fuchsia_sysmem2::ColorSpace>& x) {
  if (x.size() == 0) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  uint32_t do_not_care_count = 0;
  for (uint32_t i = 0; i < x.size(); ++i) {
    if (!x[i].type().has_value()) {
      return fit::error(ZX_ERR_INVALID_ARGS);
    }
    if (*x[i].type() == fuchsia_sysmem2::ColorSpaceType::kDoNotCare) {
      ++do_not_care_count;
    }
  }
  if (do_not_care_count >= 1 && x.size() != 1) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  ZX_DEBUG_ASSERT(do_not_care_count <= 1);
  return fit::ok(do_not_care_count != 0);
}

// Replicate the kDoNotCare to_update to correspond to the not kDoNotCare to_match.
void ReplicatePixelFormatDoNotCare(
    const std::vector<fuchsia_sysmem2::ImageFormatConstraints>& to_match,
    std::vector<fuchsia_sysmem2::ImageFormatConstraints>& to_update) {
  // Error result excluded by caller.
  ZX_DEBUG_ASSERT(!IsImageFormatConstraintsArrayPixelFormatDoNotCare(to_match).value());
  // Error result excluded by caller.
  ZX_DEBUG_ASSERT(IsImageFormatConstraintsArrayPixelFormatDoNotCare(to_update).value());
  ZX_DEBUG_ASSERT(to_update.size() == 1);
  if (to_match.empty()) {
    to_update.resize(0);
    return;
  }
  ZX_DEBUG_ASSERT(!to_match.empty());
  auto stash = std::move(to_update[0]);
  to_update.resize(to_match.size());
  for (uint32_t i = 0; i < to_match.size(); ++i) {
    // copy / clone
    to_update[i] = stash;
    // parameter is copied / cloned
    to_update[i].pixel_format().emplace(*to_match[i].pixel_format());
  }
  ZX_DEBUG_ASSERT(to_update.size() == to_match.size());
  ZX_DEBUG_ASSERT(!to_update.empty());
  ZX_DEBUG_ASSERT(!to_match.empty());
  ZX_DEBUG_ASSERT(*to_update[0].pixel_format()->type() == *to_match[0].pixel_format()->type());
  ZX_DEBUG_ASSERT(to_update[0].pixel_format()->format_modifier_value().has_value() ==
                  to_match[0].pixel_format()->format_modifier_value().has_value());
  // The format_modifier_value (if any) also matches.
}

void ReplicateColorSpaceDoNotCare(const std::vector<fuchsia_sysmem2::ColorSpace>& to_match,
                                  std::vector<fuchsia_sysmem2::ColorSpace>& to_update) {
  // error result excluded by caller
  ZX_DEBUG_ASSERT(!IsColorSpaceArrayDoNotCare(to_match).value());
  // error result excluded by caller
  ZX_DEBUG_ASSERT(IsColorSpaceArrayDoNotCare(to_update).value());
  ZX_DEBUG_ASSERT(to_update.size() == 1);
  if (to_match.empty()) {
    to_update.resize(0);
    return;
  }
  ZX_DEBUG_ASSERT(!to_match.empty());
  auto stash = std::move(to_update[0]);
  to_update.resize(to_match.size());
  for (uint32_t i = 0; i < to_match.size(); ++i) {
    // copy / clone
    to_update[i] = stash;
    to_update[i].type().emplace(*to_match[i].type());
  }
  ZX_DEBUG_ASSERT(to_update.size() == to_match.size());
  ZX_DEBUG_ASSERT(!to_update.empty());
  ZX_DEBUG_ASSERT(!to_match.empty());
  ZX_DEBUG_ASSERT(*to_update[0].type() == *to_match[0].type());
}

}  // namespace

// static
void LogicalBufferCollection::Create(zx::channel buffer_collection_token_request,
                                     Device* parent_device) {
  fbl::RefPtr<LogicalBufferCollection> logical_buffer_collection =
      fbl::AdoptRef<LogicalBufferCollection>(new LogicalBufferCollection(parent_device));
  // The existence of a channel-owned BufferCollectionToken adds a
  // fbl::RefPtr<> ref to LogicalBufferCollection.
  logical_buffer_collection->LogInfo(FROM_HERE, "LogicalBufferCollection::Create()");
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
  zx_status_t status = get_handle_koids(buffer_collection_token, &token_client_koid,
                                        &token_server_koid, ZX_OBJ_TYPE_CHANNEL);
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
  auto& token = BufferCollectionToken::EmplaceInTree(self, new_node_properties,
                                                     zx::unowned_channel(token_request.channel()));
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

  if (token.create_status() != ZX_OK) {
    LogAndFailNode(FROM_HERE, &token.node_properties(), token.create_status(),
                   "token.status() failed - create_status: %d", token.create_status());
    return;
  }

  if (token.was_unfound_node()) {
    // No failure triggered by this, but a helpful debug message on how to avoid a previous failure.
    LogClientError(FROM_HERE, new_node_properties,
                   "BufferCollectionToken.Duplicate() received for creating token with server koid"
                   "%ld after BindSharedCollection() previously received attempting to use same"
                   "token.  Client sequence should be Duplicate(), Sync(), BindSharedCollection()."
                   "Missing Sync()?",
                   token.server_koid());
  }

  token.Bind(token_request.TakeChannel());
}

void LogicalBufferCollection::CreateBufferCollectionTokenGroup(
    fbl::RefPtr<LogicalBufferCollection> self, NodeProperties* new_node_properties,
    fidl::ServerEnd<fuchsia_sysmem::BufferCollectionTokenGroup> group_request) {
  ZX_DEBUG_ASSERT(group_request);
  auto& group = BufferCollectionTokenGroup::EmplaceInTree(
      self, new_node_properties, zx::unowned_channel(group_request.channel()));
  group.SetErrorHandler([this, &group](zx_status_t status) {
    // Clean close from FIDL channel point of view is ZX_ERR_PEER_CLOSED,
    // and ZX_OK is never passed to the error handler.
    ZX_DEBUG_ASSERT(status != ZX_OK);

    // The dispatcher shut down before we were able to Bind(...)
    if (status == ZX_ERR_BAD_STATE) {
      LogAndFailRootNode(FROM_HERE, status, "sysmem dispatcher shutting down - status: %d", status);
      return;
    }

    // We know |this| is alive because the group is alive and the group has
    // a fbl::RefPtr<LogicalBufferCollection>.  The group is alive because
    // the group is still under the tree rooted at root_.
    //
    // Any other deletion of the group out of the tree at root_ (outside of
    // this error handler) doesn't run this error handler.
    ZX_DEBUG_ASSERT(root_);

    // If not clean Close()
    if (!group.is_done()) {
      // LogAndFailDownFrom() will also remove any no-longer-needed nodes from the tree.
      //
      // A group whose error handler sees anything other than clean Close() (is_done()) implies
      // failure domain failure (possibly LogicalBufferCollection failure if not separated from
      // root_ by AttachToken() or SetDispensable()).
      //
      // If a participant needs/wants to close its BufferCollectionTokenGroup channel without
      // triggering failure domain failure, the participant should use Close() to avoid triggering
      // this failure.
      NodeProperties* tree_to_fail = FindTreeToFail(&group.node_properties());
      if (tree_to_fail == root_.get()) {
        LogAndFailDownFrom(FROM_HERE, tree_to_fail, status,
                           "Group failure causing LogicalBufferCollection failure - status: %d",
                           status);
      } else {
        LogAndFailDownFrom(FROM_HERE, tree_to_fail, status,
                           "Group failure causing failure domain sub-tree failure - status: %d",
                           status);
      }
      return;
    }

    // At this point we know the group channel was Close()ed.
    ZX_DEBUG_ASSERT(status == ZX_ERR_PEER_CLOSED && group.is_done());

    // Just like a Close()ed token or collection channel, we replace a Close()ed
    // BufferCollectionTokenGroup with an OrphanedNode.  Since the 1 of N semantic of a group during
    // constraints aggregation is handled by NodeProperties, we don't need the
    // BufferCollectionTokenGroup object for that aspect.  The BufferCollectionTokenGroup is only
    // for the protocol-serving aspect and driving state changes related to protocol-serving.  Now
    // that protocol-serving is done, the BufferCollectionTokenGroup can go away.
    //
    // Unlike a token, since a group has no constraints of its own, the Close() doesn't implicitly
    // set has_constraints false constraints (unconstrained constraints), so the only reason we're
    // calling MaybeAllocate() in this path is in case there are zero remaining open channels to
    // any node, in which case the MaybeAllocate() will call Fail().
    //
    // We want to stop tracking the group now that we've processed all its previously-queued inbound
    // messages.
    //
    // Keep self alive fia "self" in case this will drop connected_node_count_ to zero.
    auto self = group.shared_logical_buffer_collection();
    ZX_DEBUG_ASSERT(self.get() == this);
    // This OrphanedNode will have no BufferCollectionConstraints of its own (no group ever does),
    // and the NodeConstraints is already configured as a group.  A group may be Close()ed before
    // or after buffers are logically allocated (in contrast to a token which can only be Close()ed
    // before buffers are logically allocated.
    NodeProperties& node_properties = group.node_properties();
    // This replaces group with an OrphanedNode, and also de-refs group.  It's not possible to send
    // an epitaph because ZX_ERR_PEER_CLOSED.
    OrphanedNode::EmplaceInTree(fbl::RefPtr(this), &node_properties);
    // This will never actually allocate, since group Close() is never a state change that will
    // enable allocation, but MaybeAllocate() may call Fail() if there are zero client connections
    // to any node remaining.
    MaybeAllocate();
    // ~self may delete "this"
  });

  if (group.create_status() != ZX_OK) {
    LogAndFailNode(FROM_HERE, new_node_properties, group.create_status(),
                   "get_handle_koids() failed - status: %d", group.create_status());
    return;
  }

  LogInfo(FROM_HERE, "CreateBufferCollectionTokenGroup() - server_koid: %lu", group.server_koid());
  group.Bind(group_request.TakeChannel());
}

void LogicalBufferCollection::AttachLifetimeTracking(zx::eventpair server_end,
                                                     uint32_t buffers_remaining) {
  lifetime_tracking_.emplace(buffers_remaining, std::move(server_end));
  SweepLifetimeTracking();
}

void LogicalBufferCollection::SweepLifetimeTracking() {
  while (true) {
    if (lifetime_tracking_.empty()) {
      return;
    }
    auto last_iter = lifetime_tracking_.end();
    last_iter--;
    uint32_t buffers_remaining = last_iter->first;
    if (buffers_remaining < parent_vmos_.size()) {
      return;
    }
    ZX_DEBUG_ASSERT(buffers_remaining >= parent_vmos_.size());
    // This does ~server_end, which signals ZX_EVENTPAIR_PEER_CLOSED to the client_end which is
    // typically held by the client.
    lifetime_tracking_.erase(last_iter);
  }
}

void LogicalBufferCollection::OnNodeReady() {
  // MaybeAllocate() requires the caller to keep "this" alive.
  auto self = fbl::RefPtr(this);
  MaybeAllocate();
  return;
}

void LogicalBufferCollection::SetName(uint32_t priority, std::string name) {
  if (!name_.has_value() || (priority > name_->priority)) {
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

void LogicalBufferCollection::SetVerboseLogging() {
  is_verbose_logging_ = true;
  LogInfo(FROM_HERE, "SetVerboseLogging()");
}

uint64_t LogicalBufferCollection::CreateDispensableOrdinal() { return next_dispensable_ordinal_++; }

AllocationResult LogicalBufferCollection::allocation_result() {
  ZX_DEBUG_ASSERT(has_allocation_result_ ||
                  (allocation_result_status_ == ZX_OK && !allocation_result_info_.has_value()));
  // If this assert fails, it mean we've already done ::Fail().  This should be impossible since
  // Fail() clears all BufferCollection views so they shouldn't be able to call
  // ::allocation_result().
  ZX_DEBUG_ASSERT(!(has_allocation_result_ && allocation_result_status_ == ZX_OK &&
                    !allocation_result_info_.has_value()));
  return {
      .buffer_collection_info =
          allocation_result_info_.has_value() ? &allocation_result_info_.value() : nullptr,
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
    // Clear out the result info. This may not yet close the VMOs, since they'll still be held onto
    // by the TableSet allocator.
    allocation_result_info_.reset();
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

void LogicalBufferCollection::LogInfo(Location location, const char* format, ...) const {
  va_list args;
  va_start(args, format);
  if (is_verbose_logging()) {
    zxlogvf(INFO, location.file(), location.line(), format, args);
  } else {
    zxlogvf(DEBUG, location.file(), location.line(), format, args);
  }
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
  const char* collection_name = name_.has_value() ? name_->name.c_str() : "Unknown";
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
  ZX_DEBUG_ASSERT(!constraints_list.empty());
  constraints_at_allocation_.clear();
  for (auto& constraints : constraints_list) {
    ConstraintInfoSnapshot snapshot;
    snapshot.inspect_node =
        inspect_node().CreateChild(CreateUniqueName("collection-at-allocation-"));
    if (constraints.constraints().min_buffer_count_for_camping().has_value()) {
      snapshot.inspect_node.RecordUint(
          "min_buffer_count_for_camping",
          constraints.constraints().min_buffer_count_for_camping().value());
    }
    if (constraints.constraints().min_buffer_count_for_shared_slack().has_value()) {
      snapshot.inspect_node.RecordUint(
          "min_buffer_count_for_shared_slack",
          constraints.constraints().min_buffer_count_for_shared_slack().value());
    }
    if (constraints.constraints().min_buffer_count_for_dedicated_slack().has_value()) {
      snapshot.inspect_node.RecordUint(
          "min_buffer_count_for_dedicated_slack",
          constraints.constraints().min_buffer_count_for_dedicated_slack().value());
    }
    if (constraints.constraints().min_buffer_count().has_value()) {
      snapshot.inspect_node.RecordUint("min_buffer_count",
                                       constraints.constraints().min_buffer_count().value());
    }
    snapshot.inspect_node.RecordUint("debug_id", constraints.client_debug_info().id);
    snapshot.inspect_node.RecordString("debug_name", constraints.client_debug_info().name);
    constraints_at_allocation_.push_back(std::move(snapshot));
  }
}

void LogicalBufferCollection::ResetGroupChildSelection(
    std::vector<NodeProperties*>& groups_by_priority) {
  // We intentionally set this to false in both ResetGroupChildSelection() and
  // InitGroupChildSelection(), because both put the child selections into a non-"done" state.
  done_with_group_child_selection_ = false;
  for (auto& node_properties : groups_by_priority) {
    node_properties->ResetWhichChild();
  }
}

void LogicalBufferCollection::InitGroupChildSelection(
    std::vector<NodeProperties*>& groups_by_priority) {
  // We intentionally set this to false in both ResetGroupChildSelection() and
  // InitGroupChildSelection(), because both put the child selections into a non-"done" state.
  done_with_group_child_selection_ = false;
  for (auto& node_properties : groups_by_priority) {
    node_properties->SetWhichChild(0);
  }
}

void LogicalBufferCollection::NextGroupChildSelection(
    std::vector<NodeProperties*> groups_by_priority) {
  ZX_DEBUG_ASSERT(groups_by_priority.empty() || groups_by_priority[0]->visible());
  std::vector<NodeProperties*>::reverse_iterator iter;
  if (groups_by_priority.empty()) {
    done_with_group_child_selection_ = true;
    ZX_DEBUG_ASSERT(DoneWithGroupChildSelections(groups_by_priority));
    return;
  }
  for (iter = groups_by_priority.rbegin(); iter != groups_by_priority.rend(); ++iter) {
    NodeProperties& np = **iter;
    if (!np.visible()) {
      // In this case we know there's another group before np in groups_by_priority, so in addition
      // to not needing to update which_child() of np, we don't need to handle being out of child
      // selections to consider in this path since we can handle that when we get to the first
      // group, which is also always visible.
      continue;
    }
    // If we're using NextGroupChildSelection(), we know that all groups have which_child() set.
    ZX_DEBUG_ASSERT(np.which_child().has_value());
    ZX_DEBUG_ASSERT(*np.which_child() < np.child_count());
    auto which_child = *np.which_child();
    if (which_child == np.child_count() - 1) {
      // "carry"; we'll keep looking for a parent which can increment without running out of
      // "digits".
      np.SetWhichChild(0);
      continue;
    }
    ZX_DEBUG_ASSERT(which_child + 1 < np.child_count());
    np.SetWhichChild(which_child + 1);
    // Successfully moved to next group child selection, and not done with child selections.
    ZX_DEBUG_ASSERT(!DoneWithGroupChildSelections(groups_by_priority));
    return;
  }
  // We tried to carry off the top (roughly speaking), so DoneWithGroupChildSelections() should now
  // return true.
  done_with_group_child_selection_ = true;
  ZX_DEBUG_ASSERT(DoneWithGroupChildSelections(groups_by_priority));
}

bool LogicalBufferCollection::DoneWithGroupChildSelections(
    const std::vector<NodeProperties*> groups_by_priority) {
  return done_with_group_child_selection_;
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
      auto failure_domains = FailureDomainSubtrees();
      // To get more detailed log output, we fail smaller trees first.
      int32_t i;
      for (i = safe_cast<int32_t>(failure_domains.size()) - 1; i >= 0; --i) {
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
          ZX_DEBUG_ASSERT(i >= 0);
          break;
        }
      }
      if (i < 0) {
        // Processed all failure domains and found zero that needed to fail due to zero
        // connected_client_count().
        break;
      }
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

      // Group 0 is highest priority (most important), with decreasing priority after that.
      std::vector<NodeProperties*> groups_by_priority =
          PrioritizedGroupsOfPrunedSubtreeEligibleForLogicalAllocation(*eligible_subtree);
      // All nodes of logical allocation (each group set to "all").
      ResetGroupChildSelection(groups_by_priority);
      auto all_subtree_nodes = NodesOfPrunedSubtreeEligibleForLogicalAllocation(*eligible_subtree);
      if (is_verbose_logging()) {
        // Just the NodeProperties* and whether it has constraints, in tree layout, accounting for
        // which_child == all which shows all children.
        //
        // We also log this again with which_child != all below, per group child selection.
        LogInfo(FROM_HERE, "pruned subtree (including all group children):");
        LogPrunedSubTree(eligible_subtree);
      }
      ZX_DEBUG_ASSERT(all_subtree_nodes.front() == eligible_subtree);
      bool found_not_ready_node = false;
      for (auto node_properties : all_subtree_nodes) {
        if (!node_properties->node()->ReadyForAllocation()) {
          found_not_ready_node = true;
          break;
        }
      }
      if (found_not_ready_node) {
        // next sub-tree
        continue;
      }

      if (is_verbose_logging()) {
        // All constraints, including NodeProperties* to ID the node.  Logged only once per subtree.
        LogInfo(FROM_HERE, "pruned subtree - node constraints:");
        LogNodeConstraints(all_subtree_nodes);
        // Log after constraints also, mainly to make the tree view easier to reference in the log
        // since the constraints log info can be a bit long.
        LogInfo(FROM_HERE, "pruned subtree (including all group children):");
        LogPrunedSubTree(eligible_subtree);
      }

      // We know all the nodes of this sub-tree are ready to attempt allocation.  Every path from
      // here down will have done something.
      did_something = true;

      ZX_DEBUG_ASSERT((!is_allocate_attempted_) == (eligible_subtree == root_.get()));
      ZX_DEBUG_ASSERT(is_allocate_attempted_ || eligible_subtrees.size() == 1);

      bool was_allocate_attempted = is_allocate_attempted_;
      // By default, aggregation failure, unless we get a more immediate failure or success.
      zx_status_t subtree_status = ZX_ERR_NOT_SUPPORTED;
      uint32_t combination_ordinal = 0;
      bool done_with_subtree;
      for (done_with_subtree = false, InitGroupChildSelection(groups_by_priority);
           !done_with_subtree && !DoneWithGroupChildSelections(groups_by_priority);
           ++combination_ordinal,
          ignore_result(done_with_subtree ||
                        (NextGroupChildSelection(groups_by_priority), false))) {
        if (combination_ordinal == kMaxGroupChildCombinations) {
          LOG(INFO, "hit kMaxGroupChildCombinations before successful constraint aggregation");
          subtree_status = ZX_ERR_OUT_OF_RANGE;
          done_with_subtree = true;
          break;
        }
        auto nodes = NodesOfPrunedSubtreeEligibleForLogicalAllocation(*eligible_subtree);
        ZX_DEBUG_ASSERT(nodes.front() == eligible_subtree);

        if (is_verbose_logging()) {
          // Just the NodeProperties* and its type, in tree view, accounting for which_child, to
          // show only the children that'll be included in the aggregation.
          LogInfo(FROM_HERE, "pruned subtree (including only selected group children):");
          LogPrunedSubTree(eligible_subtree);
        }

        if (is_allocate_attempted_) {
          // Allocate was already previously attempted.
          zx_status_t status = TryLateLogicalAllocation(nodes);
          if (status != ZX_OK) {
            switch (status) {
              case ZX_ERR_NOT_SUPPORTED:
                // next child selections (next iteration of the enclosing loop)
                ZX_DEBUG_ASSERT(subtree_status == ZX_ERR_NOT_SUPPORTED);
                break;
              default:
                subtree_status = status;
                done_with_subtree = true;
                break;
            }
            // next child selections or done_with_subtree
            continue;
          }
          subtree_status = ZX_OK;
          done_with_subtree = true;
          // Succeed the nodes of the subtree that aren't currently hidden by which_child()
          // selections, and fail the rest of the subtree as if ZX_ERR_NOT_SUPPORTED (like
          // aggregation failure).
          SetSucceededLateLogicalAllocationResult(std::move(nodes), std::move(all_subtree_nodes));
          // done_with_subtree true means loop will be done
          ZX_DEBUG_ASSERT(done_with_subtree);
          continue;
        }

        // Initial allocation can only have one eligible subtree.
        ZX_DEBUG_ASSERT(eligible_subtrees.size() == 1);
        ZX_DEBUG_ASSERT(eligible_subtrees[0] == root_.get());

        // All the views have seen SetConstraints(), and there are no tokens left.
        // Regardless of whether allocation succeeds or fails, we remember we've
        // started an attempt to allocate so we don't attempt again.
        auto result = TryAllocate(nodes);
        if (!result.is_ok()) {
          switch (result.error()) {
            case ZX_ERR_NOT_SUPPORTED:
              ZX_DEBUG_ASSERT(subtree_status == ZX_ERR_NOT_SUPPORTED);
              // next child selections
              break;
            default:
              subtree_status = result.error();
              done_with_subtree = true;
              break;
          }
          // next child selections or done_with_subtree
          continue;
        }
        subtree_status = ZX_OK;
        done_with_subtree = true;
        is_allocate_attempted_ = true;
        // Succeed portion of pruned subtree indicated by current group child selections; fail
        // rest of pruned subtree nodes with ZX_ERR_NOT_SUPPORTED as if they failed aggregation.
        //
        // For nodes outside the portion of the pruned subtree indicated by current group child
        // selections, it's not relevant whether the node(s) were part of any attempted
        // aggregations so far which happened to fail aggregation (with diffuse blame), or
        // whether the node(s) just didn't need to be used/tried.  We just fail them with
        // ZX_ERR_NOT_SUPPORTED as a sanitized/converged error so the relevant collection
        // channels indicate failure and the corresponding Node(s) can be cleaned up.
        SetAllocationResult(std::move(nodes), result.take_value(), std::move(all_subtree_nodes));
        // The outermost loop will try again, in case there were ready AttachToken()(s) and/or
        // dispensable views queued up behind the initial allocation.  In the next iteration if
        // there's nothing to do we'll return.
        //
        // done_with_subtree true means loop will be done
        ZX_DEBUG_ASSERT(done_with_subtree);
      }
      // This can still be ZX_ERR_NOT_SUPPORTED if we never got any more immediate failure and
      // never got success, or this can be some other more immediate failure (still needs to be
      // handled/propagated here), or this can be ZX_OK if we already handled success.
      if (subtree_status != ZX_OK) {
        if (was_allocate_attempted) {
          // fail entire logical allocation, including all pruned subtree nodes, regardless of
          // group child selections
          SetFailedLateLogicalAllocationResult(all_subtree_nodes[0], subtree_status);
        } else {
          // fail the initial allocation from root_ down
          LOG(INFO, "fail the initial allocation from root_ down");
          SetFailedAllocationResult(subtree_status);
        }
      }
    }
  } while (did_something);
}

fpromise::result<fuchsia_sysmem2::BufferCollectionInfo, zx_status_t>
LogicalBufferCollection::TryAllocate(std::vector<NodeProperties*> nodes) {
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
      // first parameter is cloned/copied via generated code
      constraints_list.emplace_back(*node_properties->buffer_collection_constraints(),
                                    *node_properties);
    }
  }

  InitializeConstraintSnapshots(constraints_list);

  auto combine_result = CombineConstraints(&constraints_list);
  if (!combine_result.is_ok()) {
    // It's impossible to combine the constraints due to incompatible
    // constraints, or all participants set null constraints.
    LogInfo(FROM_HERE, "CombineConstraints() failed");
    return fpromise::error(ZX_ERR_NOT_SUPPORTED);
  }
  ZX_DEBUG_ASSERT(combine_result.is_ok());
  ZX_DEBUG_ASSERT(constraints_list.empty());
  auto combined_constraints = combine_result.take_value();

  auto generate_result = GenerateUnpopulatedBufferCollectionInfo(combined_constraints);
  if (!generate_result.is_ok()) {
    ZX_DEBUG_ASSERT(generate_result.error() != ZX_OK);
    if (generate_result.error() != ZX_ERR_NOT_SUPPORTED) {
      LogError(FROM_HERE, "GenerateUnpopulatedBufferCollectionInfo() failed");
    }
    return generate_result;
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
  ZX_DEBUG_ASSERT(!buffer_collection_info_before_population_.has_value());
  auto clone_result = sysmem::V2CloneBufferCollectionInfo(buffer_collection_info, 0, 0);
  if (!clone_result.is_ok()) {
    ZX_DEBUG_ASSERT(clone_result.error() != ZX_OK);
    ZX_DEBUG_ASSERT(clone_result.error() != ZX_ERR_NOT_SUPPORTED);
    LogError(FROM_HERE, "V2CloneBufferCollectionInfo() failed");
    return clone_result;
  }
  buffer_collection_info_before_population_.emplace(clone_result.take_value());
  clone_result = sysmem::V2CloneBufferCollectionInfo(buffer_collection_info, 0, 0);
  if (!clone_result.is_ok()) {
    ZX_DEBUG_ASSERT(clone_result.error() != ZX_OK);
    ZX_DEBUG_ASSERT(clone_result.error() != ZX_ERR_NOT_SUPPORTED);
    LogError(FROM_HERE, "V2CloneBufferCollectionInfo() failed");
    return clone_result;
  }
  auto tmp_buffer_collection_info_before_population = clone_result.take_value();
  fidl::Arena arena;
  auto wire_tmp_buffer_collection_info_before_population =
      fidl::ToWire(arena, std::move(tmp_buffer_collection_info_before_population));
  // TODO(fxbug.dev/45252): Use FIDL at rest.
  linearized_buffer_collection_info_before_population_.emplace(
      fidl::internal::WireFormatVersion::kV2, &wire_tmp_buffer_collection_info_before_population);

  fpromise::result<fuchsia_sysmem2::BufferCollectionInfo, zx_status_t> result =
      Allocate(combined_constraints, &buffer_collection_info);
  if (!result.is_ok()) {
    ZX_DEBUG_ASSERT(result.error() != ZX_OK);
    ZX_DEBUG_ASSERT(result.error() != ZX_ERR_NOT_SUPPORTED);
    return result;
  }
  ZX_DEBUG_ASSERT(result.is_ok());
  return result;
}

// This requires that nodes have the sub-tree's root-most node at nodes[0].
zx_status_t LogicalBufferCollection::TryLateLogicalAllocation(std::vector<NodeProperties*> nodes) {
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
      // first parameter cloned/copied via generated code
      constraints_list.emplace_back(*logically_allocated_node->buffer_collection_constraints(),
                                    *logically_allocated_node);
    }
  }

  // Constraints of nodes trying to logically allocate now.  These can include BufferCollection(s)
  // and OrphanedNode(s).
  for (auto additional_node : nodes) {
    ZX_DEBUG_ASSERT(additional_node->node()->ReadyForAllocation());
    if (additional_node->buffer_collection_constraints()) {
      // first parameter cloned/copied via generated code
      constraints_list.emplace_back(*additional_node->buffer_collection_constraints(),
                                    *additional_node);
    }
  }

  // Synthetic constraints entry to make sure the total # of buffers is at least as large as the
  // number already allocated.  Also, to try to use the same PixelFormat as we've already allocated,
  // else we'll fail to CombineConstraints().  Also, if what we've already allocated has any
  // optional characteristics, we require those so that we'll choose to enable those characteristics
  // again if we can, else we'll fail to CombineConstraints().
  const auto& existing = *buffer_collection_info_before_population_;
  fuchsia_sysmem2::BufferCollectionConstraints existing_constraints;
  fuchsia_sysmem2::BufferUsage usage;
  usage.none().emplace(fuchsia_sysmem2::kNoneUsage);
  existing_constraints.usage().emplace(std::move(usage));
  ZX_DEBUG_ASSERT(!existing_constraints.min_buffer_count_for_camping().has_value());
  ZX_DEBUG_ASSERT(!existing_constraints.min_buffer_count_for_dedicated_slack().has_value());
  ZX_DEBUG_ASSERT(!existing_constraints.min_buffer_count_for_shared_slack().has_value());
  ZX_DEBUG_ASSERT(!existing_constraints.min_buffer_count_for_shared_slack().has_value());
  existing_constraints.min_buffer_count().emplace(safe_cast<uint32_t>(existing.buffers()->size()));
  // We don't strictly need to set this, because we always try to allocate as few buffers as we can
  // so we'd catch needing more than we have during linear form comparison below, but _might_ be
  // easier to diagnose why we failed with this set, as the constraints aggregation will fail with
  // a logged message about the max_buffer_count being exceeded.
  existing_constraints.max_buffer_count().emplace(safe_cast<uint32_t>(existing.buffers()->size()));
  existing_constraints.buffer_memory_constraints().emplace();
  auto& buffer_memory_constraints = existing_constraints.buffer_memory_constraints().value();
  buffer_memory_constraints.min_size_bytes().emplace(
      existing.settings()->buffer_settings()->size_bytes().value());
  buffer_memory_constraints.max_size_bytes().emplace(
      existing.settings()->buffer_settings()->size_bytes().value());
  if (existing.settings()->buffer_settings()->is_physically_contiguous().value()) {
    buffer_memory_constraints.physically_contiguous_required().emplace(true);
  }
  ZX_DEBUG_ASSERT(existing.settings()->buffer_settings()->is_secure().value() ==
                  IsSecureHeap(existing.settings()->buffer_settings()->heap().value()));
  if (existing.settings()->buffer_settings()->is_secure().value()) {
    buffer_memory_constraints.secure_required().emplace(true);
  }
  switch (existing.settings()->buffer_settings()->coherency_domain().value()) {
    case fuchsia_sysmem2::CoherencyDomain::kCpu:
      // We don't want defaults chosen based on usage, so explicitly specify each of these fields.
      buffer_memory_constraints.cpu_domain_supported().emplace(true);
      buffer_memory_constraints.ram_domain_supported().emplace(false);
      buffer_memory_constraints.inaccessible_domain_supported().emplace(false);
      break;
    case fuchsia_sysmem2::CoherencyDomain::kRam:
      // We don't want defaults chosen based on usage, so explicitly specify each of these fields.
      buffer_memory_constraints.cpu_domain_supported().emplace(false);
      buffer_memory_constraints.ram_domain_supported().emplace(true);
      buffer_memory_constraints.inaccessible_domain_supported().emplace(false);
      break;
    case fuchsia_sysmem2::CoherencyDomain::kInaccessible:
      // We don't want defaults chosen based on usage, so explicitly specify each of these fields.
      buffer_memory_constraints.cpu_domain_supported().emplace(false);
      buffer_memory_constraints.ram_domain_supported().emplace(false);
      buffer_memory_constraints.inaccessible_domain_supported().emplace(true);
      break;
    default:
      ZX_PANIC("not yet implemented (new enum value?)");
  }
  buffer_memory_constraints.heap_permitted().emplace(1);
  buffer_memory_constraints.heap_permitted()->at(0) =
      existing.settings()->buffer_settings()->heap().value();
  if (existing.settings()->image_format_constraints().has_value()) {
    // We can't loosen the constraints after initial allocation, nor can we tighten them.  We also
    // want to chose the same PixelFormat as we already have allocated.
    existing_constraints.image_format_constraints().emplace(1);
    // clone/copy via generated code
    existing_constraints.image_format_constraints()->at(0) =
        existing.settings()->image_format_constraints().value();
  }
  if (existing.buffers()->at(0).vmo_usable_start().has_value() &&
      existing.buffers()->at(0).vmo_usable_start().value() & kNeedAuxVmoAlso) {
    existing_constraints.need_clear_aux_buffers_for_secure().emplace(true);
  }
  existing_constraints.allow_clear_aux_buffers_for_secure().emplace(true);

  if (is_verbose_logging()) {
    LogInfo(FROM_HERE, "constraints from initial allocation:");
    LogConstraints(FROM_HERE, nullptr, existing_constraints);
  }

  // We could make this temp NodeProperties entirely stack-based, but we'd rather enforce that
  // NodeProperties is always tracked with std::unique_ptr<NodeProperties>.
  auto tmp_node = NodeProperties::NewTemporary(this, std::move(existing_constraints),
                                               "sysmem-internals-no-fewer");
  // first parameter cloned/copied via generated code
  constraints_list.emplace_back(*tmp_node->buffer_collection_constraints(), *tmp_node);

  auto combine_result = CombineConstraints(&constraints_list);
  if (!combine_result.is_ok()) {
    // It's impossible to combine the constraints due to incompatible constraints, or all
    // participants set null constraints.
    //
    // While nodes are from the pruned tree, if a parent can't allocate, then its child can't
    // allocate either, so this fails the whole sub-tree.
    return ZX_ERR_NOT_SUPPORTED;
  }

  ZX_DEBUG_ASSERT(combine_result.is_ok());
  ZX_DEBUG_ASSERT(constraints_list.empty());
  auto combined_constraints = combine_result.take_value();

  auto generate_result = GenerateUnpopulatedBufferCollectionInfo(combined_constraints);
  if (!generate_result.is_ok()) {
    ZX_DEBUG_ASSERT(generate_result.error() != ZX_OK);
    if (generate_result.error() != ZX_ERR_NOT_SUPPORTED) {
      LogError(FROM_HERE,
               "GenerateUnpopulatedBufferCollectionInfo() failed -> "
               "AttachToken() sequence failed - status: %d",
               generate_result.error());
    }
    return generate_result.error();
  }
  ZX_DEBUG_ASSERT(generate_result.is_ok());
  fuchsia_sysmem2::BufferCollectionInfo unpopulated_buffer_collection_info =
      generate_result.take_value();

  auto clone_result = sysmem::V2CloneBufferCollectionInfo(unpopulated_buffer_collection_info, 0, 0);
  if (!clone_result.is_ok()) {
    ZX_DEBUG_ASSERT(clone_result.error() != ZX_OK);
    ZX_DEBUG_ASSERT(clone_result.error() != ZX_ERR_NOT_SUPPORTED);
    LogError(FROM_HERE,
             "V2CloneBufferCollectionInfo() failed -> AttachToken() "
             "sequence failed - status: %d",
             clone_result.error());
    return clone_result.error();
  }
  auto tmp_unpopulated_buffer_collection_info = clone_result.take_value();
  fidl::Arena arena;
  auto wire_tmp_unpopulated_buffer_collection_info =
      fidl::ToWire(arena, std::move(tmp_unpopulated_buffer_collection_info));
  // This could be big so use heap.
  // TODO(fxbug.dev/45252): Use FIDL at rest.
  auto linearized_late_logical_allocation_buffer_collection_info = std::make_unique<
      fidl::unstable::OwnedEncodedMessage<fuchsia_sysmem2::wire::BufferCollectionInfo>>(
      fidl::internal::WireFormatVersion::kV2, &wire_tmp_unpopulated_buffer_collection_info);

  fidl::OutgoingMessage& original_linear_buffer_collection_info =
      linearized_buffer_collection_info_before_population_->GetOutgoingMessage();
  fidl::OutgoingMessage& new_linear_buffer_collection_info =
      linearized_late_logical_allocation_buffer_collection_info->GetOutgoingMessage();
  if (!original_linear_buffer_collection_info.ok()) {
    LogError(FROM_HERE, "original error: %s",
             original_linear_buffer_collection_info.FormatDescription().c_str());
  }
  if (!new_linear_buffer_collection_info.ok()) {
    LogError(FROM_HERE, "new error: %s",
             new_linear_buffer_collection_info.FormatDescription().c_str());
  }
  ZX_DEBUG_ASSERT(original_linear_buffer_collection_info.ok());
  ZX_DEBUG_ASSERT(new_linear_buffer_collection_info.ok());
  ZX_DEBUG_ASSERT(original_linear_buffer_collection_info.handle_actual() == 0);
  ZX_DEBUG_ASSERT(new_linear_buffer_collection_info.handle_actual() == 0);
  if (!original_linear_buffer_collection_info.BytesMatch(new_linear_buffer_collection_info)) {
    LogInfo(FROM_HERE,
            "original_linear_buffer_collection_info.BytesMatch(new_linear_buffer_collection_info)");
    LogDiffsBufferCollectionInfo(*buffer_collection_info_before_population_,
                                 unpopulated_buffer_collection_info);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Now that we know the new participants can be added without changing the BufferCollectionInfo,
  // we can inform the new participants that their logical allocation succeeded.
  //
  // This will set success for nodes of the pruned sub-tree, not any AttachToken() children; those
  // attempt logical allocation later assuming all goes well.  The success only applies to the
  // current which_child() selections; nodes of the pruned sub-tree outside the current
  // which_child() selections will still be handled as if they are ZX_ERR_NOT_SUPPORTED aggregation
  // failure (despite the possibility that perhaps a lower-priority list of selections could have
  // succeeded if the current list of selections hadn't).
  return ZX_OK;
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
  ZX_DEBUG_ASSERT(!allocation_result_info_.has_value());
  has_allocation_result_ = true;
  SendAllocationResult(root_->BreadthFirstOrder());
  return;
}

void LogicalBufferCollection::SetAllocationResult(
    std::vector<NodeProperties*> visible_pruned_sub_tree,
    fuchsia_sysmem2::BufferCollectionInfo info,
    std::vector<NodeProperties*> whole_pruned_sub_tree) {
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
  allocation_result_info_.emplace(std::move(info));
  has_allocation_result_ = true;
  SendAllocationResult(std::move(visible_pruned_sub_tree));

  std::vector<NodeProperties*>::reverse_iterator next;
  for (auto iter = whole_pruned_sub_tree.rbegin(); iter != whole_pruned_sub_tree.rend();
       iter = next) {
    next = iter + 1;
    auto& np = **iter;
    if (np.buffers_logically_allocated()) {
      // np is part of visible_pruned_sub_tree (or was, before the move above), so we're not failing
      // this item
      continue;
    }
    np.error_propagation_mode() = ErrorPropagationMode::kDoNotPropagate;
    FailDownFrom(&np, ZX_ERR_NOT_SUPPORTED);
  }
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
    std::vector<NodeProperties*> visible_pruned_sub_tree,
    std::vector<NodeProperties*> whole_pruned_sub_tree) {
  ZX_DEBUG_ASSERT(allocation_result().status == ZX_OK);
  for (auto node_properties : visible_pruned_sub_tree) {
    ZX_DEBUG_ASSERT(!node_properties->buffers_logically_allocated());
    node_properties->node()->OnBuffersAllocated(allocation_result());
    ZX_DEBUG_ASSERT(node_properties->buffers_logically_allocated());
  }
  std::vector<NodeProperties*>::reverse_iterator next;
  for (auto iter = whole_pruned_sub_tree.rbegin(); iter != whole_pruned_sub_tree.rend();
       iter = next) {
    next = iter + 1;
    auto& np = **iter;
    if (np.buffers_logically_allocated()) {
      // np is part of visible_pruned_sub_tree, so we're not failing this item
      continue;
    }
    np.error_propagation_mode() = ErrorPropagationMode::kDoNotPropagate;
    FailDownFrom(&np, ZX_ERR_NOT_SUPPORTED);
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
  auto& collection =
      BufferCollection::EmplaceInTree(self, token, zx::unowned_channel(buffer_collection_request));
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
        // We don't complain when a non-initiator participant closes first, since even if we prefer
        // that the initiator close first, the channels are separate so we could see some
        // reordering.
        FailDownFrom(tree_to_fail, status);
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

  if (collection.create_status() != ZX_OK) {
    LogAndFailNode(FROM_HERE, &collection.node_properties(), collection.create_status(),
                   "token.status() failed - create_status: %d", collection.create_status());
    return;
  }

  collection.Bind(std::move(buffer_collection_request));
  // ~self
}

bool LogicalBufferCollection::IsMinBufferSizeSpecifiedByAnyParticipant(
    const ConstraintsList& constraints_list) {
  ZX_DEBUG_ASSERT(root_->connected_client_count() != 0);
  ZX_DEBUG_ASSERT(!constraints_list.empty());
  for (auto& entry : constraints_list) {
    auto& constraints = entry.constraints();
    if (constraints.buffer_memory_constraints().has_value() &&
        constraints.buffer_memory_constraints()->min_size_bytes().has_value() &&
        constraints.buffer_memory_constraints()->min_size_bytes().value() > 0) {
      return true;
    }
    if (constraints.image_format_constraints().has_value()) {
      for (auto& image_format_constraints : constraints.image_format_constraints().value()) {
        if (image_format_constraints.min_coded_width().has_value() &&
            image_format_constraints.min_coded_height().has_value() &&
            image_format_constraints.min_coded_width().value() > 0 &&
            image_format_constraints.min_coded_height().value() > 0) {
          return true;
        }
        if (image_format_constraints.required_max_coded_width().has_value() &&
            image_format_constraints.required_max_coded_height().has_value() &&
            image_format_constraints.required_max_coded_width().value() > 0 &&
            image_format_constraints.required_max_coded_height().value() > 0) {
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
  // Don't do anything during visit of each pruned subtree node, just keep all the pruned subtree
  // nodes in the returned.
  return subtree.BreadthFirstOrder(
      PrunedSubtreeFilter(subtree, [](const NodeProperties& node_properties) { return true; }));
}

fit::function<NodeFilterResult(const NodeProperties&)> LogicalBufferCollection::PrunedSubtreeFilter(
    NodeProperties& subtree, fit::function<bool(const NodeProperties&)> visit_keep) const {
  return [&subtree, visit_keep = std::move(visit_keep)](const NodeProperties& node_properties) {
    bool in_pruned_subtree = false;
    bool iterate_children = true;
    if (&node_properties != &subtree &&
        ((node_properties.error_propagation_mode() == ErrorPropagationMode::kDoNotPropagate) ||
         (node_properties.parent() && node_properties.parent()->which_child().has_value() &&
          &node_properties.parent()->child(*node_properties.parent()->which_child()) !=
              &node_properties))) {
      ZX_DEBUG_ASSERT(!in_pruned_subtree);
      iterate_children = false;
    } else {
      // We know we won't encounter any of the conditions checked above on the way from
      // node_properties to subtree (before reaching subtree) since parents are iterated before
      // children and we don't iterate any child of a node with any of the conditions checked above.
      in_pruned_subtree = true;
    }
    bool keep_node = false;
    if (in_pruned_subtree) {
      keep_node = visit_keep(node_properties);
    }
    return NodeFilterResult{.keep_node = keep_node, .iterate_children = iterate_children};
  };
}

std::vector<NodeProperties*>
LogicalBufferCollection::PrioritizedGroupsOfPrunedSubtreeEligibleForLogicalAllocation(
    NodeProperties& subtree) {
  ZX_DEBUG_ASSERT(!subtree.buffers_logically_allocated());
  ZX_DEBUG_ASSERT((&subtree == root_.get()) || subtree.parent()->buffers_logically_allocated());
  return subtree.DepthFirstPreOrder([&subtree](const NodeProperties& node_properties) {
    // This is only needed for a ZX_DEBUG_ASSERT() below.
    bool in_pruned_subtree = false;
    // By default we iterate children, but if we encounter kDoNotPropagate, we
    // don't iterate children since node_properties is no longer
    // in_pruned_subtree, and no children under node_properties are either.
    bool iterate_children = true;
    if (&node_properties != &subtree &&
        node_properties.error_propagation_mode() == ErrorPropagationMode::kDoNotPropagate) {
      // Groups can't have kDoNotPropagate, so we know iter is not a group.  We also know that
      // node_properties is not in_pruned_subtree since we encountered kDoNotPropagate between
      // node_properties and subtree (before reaching subtree).
      ZX_DEBUG_ASSERT(!node_properties.node()->buffer_collection_token_group());
      ZX_DEBUG_ASSERT(!in_pruned_subtree);
      iterate_children = false;
    } else {
      // We won't encounter any kDoNotPropagate from node_properties up to subtree (before reaching
      // subtree) because we only take this else path if the present node_properties is not
      // kDoNotPropagate, and because if a parent node of this node_properties other than subtree
      // were kDoNotPropagate, we wouldn't be iterating this child node of that parent node.
      in_pruned_subtree = true;
    }
    bool is_group = !!node_properties.node()->buffer_collection_token_group();
    // We can assert that all groups that we actually iterate to are in the
    // pruned subtree, since we iterate only from "subtree" down, and because
    // the nodes that we do potentially iterate which are not in the pruned
    // subtree must be non-group nodes because groups can't have kDoNoPropagate.
    // Since we don't iterate to any children of a kDoNotPropagate node, there's
    // no way for the DepthFirstPreOrder() to get called with node_properties
    // a group that is not in the pruned subtree.  If instead we did iterate
    // children of a kDoNotPropagate node, we could encounter some groups that
    // are not in_pruned_subtree, so we'd need to set
    // keep_node = in_pruned_subtree && is_group.  But since we don't iterate
    // children of a kDoNotPropagate node, we can just assert that it's
    // impossible to be iterating a group that is not in the pruned subtree, and
    // we don't need to include in_pruned_subtree in the keep_node expression
    // since we know that if is_group is true, in_pruned_subtree must also be
    // true, and if in_pruned_subtree is false, is_group is also false.
    ZX_DEBUG_ASSERT(!is_group || in_pruned_subtree);
    // The keep_node expressions with or without in_pruned_subtree will always
    // have the same value (in this method), so we can just use "in_group" (in
    // this method) since it's a simpler expression that accomplishes the same
    // thing (in this method, not necessarily in other methods that still need
    // in_pruned_subtree).
    ZX_DEBUG_ASSERT((in_pruned_subtree && is_group) == is_group);
    return NodeFilterResult{.keep_node = is_group, .iterate_children = iterate_children};
  });
}

fpromise::result<fuchsia_sysmem2::BufferCollectionConstraints, void>
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
    return fpromise::error();
  }

  // Start with empty constraints / unconstrained.
  fuchsia_sysmem2::BufferCollectionConstraints acc;
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
      return fpromise::error();
    }
    if (!AccumulateConstraintBufferCollection(&acc,
                                              std::move(constraints_entry.mutate_constraints()))) {
      // This is a failure.  The space of permitted settings contains no
      // points.
      return fpromise::error();
    }
  }

  if (!CheckSanitizeBufferCollectionConstraints(CheckSanitizeStage::kAggregated, acc)) {
    return fpromise::error();
  }

  LogInfo(FROM_HERE, "After combining constraints:");
  LogConstraints(FROM_HERE, nullptr, acc);

  return fpromise::ok(std::move(acc));
}

// TODO(dustingreen): Consider rejecting secure_required + any non-secure heaps, including the
// potentially-implicit SYSTEM_RAM heap.
//
// TODO(dustingreen): From a particular participant, CPU usage without
// IsCpuAccessibleHeapPermitted() should fail.
//
// TODO(dustingreen): From a particular participant, be more picky about which domains are supported
// vs. which heaps are supported.
static bool IsHeapPermitted(const fuchsia_sysmem2::BufferMemoryConstraints& constraints,
                            fuchsia_sysmem2::HeapType heap) {
  if (constraints.heap_permitted()->size()) {
    auto begin = constraints.heap_permitted().value().begin();
    auto end = constraints.heap_permitted().value().end();
    return std::find(begin, end, heap) != end;
  }
  // Zero heaps in heap_permitted() means any heap is ok.
  return true;
}

static bool IsSecurePermitted(const fuchsia_sysmem2::BufferMemoryConstraints& constraints) {
  // TODO(fxbug.dev/37452): Generalize this by finding if there's a heap that maps to secure
  // MemoryAllocator in the permitted heaps.
  return constraints.inaccessible_domain_supported().value() &&
         (IsHeapPermitted(constraints, fuchsia_sysmem2::HeapType::kAmlogicSecure) ||
          IsHeapPermitted(constraints, fuchsia_sysmem2::HeapType::kAmlogicSecureVdec));
}

static bool IsCpuAccessSupported(const fuchsia_sysmem2::BufferMemoryConstraints& constraints) {
  return constraints.cpu_domain_supported().value() || constraints.ram_domain_supported().value();
}

bool LogicalBufferCollection::CheckSanitizeBufferUsage(CheckSanitizeStage stage,
                                                       fuchsia_sysmem2::BufferUsage& buffer_usage) {
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
      if (*buffer_usage.none() == 0 && *buffer_usage.cpu() == 0 && *buffer_usage.vulkan() == 0 &&
          *buffer_usage.display() == 0 && *buffer_usage.video() == 0) {
        LogError(FROM_HERE, "At least one usage bit must be set by a participant.");
        return false;
      }
      if (*buffer_usage.none() != 0) {
        if (*buffer_usage.cpu() != 0 || *buffer_usage.vulkan() != 0 ||
            *buffer_usage.display() != 0 || *buffer_usage.video() != 0) {
          LogError(FROM_HERE,
                   "A participant indicating 'none' usage can't specify any other usage.");
          return false;
        }
      }
      break;
    case CheckSanitizeStage::kAggregated:
      if (*buffer_usage.cpu() == 0 && *buffer_usage.vulkan() == 0 && *buffer_usage.display() == 0 &&
          *buffer_usage.video() == 0) {
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
    CheckSanitizeStage stage, fuchsia_sysmem2::BufferCollectionConstraints& constraints) {
  bool was_empty = constraints.IsEmpty();
  FIELD_DEFAULT_SET(constraints, usage);
  if (was_empty) {
    // Completely empty constraints are permitted, so convert to NONE_USAGE to avoid triggering the
    // check applied to non-empty constraints where at least one usage bit must be set (NONE_USAGE
    // counts for that check, and doesn't constrain anything).
    FIELD_DEFAULT(constraints.usage().value(), none, fuchsia_sysmem2::kNoneUsage);
  }
  FIELD_DEFAULT_ZERO(constraints, min_buffer_count_for_camping);
  FIELD_DEFAULT_ZERO(constraints, min_buffer_count_for_dedicated_slack);
  FIELD_DEFAULT_ZERO(constraints, min_buffer_count_for_shared_slack);
  FIELD_DEFAULT_ZERO(constraints, min_buffer_count);
  FIELD_DEFAULT_MAX(constraints, max_buffer_count);
  ZX_DEBUG_ASSERT(constraints.buffer_memory_constraints().has_value() ||
                  stage != CheckSanitizeStage::kAggregated);
  FIELD_DEFAULT_SET(constraints, buffer_memory_constraints);
  ZX_DEBUG_ASSERT(constraints.buffer_memory_constraints().has_value());
  FIELD_DEFAULT_SET_VECTOR(constraints, image_format_constraints, InitialCapacityOrZero(stage, 64));
  FIELD_DEFAULT_FALSE(constraints, need_clear_aux_buffers_for_secure);
  FIELD_DEFAULT(constraints, allow_clear_aux_buffers_for_secure,
                !IsWriteUsage(constraints.usage().value()));
  if (!CheckSanitizeBufferUsage(stage, constraints.usage().value())) {
    LogError(FROM_HERE, "CheckSanitizeBufferUsage() failed");
    return false;
  }
  if (*constraints.max_buffer_count() == 0) {
    LogError(FROM_HERE, "max_buffer_count == 0");
    return false;
  }
  if (*constraints.min_buffer_count() > *constraints.max_buffer_count()) {
    LogError(FROM_HERE, "min_buffer_count > max_buffer_count");
    return false;
  }
  if (!CheckSanitizeBufferMemoryConstraints(stage, constraints.usage().value(),
                                            constraints.buffer_memory_constraints().value())) {
    return false;
  }
  if (stage != CheckSanitizeStage::kAggregated) {
    if (IsCpuUsage(constraints.usage().value())) {
      if (!IsCpuAccessSupported(constraints.buffer_memory_constraints().value())) {
        LogError(FROM_HERE, "IsCpuUsage() && !IsCpuAccessSupported()");
        return false;
      }
      // From a single participant, reject secure_required in combination with CPU usage, since CPU
      // usage isn't possible given secure memory.
      if (constraints.buffer_memory_constraints()->secure_required().value()) {
        LogError(FROM_HERE, "IsCpuUsage() && secure_required");
        return false;
      }
      // It's fine if a participant sets CPU usage but also permits inaccessible domain and possibly
      // IsSecurePermitted().  In that case the participant is expected to pay attention to the
      // coherency domain and is_secure and realize that it shouldn't attempt to read/write the
      // VMOs.
    }
    if (constraints.buffer_memory_constraints()->secure_required().value() &&
        IsCpuAccessSupported(constraints.buffer_memory_constraints().value())) {
      // This is a little picky, but easier to be less picky later than more picky later.
      LogError(FROM_HERE, "secure_required && IsCpuAccessSupported()");
      return false;
    }
  }

  auto is_pixel_format_do_not_care_result =
      IsImageFormatConstraintsArrayPixelFormatDoNotCare(*constraints.image_format_constraints());
  // Here is where is_error() is "checked previously" for PixelFormatType re. DO_NOT_CARE.
  if (is_pixel_format_do_not_care_result.is_error()) {
    LogError(FROM_HERE, "malformed PixelFormat (possibly involving DO_NOT_CARE)");
    return false;
  }
  bool is_pixel_format_do_not_care = is_pixel_format_do_not_care_result.value();
  for (uint32_t i = 0; i < constraints.image_format_constraints()->size(); ++i) {
    if (!CheckSanitizeImageFormatConstraints(stage,
                                             constraints.image_format_constraints()->at(i))) {
      return false;
    }
  }

  if (stage == CheckSanitizeStage::kAggregated) {
    if (constraints.image_format_constraints().has_value()) {
      if (is_pixel_format_do_not_care) {
        // By design, sysmem does not arbitrarily select a colorspace from among all color spaces
        // without any participant-specified pixel format constraints, as doing so would be likely
        // to lead to unexpected changes to the resulting pixel format when additional pixel formats
        // are added to PixelFormatType.
        LogError(FROM_HERE, "at least one participant must specify PixelFormatType != DO_NOT_CARE");
        return false;
      }
      for (uint32_t i = 0; i < constraints.image_format_constraints()->size(); ++i) {
        auto& ifc = constraints.image_format_constraints()->at(i);
        auto is_color_space_do_not_care_result = IsColorSpaceArrayDoNotCare(*ifc.color_spaces());
        // maintained during accumulation
        ZX_DEBUG_ASSERT(is_color_space_do_not_care_result.is_ok());
        bool is_color_space_do_not_care = is_color_space_do_not_care_result.value();
        if (is_color_space_do_not_care) {
          // Both producers and consumers ("active" participants) are required to specify specific
          // color_spaces by design, with the only exception being kPassThrough.
          //
          // Only more passive participants should ever specify ColorSpaceType kDoNotCare.  If an
          // active participant really does not care, it can instead list all the color spaces.  In
          // a few scenarios it may be fine for participants to all specify kPassThrough, if there's
          // no reason to add a particular highly-custom and/or not-actually-color-as-such space to
          // the ColorSpaceType enum, or if all participants are all truly intending to just pass
          // through the color space no matter what it is with no need for sysmem to select a color
          // space and the special scenario would otherwise involve (by intent of design) _all_ the
          // participants wanting to set kDoNotCare (which would lead to this error).
          //
          // The preferred fix most of the time is specifying a specific color space in at least one
          // participant.  Much less commonly, and only if actually necessary, kPassThrough can be
          // used by all participants instead (see previous paragraph).
          LogInfo(FROM_HERE,
                  "per-PixelFormatType, at least one participant must specify ColorSpaceType != "
                  "kDoNotCare - removing PixelFormatType: type: %u modifier: 0x%" PRIx64,
                  *ifc.pixel_format()->type(),
                  ifc.pixel_format()->format_modifier_value().has_value()
                      ? *ifc.pixel_format()->format_modifier_value()
                      : 0ull);
          // Remove by copying down last PixelFormat to this index and processing this index again,
          // if this isn't already the last PixelFormat.
          if (i != constraints.image_format_constraints()->size() - 1) {
            constraints.image_format_constraints()->at(i) =
                std::move(constraints.image_format_constraints()->at(
                    constraints.image_format_constraints()->size() - 1));
            --i;
          }
          constraints.image_format_constraints()->resize(
              constraints.image_format_constraints()->size() - 1);
          if (constraints.image_format_constraints()->size() == 0) {
            LogError(FROM_HERE,
                     "after removing pixel format that remained ColorSpaceType kDoNotCare, zero "
                     "pixel formats remaining");
            return false;
          }
        }
      }
    }

    // Given the image constriant's pixel format, select the best color space
    for (auto& image_constraint : constraints.image_format_constraints().value()) {
      // We are guaranteed that color spaces are valid for the current pixel format
      if (image_constraint.color_spaces().has_value()) {
        auto best_color_space = fuchsia_sysmem2::ColorSpaceType::kInvalid;

        for (auto& color_space : image_constraint.color_spaces().value()) {
          if (color_space.type().has_value()) {
            auto best_ranking =
                kColorSpaceRanking.at(sysmem::fidl_underlying_cast(best_color_space));
            auto current_ranking =
                kColorSpaceRanking.at(sysmem::fidl_underlying_cast(color_space.type().value()));

            if (best_ranking > current_ranking) {
              best_color_space = color_space.type().value();
            }
          }
        }

        // Set the best color space
        image_constraint.color_spaces()->resize(0);
        fuchsia_sysmem2::ColorSpace color_space;
        color_space.type().emplace(best_color_space);
        image_constraint.color_spaces()->emplace_back(std::move(color_space));
      }
    }
  }

  if (stage == CheckSanitizeStage::kNotAggregated) {
    // As an optimization, only check the unaggregated inputs.
    for (uint32_t i = 0; i < constraints.image_format_constraints()->size(); ++i) {
      for (uint32_t j = i + 1; j < constraints.image_format_constraints()->size(); ++j) {
        if (ImageFormatIsPixelFormatEqual(
                constraints.image_format_constraints()->at(i).pixel_format().value(),
                constraints.image_format_constraints()->at(j).pixel_format().value())) {
          LogError(FROM_HERE, "image format constraints %d and %d have identical formats", i, j);
          return false;
        }
      }
    }
  }

  return true;
}

bool LogicalBufferCollection::CheckSanitizeBufferMemoryConstraints(
    CheckSanitizeStage stage, const fuchsia_sysmem2::BufferUsage& buffer_usage,
    fuchsia_sysmem2::BufferMemoryConstraints& constraints) {
  FIELD_DEFAULT_ZERO(constraints, min_size_bytes);
  FIELD_DEFAULT_MAX(constraints, max_size_bytes);
  FIELD_DEFAULT_FALSE(constraints, physically_contiguous_required);
  FIELD_DEFAULT_FALSE(constraints, secure_required);
  // The CPU domain is supported by default.
  FIELD_DEFAULT(constraints, cpu_domain_supported, true);
  // If !usage.cpu, then participant doesn't care what domain, so indicate support
  // for RAM and inaccessible domains in that case.  This only takes effect if the participant
  // didn't explicitly specify a value for these fields.
  FIELD_DEFAULT(constraints, ram_domain_supported, !*buffer_usage.cpu());
  FIELD_DEFAULT(constraints, inaccessible_domain_supported, !*buffer_usage.cpu());
  if (stage != CheckSanitizeStage::kAggregated) {
    if (constraints.heap_permitted().has_value() && constraints.heap_permitted()->empty()) {
      LogError(FROM_HERE,
               "constraints.has_heap_permitted() && constraints.heap_permitted().empty()");
      return false;
    }
  }
  // TODO(dustingreen): When 0 heaps specified, constrain heap list based on other constraints.
  // For now 0 heaps means any heap.
  FIELD_DEFAULT_SET_VECTOR(constraints, heap_permitted, 0);
  ZX_DEBUG_ASSERT(stage != CheckSanitizeStage::kInitial || constraints.heap_permitted()->empty());
  if (*constraints.min_size_bytes() > *constraints.max_size_bytes()) {
    LogError(FROM_HERE, "min_size_bytes > max_size_bytes");
    return false;
  }
  if (*constraints.secure_required() && !IsSecurePermitted(constraints)) {
    LogError(FROM_HERE, "secure memory required but not permitted");
    return false;
  }
  return true;
}

bool LogicalBufferCollection::CheckSanitizeImageFormatConstraints(
    CheckSanitizeStage stage, fuchsia_sysmem2::ImageFormatConstraints& constraints) {
  // We never CheckSanitizeImageFormatConstraints() on empty (aka initial) constraints.
  ZX_DEBUG_ASSERT(stage != CheckSanitizeStage::kInitial);

  FIELD_DEFAULT_SET(constraints, pixel_format);
  FIELD_DEFAULT_ZERO(constraints.pixel_format().value(), type);
  FIELD_DEFAULT_ZERO_64_BIT(constraints.pixel_format().value(), format_modifier_value);

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

  if (constraints.pixel_format()->type().value() == fuchsia_sysmem2::PixelFormatType::kInvalid) {
    LogError(FROM_HERE, "PixelFormatType INVALID not allowed");
    return false;
  }

  auto is_pixel_format_do_not_care_result =
      IsImageFormatConstraintsPixelFormatDoNotCare(constraints);
  // checked previously
  ZX_DEBUG_ASSERT(is_pixel_format_do_not_care_result.is_ok());
  bool is_pixel_format_do_not_care = is_pixel_format_do_not_care_result.value();
  if (!is_pixel_format_do_not_care) {
    if (!ImageFormatIsSupported(constraints.pixel_format().value())) {
      LogError(FROM_HERE, "Unsupported pixel format");
      return false;
    }
    uint32_t min_bytes_per_row_given_min_width =
        ImageFormatStrideBytesPerWidthPixel(constraints.pixel_format().value()) *
        constraints.min_coded_width().value();
    constraints.min_bytes_per_row().emplace(
        std::max(constraints.min_bytes_per_row().value(), min_bytes_per_row_given_min_width));
  }

  if (constraints.color_spaces()->empty()) {
    LogError(FROM_HERE, "color_spaces.empty() not allowed");
    return false;
  }

  if (constraints.min_coded_width().value() > constraints.max_coded_width().value()) {
    LogError(FROM_HERE, "min_coded_width > max_coded_width");
    return false;
  }
  if (constraints.min_coded_height().value() > constraints.max_coded_height().value()) {
    LogError(FROM_HERE, "min_coded_height > max_coded_height");
    return false;
  }
  if (constraints.min_bytes_per_row().value() > constraints.max_bytes_per_row().value()) {
    LogError(FROM_HERE, "min_bytes_per_row > max_bytes_per_row");
    return false;
  }
  if (constraints.min_coded_width().value() * constraints.min_coded_height().value() >
      constraints.max_coded_width_times_coded_height().value()) {
    LogError(FROM_HERE,
             "min_coded_width * min_coded_height > "
             "max_coded_width_times_coded_height");
    return false;
  }

  if (!IsNonZeroPowerOf2(*constraints.coded_width_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 coded_width_divisor not supported");
    return false;
  }
  if (!IsNonZeroPowerOf2(*constraints.coded_height_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 coded_width_divisor not supported");
    return false;
  }
  if (!IsNonZeroPowerOf2(*constraints.bytes_per_row_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 bytes_per_row_divisor not supported");
    return false;
  }
  if (!IsNonZeroPowerOf2(*constraints.start_offset_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 start_offset_divisor not supported");
    return false;
  }
  if (*constraints.start_offset_divisor() > zx_system_get_page_size()) {
    LogError(FROM_HERE,
             "support for start_offset_divisor > zx_system_get_page_size() not yet implemented");
    return false;
  }
  if (!IsNonZeroPowerOf2(*constraints.display_width_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 display_width_divisor not supported");
    return false;
  }
  if (!IsNonZeroPowerOf2(*constraints.display_height_divisor())) {
    LogError(FROM_HERE, "non-power-of-2 display_height_divisor not supported");
    return false;
  }

  // Pre-check this to make error easier to diagnose vs. error from IsColorSpaceArrayDoNotCare(),
  // since this requirement applies regardless of DO_NOT_CARE true or false.
  for (uint32_t i = 0; i < constraints.color_spaces()->size(); ++i) {
    if (!constraints.color_spaces()->at(i).type().has_value()) {
      LogError(FROM_HERE, "color_spaces.type must be set");
      return false;
    }
  }

  if (!is_pixel_format_do_not_care) {
    auto is_color_space_do_not_care_result =
        IsColorSpaceArrayDoNotCare(*constraints.color_spaces());
    // Here is where is_error() is "checked previously" for ColorSpaceType re. DO_NOT_CARE.
    if (is_color_space_do_not_care_result.is_error()) {
      LogError(FROM_HERE, "malformed color_spaces re. DO_NOT_CARE");
      return false;
    }
    bool is_color_space_do_not_care = is_color_space_do_not_care_result.value();
    if (!is_color_space_do_not_care) {
      for (uint32_t i = 0; i < constraints.color_spaces()->size(); ++i) {
        if (!ImageFormatIsSupportedColorSpaceForPixelFormat(constraints.color_spaces()->at(i),
                                                            *constraints.pixel_format())) {
          auto colorspace_type = constraints.color_spaces()->at(i).type().has_value()
                                     ? *constraints.color_spaces()->at(i).type()
                                     : fuchsia_sysmem2::wire::ColorSpaceType::kInvalid;
          LogError(FROM_HERE,
                   "!ImageFormatIsSupportedColorSpaceForPixelFormat() "
                   "color_space.type: %u "
                   "pixel_format.type: %u",
                   sysmem::fidl_underlying_cast(colorspace_type),
                   sysmem::fidl_underlying_cast(*constraints.pixel_format()->type()));
          return false;
        }
      }
    }
  }

  if (*constraints.required_min_coded_width() == 0) {
    LogError(FROM_HERE, "required_min_coded_width == 0");
    return false;
  }
  ZX_DEBUG_ASSERT(*constraints.required_min_coded_width() != 0);
  if (*constraints.required_min_coded_width() < *constraints.min_coded_width()) {
    LogError(FROM_HERE, "required_min_coded_width < min_coded_width");
    return false;
  }
  if (*constraints.required_max_coded_width() > *constraints.max_coded_width()) {
    LogError(FROM_HERE, "required_max_coded_width > max_coded_width");
    return false;
  }
  if (*constraints.required_min_coded_height() == 0) {
    LogError(FROM_HERE, "required_min_coded_height == 0");
    return false;
  }
  ZX_DEBUG_ASSERT(*constraints.required_min_coded_height() != 0);
  if (*constraints.required_min_coded_height() < *constraints.min_coded_height()) {
    LogError(FROM_HERE, "required_min_coded_height < min_coded_height");
    return false;
  }
  if (*constraints.required_max_coded_height() > *constraints.max_coded_height()) {
    LogError(FROM_HERE, "required_max_coded_height > max_coded_height");
    return false;
  }
  if (*constraints.required_min_bytes_per_row() == 0) {
    LogError(FROM_HERE, "required_min_bytes_per_row == 0");
    return false;
  }
  ZX_DEBUG_ASSERT(*constraints.required_min_bytes_per_row() != 0);
  if (*constraints.required_min_bytes_per_row() < *constraints.min_bytes_per_row()) {
    LogError(FROM_HERE, "required_min_bytes_per_row < min_bytes_per_row");
    return false;
  }
  if (*constraints.required_max_bytes_per_row() > *constraints.max_bytes_per_row()) {
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

bool LogicalBufferCollection::AccumulateConstraintsBufferUsage(fuchsia_sysmem2::BufferUsage* acc,
                                                               fuchsia_sysmem2::BufferUsage c) {
  // We accumulate "none" usage just like other usages, to make aggregation and CheckSanitize
  // consistent/uniform.
  *acc->none() |= *c.none();
  *acc->cpu() |= *c.cpu();
  *acc->vulkan() |= *c.vulkan();
  *acc->display() |= *c.display();
  *acc->video() |= *c.video();
  return true;
}

// |acc| accumulated constraints so far
//
// |c| additional constraint to aggregate into acc
bool LogicalBufferCollection::AccumulateConstraintBufferCollection(
    fuchsia_sysmem2::BufferCollectionConstraints* acc,
    fuchsia_sysmem2::BufferCollectionConstraints c) {
  if (!AccumulateConstraintsBufferUsage(&acc->usage().value(), std::move(c.usage().value()))) {
    return false;
  }

  acc->min_buffer_count_for_camping().value() += c.min_buffer_count_for_camping().value();
  acc->min_buffer_count_for_dedicated_slack().value() +=
      c.min_buffer_count_for_dedicated_slack().value();
  acc->min_buffer_count_for_shared_slack().emplace(
      std::max(acc->min_buffer_count_for_shared_slack().value(),
               c.min_buffer_count_for_shared_slack().value()));
  acc->min_buffer_count().emplace(
      std::max(acc->min_buffer_count().value(), c.min_buffer_count().value()));

  // 0 is replaced with 0xFFFFFFFF in
  // CheckSanitizeBufferCollectionConstraints.
  ZX_DEBUG_ASSERT(acc->max_buffer_count().value() != 0);
  ZX_DEBUG_ASSERT(c.max_buffer_count().value() != 0);
  acc->max_buffer_count().emplace(
      std::min(acc->max_buffer_count().value(), c.max_buffer_count().value()));

  // CheckSanitizeBufferCollectionConstraints() takes care of setting a default
  // buffer_collection_constraints, so we can assert that both acc and c "has_" one.
  ZX_DEBUG_ASSERT(acc->buffer_memory_constraints().has_value());
  ZX_DEBUG_ASSERT(c.buffer_memory_constraints().has_value());
  if (!AccumulateConstraintBufferMemory(&acc->buffer_memory_constraints().value(),
                                        std::move(c.buffer_memory_constraints().value()))) {
    return false;
  }

  if (acc->image_format_constraints()->empty()) {
    // Take the whole VectorView<>, as the count() can only go down later, so the capacity of
    // c.image_format_constraints() is fine.
    acc->image_format_constraints().emplace(std::move(c.image_format_constraints().value()));
  } else {
    ZX_DEBUG_ASSERT(!acc->image_format_constraints()->empty());
    if (!c.image_format_constraints()->empty()) {
      if (!AccumulateConstraintImageFormats(&acc->image_format_constraints().value(),
                                            std::move(c.image_format_constraints().value()))) {
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
      ZX_DEBUG_ASSERT(!acc->image_format_constraints()->empty());
    }
  }

  acc->need_clear_aux_buffers_for_secure().emplace(
      acc->need_clear_aux_buffers_for_secure().value() ||
      c.need_clear_aux_buffers_for_secure().value());
  acc->allow_clear_aux_buffers_for_secure().emplace(
      acc->allow_clear_aux_buffers_for_secure().value() &&
      c.allow_clear_aux_buffers_for_secure().value());
  // We check for consistency of these later only if we're actually attempting to allocate secure
  // buffers.

  // acc->image_format_constraints().count() == 0 is allowed here, when all
  // participants had image_format_constraints().count() == 0.
  return true;
}

bool LogicalBufferCollection::AccumulateConstraintHeapPermitted(
    std::vector<fuchsia_sysmem2::HeapType>* acc, std::vector<fuchsia_sysmem2::HeapType> c) {
  // Remove any heap in acc that's not in c.  If zero heaps
  // remain in acc, return false.
  ZX_DEBUG_ASSERT(acc->size() > 0);

  for (uint32_t ai = 0; ai < acc->size(); ++ai) {
    uint32_t ci;
    for (ci = 0; ci < c.size(); ++ci) {
      if ((*acc)[ai] == c[ci]) {
        // We found heap in c.  Break so we can move on to
        // the next heap.
        break;
      }
    }
    if (ci == c.size()) {
      // Remove from acc because not found in c.
      //
      // Copy formerly last item on top of the item being removed, if not the same item.
      if (ai != acc->size() - 1) {
        (*acc)[ai] = (*acc)[acc->size() - 1];
      }
      // remove last item
      acc->resize(acc->size() - 1);
      // adjust ai to force current index to be processed again as it's
      // now a different item
      --ai;
    }
  }

  if (acc->empty()) {
    LogError(FROM_HERE, "Zero heap permitted overlap");
    return false;
  }

  return true;
}

bool LogicalBufferCollection::AccumulateConstraintBufferMemory(
    fuchsia_sysmem2::BufferMemoryConstraints* acc, fuchsia_sysmem2::BufferMemoryConstraints c) {
  acc->min_size_bytes() = std::max(*acc->min_size_bytes(), *c.min_size_bytes());

  // Don't permit 0 as the overall min_size_bytes; that would be nonsense.  No
  // particular initiator should feel that it has to specify 1 in this field;
  // that's just built into sysmem instead.  While a VMO will have a minimum
  // actual size of page size, we do permit treating buffers as if they're 1
  // byte, mainly for testing reasons, and to avoid any unnecessary dependence
  // or assumptions re. page size.
  acc->min_size_bytes() = std::max(*acc->min_size_bytes(), 1u);
  acc->max_size_bytes() = std::min(*acc->max_size_bytes(), *c.max_size_bytes());

  acc->physically_contiguous_required() =
      *acc->physically_contiguous_required() || *c.physically_contiguous_required();

  acc->secure_required() = *acc->secure_required() || *c.secure_required();

  acc->ram_domain_supported() = *acc->ram_domain_supported() && *c.ram_domain_supported();
  acc->cpu_domain_supported() = *acc->cpu_domain_supported() && *c.cpu_domain_supported();
  acc->inaccessible_domain_supported() =
      *acc->inaccessible_domain_supported() && *c.inaccessible_domain_supported();

  if (acc->heap_permitted()->empty()) {
    acc->heap_permitted().emplace(std::move(*c.heap_permitted()));
  } else {
    if (!c.heap_permitted()->empty()) {
      if (!AccumulateConstraintHeapPermitted(&*acc->heap_permitted(),
                                             std::move(*c.heap_permitted()))) {
        return false;
      }
    }
  }
  return true;
}

bool LogicalBufferCollection::AccumulateConstraintImageFormats(
    std::vector<fuchsia_sysmem2::ImageFormatConstraints>* acc,
    std::vector<fuchsia_sysmem2::ImageFormatConstraints> c) {
  // Remove any pixel_format in acc that's not in c.  Process any format
  // that's in both.  If processing the format results in empty set for that
  // format, pretend as if the format wasn't in c and remove that format from
  // acc.  If acc ends up with zero formats, return false.

  // This method doesn't get called unless there's at least one format in
  // acc.
  ZX_DEBUG_ASSERT(!acc->empty());

  // Any pixel_format kDoNotCare can only happen with count() == 1, checked previously.  If both
  // acc and c are indicating kDoNotCare, the result still needs to be kDoNotCare.  If only one of
  // acc or c is indicating kDoNotCare, we need to fan out the kDoNotCare (via cloning) and replace
  // each of the resulting pixel_format fields with the specific (not kDoNotCare) pixel_format(s)
  // indicated by the other (of acc and c).  After this, accumulation can proceed as normal, with
  // kDoNotCare (if still present) treated as any other normal PixelFormatType.  At the end of
  // overall accumulation, we must check (elsewhere) that we're not left with only a single
  // kDoNotCare pixel_format.
  auto acc_is_do_not_care_result = IsImageFormatConstraintsArrayPixelFormatDoNotCare(*acc);
  // maintained as we accumulate, largely thanks to each c having been checked previously
  ZX_DEBUG_ASSERT(acc_is_do_not_care_result.is_ok());
  bool acc_is_do_not_care = acc_is_do_not_care_result.value();
  auto c_is_do_not_care_result = IsImageFormatConstraintsArrayPixelFormatDoNotCare(c);
  // checked previously
  ZX_DEBUG_ASSERT(c_is_do_not_care_result.is_ok());
  auto c_is_do_not_care = c_is_do_not_care_result.value();
  if (acc_is_do_not_care && !c_is_do_not_care) {
    // replicate acc entries to correspond to c entries
    ReplicatePixelFormatDoNotCare(c, *acc);
  } else if (!acc_is_do_not_care && c_is_do_not_care) {
    // replicate c entries to correspond to acc entries
    ReplicatePixelFormatDoNotCare(*acc, c);
  } else {
    // Either both are pixel_format kDoNotCare, or neither are.
    ZX_DEBUG_ASSERT(acc_is_do_not_care == c_is_do_not_care);
  }

  for (uint32_t ai = 0; ai < acc->size(); ++ai) {
    bool is_found_in_c = false;
    for (size_t ci = 0; ci < c.size(); ++ci) {
      if (ImageFormatIsPixelFormatEqual(*(*acc)[ai].pixel_format(), *c[ci].pixel_format())) {
        // Move last entry into the entry we're consuming, since LLCPP FIDL tables don't have any
        // way to detect that they've been moved out of, so we need to keep c tightly packed with
        // not-moved-out-of entries.  We don't need to adjust ci to stay at the same entry for the
        // next iteration of the loop because by this point we know we're done scanning c in this
        // iteration of the ai loop.
        fuchsia_sysmem2::ImageFormatConstraints old_c_ci = std::move(c[ci]);
        if (ci != c.size() - 1) {
          c[ci] = std::move(c[c.size() - 1]);
        }
        c.resize(c.size() - 1);
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
      if (ai != acc->size() - 1) {
        (*acc)[ai] = std::move((*acc)[acc->size() - 1]);
      } else {
        // Stuff under this item would get deleted later anyway, but delete now to avoid keeping
        // cruft we don't need.
        (*acc)[ai] = fuchsia_sysmem2::ImageFormatConstraints();
      }
      // remove last item
      acc->resize(acc->size() - 1);
      // adjust ai to force current index to be processed again as it's
      // now a different item
      --ai;
    }
  }

  if (acc->empty()) {
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
    fuchsia_sysmem2::ImageFormatConstraints* acc, fuchsia_sysmem2::ImageFormatConstraints c) {
  ZX_DEBUG_ASSERT(ImageFormatIsPixelFormatEqual(*acc->pixel_format(), *c.pixel_format()));
  // Checked previously.
  ZX_DEBUG_ASSERT(!acc->color_spaces()->empty());
  // Checked previously.
  ZX_DEBUG_ASSERT(!c.color_spaces()->empty());

  if (!AccumulateConstraintColorSpaces(&*acc->color_spaces(), std::move(*c.color_spaces()))) {
    return false;
  }
  // Else AccumulateConstraintColorSpaces() would have returned false.
  ZX_DEBUG_ASSERT(!acc->color_spaces()->empty());

  acc->min_coded_width() = std::max(*acc->min_coded_width(), *c.min_coded_width());
  acc->max_coded_width() = std::min(*acc->max_coded_width(), *c.max_coded_width());
  acc->min_coded_height() = std::max(*acc->min_coded_height(), *c.min_coded_height());
  acc->max_coded_height() = std::min(*acc->max_coded_height(), *c.max_coded_height());
  acc->min_bytes_per_row() = std::max(*acc->min_bytes_per_row(), *c.min_bytes_per_row());
  acc->max_bytes_per_row() = std::min(*acc->max_bytes_per_row(), *c.max_bytes_per_row());
  acc->max_coded_width_times_coded_height() =
      std::min(*acc->max_coded_width_times_coded_height(), *c.max_coded_width_times_coded_height());

  // For these, see also the conditional statement below that ensures these are fixed up with any
  // pixel-format-dependent adjustment.
  acc->coded_width_divisor() = std::max(*acc->coded_width_divisor(), *c.coded_width_divisor());
  acc->coded_height_divisor() = std::max(*acc->coded_height_divisor(), *c.coded_height_divisor());
  acc->bytes_per_row_divisor() =
      std::max(*acc->bytes_per_row_divisor(), *c.bytes_per_row_divisor());
  acc->start_offset_divisor() = std::max(*acc->start_offset_divisor(), *c.start_offset_divisor());

  // When acc still has kDoNotCare (when this condition is false), we're guaranteed to either end up
  // here again later in aggregation with the condition true, or to fail the overall aggregation if
  // acc still has kDoNotCare at the end of aggregation.  This way, we know that we'll take the
  // pixel format's contribution to these values into consideration before the end of aggregation,
  // or we'll fail the aggregation anyway.
  auto acc_is_pixel_format_do_not_care_result = IsImageFormatConstraintsPixelFormatDoNotCare(*acc);
  // maintained during accumulation, largely thanks to each c having been checked previously
  ZX_DEBUG_ASSERT(acc_is_pixel_format_do_not_care_result.is_ok());
  bool acc_is_pixel_format_do_not_care = acc_is_pixel_format_do_not_care_result.value();
  if (!acc_is_pixel_format_do_not_care) {
    acc->coded_width_divisor() = std::max(*acc->coded_width_divisor(),
                                          ImageFormatCodedWidthMinDivisor(*acc->pixel_format()));
    acc->coded_height_divisor() = std::max(*acc->coded_height_divisor(),
                                           ImageFormatCodedHeightMinDivisor(*acc->pixel_format()));
    acc->bytes_per_row_divisor() =
        std::max(*acc->bytes_per_row_divisor(), ImageFormatSampleAlignment(*acc->pixel_format()));
    acc->start_offset_divisor() =
        std::max(*acc->start_offset_divisor(), ImageFormatSampleAlignment(*acc->pixel_format()));
  }

  acc->display_width_divisor() =
      std::max(*acc->display_width_divisor(), *c.display_width_divisor());
  acc->display_height_divisor() =
      std::max(*acc->display_height_divisor(), *c.display_height_divisor());

  // The required_ space is accumulated by taking the union, and must be fully
  // within the non-required_ space, else fail.  For example, this allows a
  // video decoder to indicate that it's capable of outputting a wide range of
  // output dimensions, but that it has specific current dimensions that are
  // presently required_ (min == max) for decode to proceed.
  ZX_DEBUG_ASSERT(*acc->required_min_coded_width() != 0);
  ZX_DEBUG_ASSERT(*c.required_min_coded_width() != 0);
  acc->required_min_coded_width() =
      std::min(*acc->required_min_coded_width(), *c.required_min_coded_width());
  acc->required_max_coded_width() =
      std::max(*acc->required_max_coded_width(), *c.required_max_coded_width());
  ZX_DEBUG_ASSERT(*acc->required_min_coded_height() != 0);
  ZX_DEBUG_ASSERT(*c.required_min_coded_height() != 0);
  acc->required_min_coded_height() =
      std::min(*acc->required_min_coded_height(), *c.required_min_coded_height());
  acc->required_max_coded_height() =
      std::max(*acc->required_max_coded_height(), *c.required_max_coded_height());
  ZX_DEBUG_ASSERT(*acc->required_min_bytes_per_row() != 0);
  ZX_DEBUG_ASSERT(*c.required_min_bytes_per_row() != 0);
  acc->required_min_bytes_per_row() =
      std::min(*acc->required_min_bytes_per_row(), *c.required_min_bytes_per_row());
  acc->required_max_bytes_per_row() =
      std::max(*acc->required_max_bytes_per_row(), *c.required_max_bytes_per_row());

  return true;
}

bool LogicalBufferCollection::AccumulateConstraintColorSpaces(
    std::vector<fuchsia_sysmem2::ColorSpace>* acc, std::vector<fuchsia_sysmem2::ColorSpace> c) {
  // Any ColorSpace kDoNotCare can only happen with count() == 1, checked previously.  If both acc
  // and c are indicating kDoNotCare, the result still needs to be kDoNotCare.  If only one of acc
  // or c is indicating kDoNotCare, we need to fan out the kDoNotCare into each color space
  // indicated by the other (of acc and c).  After this, accumulation can proceed as normal, with
  // kDoNotCare (if still present) treated as any other normal ColorSpaceType.  At the end of
  // overall accumulation, we must check (elsewhere) that we're not left with only a single
  // kDoNotCare ColorSpaceType.
  auto acc_is_do_not_care_result = IsColorSpaceArrayDoNotCare(*acc);
  // maintained during accumulation, largely thanks to having checked each c previously
  ZX_DEBUG_ASSERT(acc_is_do_not_care_result.is_ok());
  bool acc_is_do_not_care = acc_is_do_not_care_result.value();
  auto c_is_do_not_care_result = IsColorSpaceArrayDoNotCare(c);
  // checked previously
  ZX_DEBUG_ASSERT(c_is_do_not_care_result.is_ok());
  bool c_is_do_not_care = c_is_do_not_care_result.value();
  if (acc_is_do_not_care && !c_is_do_not_care) {
    // Replicate acc entries to correspond to c entries
    ReplicateColorSpaceDoNotCare(c, *acc);
  } else if (!acc_is_do_not_care && c_is_do_not_care) {
    // replicate c entries to corresponding acc entries
    ReplicateColorSpaceDoNotCare(*acc, c);
  } else {
    // Either both are ColorSpaceType kDoNotCare, or neither are.
    ZX_DEBUG_ASSERT(acc_is_do_not_care == c_is_do_not_care);
  }

  // Remove any color_space in acc that's not in c.  If zero color spaces
  // remain in acc, return false.

  for (uint32_t ai = 0; ai < acc->size(); ++ai) {
    uint32_t ci;
    for (ci = 0; ci < c.size(); ++ci) {
      if (IsColorSpaceEqual((*acc)[ai], c[ci])) {
        // We found the color space in c.  Break so we can move on to
        // the next color space.
        break;
      }
    }
    if (ci == c.size()) {
      // Remove from acc because not found in c.
      //
      // Move formerly last item on top of the item being removed, if not same item.
      if (ai != acc->size() - 1) {
        (*acc)[ai] = std::move((*acc)[acc->size() - 1]);
      } else {
        // Stuff under this item would get deleted later anyway, but delete now to avoid keeping
        // cruft we don't need.
        (*acc)[ai] = fuchsia_sysmem2::ColorSpace();
      }
      // remove last item
      acc->resize(acc->size() - 1);
      // adjust ai to force current index to be processed again as it's
      // now a different item
      --ai;
    }
  }

  if (acc->empty()) {
    // It's ok for this check to be under Accumulate* because it's also
    // under CheckSanitize().  It's fine to provide a slightly more helpful
    // error message here and early out here.
    LogError(FROM_HERE, "Zero color_space overlap");
    return false;
  }

  return true;
}

bool LogicalBufferCollection::IsColorSpaceEqual(const fuchsia_sysmem2::ColorSpace& a,
                                                const fuchsia_sysmem2::ColorSpace& b) {
  return *a.type() == *b.type();
}

static fpromise::result<fuchsia_sysmem2::HeapType, zx_status_t> GetHeap(
    const fuchsia_sysmem2::BufferMemoryConstraints& constraints, Device* device) {
  if (*constraints.secure_required()) {
    // TODO(fxbug.dev/37452): Generalize this.
    //
    // checked previously
    ZX_DEBUG_ASSERT(!*constraints.secure_required() || IsSecurePermitted(constraints));
    if (IsHeapPermitted(constraints, fuchsia_sysmem2::HeapType::kAmlogicSecure)) {
      return fpromise::ok(fuchsia_sysmem2::HeapType::kAmlogicSecure);
    } else {
      ZX_DEBUG_ASSERT(IsHeapPermitted(constraints, fuchsia_sysmem2::HeapType::kAmlogicSecureVdec));
      return fpromise::ok(fuchsia_sysmem2::HeapType::kAmlogicSecureVdec);
    }
  }
  if (IsHeapPermitted(constraints, fuchsia_sysmem2::HeapType::kSystemRam)) {
    return fpromise::ok(fuchsia_sysmem2::HeapType::kSystemRam);
  }

  for (size_t i = 0; i < constraints.heap_permitted()->size(); ++i) {
    auto heap = constraints.heap_permitted()->at(i);
    const auto& heap_properties = device->GetHeapProperties(heap);
    if (heap_properties.coherency_domain_support().has_value() &&
        ((*heap_properties.coherency_domain_support()->cpu_supported() &&
          *constraints.cpu_domain_supported()) ||
         (*heap_properties.coherency_domain_support()->ram_supported() &&
          *constraints.ram_domain_supported()) ||
         (*heap_properties.coherency_domain_support()->inaccessible_supported() &&
          *constraints.inaccessible_domain_supported()))) {
      return fpromise::ok(heap);
    }
  }
  return fpromise::error(ZX_ERR_NOT_FOUND);
}

static fpromise::result<fuchsia_sysmem2::CoherencyDomain> GetCoherencyDomain(
    const fuchsia_sysmem2::BufferCollectionConstraints& constraints,
    MemoryAllocator* memory_allocator) {
  ZX_DEBUG_ASSERT(constraints.buffer_memory_constraints().has_value());

  using fuchsia_sysmem2::CoherencyDomain;
  const auto& heap_properties = memory_allocator->heap_properties();
  ZX_DEBUG_ASSERT(heap_properties.coherency_domain_support().has_value());

  // Display prefers RAM coherency domain for now.
  if (*constraints.usage()->display() != 0) {
    if (*constraints.buffer_memory_constraints()->ram_domain_supported()) {
      // Display controllers generally aren't cache coherent, so prefer
      // RAM coherency domain.
      //
      // TODO - base on the system in use.
      return fpromise::ok(fuchsia_sysmem2::CoherencyDomain::kRam);
    }
  }

  if (*heap_properties.coherency_domain_support()->cpu_supported() &&
      *constraints.buffer_memory_constraints()->cpu_domain_supported()) {
    return fpromise::ok(CoherencyDomain::kCpu);
  }

  if (*heap_properties.coherency_domain_support()->ram_supported() &&
      *constraints.buffer_memory_constraints()->ram_domain_supported()) {
    return fpromise::ok(CoherencyDomain::kRam);
  }

  if (*heap_properties.coherency_domain_support()->inaccessible_supported() &&
      *constraints.buffer_memory_constraints()->inaccessible_domain_supported()) {
    // Intentionally permit treating as Inaccessible if we reach here, even
    // if the heap permits CPU access.  Only domain in common among
    // participants is Inaccessible.
    return fpromise::ok(fuchsia_sysmem2::CoherencyDomain::kInaccessible);
  }

  return fpromise::error();
}

fpromise::result<fuchsia_sysmem2::BufferCollectionInfo, zx_status_t>
LogicalBufferCollection::GenerateUnpopulatedBufferCollectionInfo(
    const fuchsia_sysmem2::BufferCollectionConstraints& constraints) {
  TRACE_DURATION("gfx", "LogicalBufferCollection:GenerateUnpopulatedBufferCollectionInfo", "this",
                 this);

  fuchsia_sysmem2::BufferCollectionInfo result;

  uint32_t min_buffer_count = *constraints.min_buffer_count_for_camping() +
                              *constraints.min_buffer_count_for_dedicated_slack() +
                              *constraints.min_buffer_count_for_shared_slack();
  min_buffer_count = std::max(min_buffer_count, *constraints.min_buffer_count());
  uint32_t max_buffer_count = *constraints.max_buffer_count();
  if (min_buffer_count > max_buffer_count) {
    LogError(FROM_HERE,
             "aggregate min_buffer_count > aggregate max_buffer_count - "
             "min: %u max: %u",
             min_buffer_count, max_buffer_count);
    return fpromise::error(ZX_ERR_NOT_SUPPORTED);
  }
  if (min_buffer_count > fuchsia_sysmem::kMaxCountBufferCollectionInfoBuffers) {
    LogError(FROM_HERE,
             "aggregate min_buffer_count (%d) > MAX_COUNT_BUFFER_COLLECTION_INFO_BUFFERS (%d)",
             min_buffer_count, fuchsia_sysmem::kMaxCountBufferCollectionInfoBuffers);
    return fpromise::error(ZX_ERR_NOT_SUPPORTED);
  }

  result.buffers().emplace(min_buffer_count);
  result.buffers()->resize(min_buffer_count);
  ZX_DEBUG_ASSERT(result.buffers()->size() == min_buffer_count);
  ZX_DEBUG_ASSERT(result.buffers()->size() <= max_buffer_count);

  uint64_t min_size_bytes = 0;
  uint64_t max_size_bytes = std::numeric_limits<uint64_t>::max();

  result.settings().emplace();
  fuchsia_sysmem2::SingleBufferSettings& settings = *result.settings();
  settings.buffer_settings().emplace();
  fuchsia_sysmem2::BufferMemorySettings& buffer_settings = *settings.buffer_settings();

  ZX_DEBUG_ASSERT(constraints.buffer_memory_constraints().has_value());
  const fuchsia_sysmem2::BufferMemoryConstraints& buffer_constraints =
      *constraints.buffer_memory_constraints();
  buffer_settings.is_physically_contiguous() = *buffer_constraints.physically_contiguous_required();
  // checked previously
  ZX_DEBUG_ASSERT(IsSecurePermitted(buffer_constraints) || !*buffer_constraints.secure_required());
  buffer_settings.is_secure() = *buffer_constraints.secure_required();
  if (*buffer_settings.is_secure()) {
    if (*constraints.need_clear_aux_buffers_for_secure() &&
        !*constraints.allow_clear_aux_buffers_for_secure()) {
      LogError(
          FROM_HERE,
          "is_secure && need_clear_aux_buffers_for_secure && !allow_clear_aux_buffers_for_secure");
      return fpromise::error(ZX_ERR_NOT_SUPPORTED);
    }
  }

  auto result_get_heap = GetHeap(buffer_constraints, parent_device_);
  if (!result_get_heap.is_ok()) {
    LogError(FROM_HERE, "Can not find a heap permitted by buffer constraints, error %d",
             result_get_heap.error());
    return fpromise::error(result_get_heap.error());
  }
  buffer_settings.heap() = result_get_heap.value();

  // We can't fill out buffer_settings yet because that also depends on
  // ImageFormatConstraints.  We do need the min and max from here though.
  min_size_bytes = *buffer_constraints.min_size_bytes();
  max_size_bytes = *buffer_constraints.max_size_bytes();

  // Get memory allocator for settings.
  MemoryAllocator* allocator = parent_device_->GetAllocator(buffer_settings);
  if (!allocator) {
    LogError(FROM_HERE, "No memory allocator for buffer settings");
    return fpromise::error(ZX_ERR_NO_MEMORY);
  }

  auto coherency_domain_result = GetCoherencyDomain(constraints, allocator);
  if (!coherency_domain_result.is_ok()) {
    LogError(FROM_HERE, "No coherency domain found for buffer constraints");
    return fpromise::error(ZX_ERR_NOT_SUPPORTED);
  }
  buffer_settings.coherency_domain() = coherency_domain_result.value();

  // It's allowed for zero participants to have any ImageFormatConstraint(s),
  // in which case the combined constraints_ will have zero (and that's fine,
  // when allocating raw buffers that don't need any ImageFormatConstraint).
  //
  // At least for now, we pick which PixelFormat to use before determining if
  // the constraints associated with that PixelFormat imply a buffer size
  // range in min_size_bytes..max_size_bytes.
  if (!constraints.image_format_constraints()->empty()) {
    // Pick the best ImageFormatConstraints.
    uint32_t best_index = UINT32_MAX;
    bool found_unsupported_when_protected = false;
    for (uint32_t i = 0; i < constraints.image_format_constraints()->size(); ++i) {
      if (*buffer_settings.is_secure() &&
          !ImageFormatCompatibleWithProtectedMemory(
              *constraints.image_format_constraints()->at(i).pixel_format())) {
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
      return fpromise::error(ZX_ERR_NOT_SUPPORTED);
    }
    // copy / clone from constraints to settings.
    settings.image_format_constraints() = constraints.image_format_constraints()->at(best_index);
  }

  // Compute the min buffer size implied by image_format_constraints, so we ensure the buffers can
  // hold the min-size image.
  if (settings.image_format_constraints().has_value()) {
    const fuchsia_sysmem2::ImageFormatConstraints& image_format_constraints =
        *settings.image_format_constraints();
    fuchsia_sysmem2::ImageFormat min_image;
    // copy / clone
    min_image.pixel_format() = *image_format_constraints.pixel_format();
    // We use required_max_coded_width because that's the max width that the producer (or
    // initiator) wants these buffers to be able to hold.
    min_image.coded_width() =
        AlignUp(std::max(*image_format_constraints.min_coded_width(),
                         *image_format_constraints.required_max_coded_width()),
                *image_format_constraints.coded_width_divisor());
    if (*min_image.coded_width() > *image_format_constraints.max_coded_width()) {
      LogError(FROM_HERE, "coded_width_divisor caused coded_width > max_coded_width");
      return fpromise::error(ZX_ERR_NOT_SUPPORTED);
    }
    // We use required_max_coded_height because that's the max height that the producer (or
    // initiator) wants these buffers to be able to hold.
    min_image.coded_height() =
        AlignUp(std::max(*image_format_constraints.min_coded_height(),
                         *image_format_constraints.required_max_coded_height()),
                *image_format_constraints.coded_height_divisor());
    if (*min_image.coded_height() > *image_format_constraints.max_coded_height()) {
      LogError(FROM_HERE, "coded_height_divisor caused coded_height > max_coded_height");
      return fpromise::error(ZX_ERR_NOT_SUPPORTED);
    }
    min_image.bytes_per_row() = AlignUp(
        std::max(*image_format_constraints.min_bytes_per_row(),
                 ImageFormatStrideBytesPerWidthPixel(*image_format_constraints.pixel_format()) *
                     *min_image.coded_width()),
        *image_format_constraints.bytes_per_row_divisor());
    if (*min_image.bytes_per_row() > *image_format_constraints.max_bytes_per_row()) {
      LogError(FROM_HERE,
               "bytes_per_row_divisor caused bytes_per_row > "
               "max_bytes_per_row");
      return fpromise::error(ZX_ERR_NOT_SUPPORTED);
    }

    if (*min_image.coded_width() * *min_image.coded_height() >
        *image_format_constraints.max_coded_width_times_coded_height()) {
      LogError(FROM_HERE,
               "coded_width * coded_height > "
               "max_coded_width_times_coded_height");
      return fpromise::error(ZX_ERR_NOT_SUPPORTED);
    }

    // These don't matter for computing size in bytes.
    ZX_DEBUG_ASSERT(!min_image.display_width().has_value());
    ZX_DEBUG_ASSERT(!min_image.display_height().has_value());

    // Checked previously.
    ZX_DEBUG_ASSERT(image_format_constraints.color_spaces()->size() >= 1);
    // This doesn't matter for computing size in bytes, as we trust the pixel_format to fully
    // specify the image size.  But set it to the first ColorSpace anyway, just so the
    // color_space.type is a valid value.
    min_image.color_space() = image_format_constraints.color_spaces()->at(0);

    uint64_t image_min_size_bytes = ImageFormatImageSize(min_image);

    if (image_min_size_bytes > min_size_bytes) {
      if (image_min_size_bytes > max_size_bytes) {
        LogError(FROM_HERE, "image_min_size_bytes > max_size_bytes");
        return fpromise::error(ZX_ERR_NOT_SUPPORTED);
      }
      min_size_bytes = image_min_size_bytes;
      ZX_DEBUG_ASSERT(min_size_bytes <= max_size_bytes);
    }
  }

  // Currently redundant with earlier checks, but just in case...
  if (min_size_bytes == 0) {
    LogError(FROM_HERE, "min_size_bytes == 0");
    return fpromise::error(ZX_ERR_NOT_SUPPORTED);
  }
  ZX_DEBUG_ASSERT(min_size_bytes != 0);

  // For purposes of enforcing max_size_bytes, we intentionally don't care that a VMO can only be a
  // multiple of page size.

  uint64_t total_size_bytes = min_size_bytes * result.buffers()->size();
  if (total_size_bytes > kMaxTotalSizeBytesPerCollection) {
    LogError(FROM_HERE, "total_size_bytes > kMaxTotalSizeBytesPerCollection");
    return fpromise::error(ZX_ERR_NO_MEMORY);
  }

  if (min_size_bytes > kMaxSizeBytesPerBuffer) {
    LogError(FROM_HERE, "min_size_bytes > kMaxSizeBytesPerBuffer");
    return fpromise::error(ZX_ERR_NO_MEMORY);
  }
  ZX_DEBUG_ASSERT(min_size_bytes <= std::numeric_limits<uint32_t>::max());

  // Now that min_size_bytes accounts for any ImageFormatConstraints, we can just allocate
  // min_size_bytes buffers.
  //
  // If an initiator (or a participant) wants to force buffers to be larger than the size implied by
  // minimum image dimensions, the initiator can use BufferMemorySettings.min_size_bytes to force
  // allocated buffers to be large enough.
  buffer_settings.size_bytes() = safe_cast<uint32_t>(min_size_bytes);

  if (*buffer_settings.size_bytes() > parent_device_->settings().max_allocation_size) {
    // This is different than max_size_bytes.  While max_size_bytes is part of the constraints,
    // max_allocation_size isn't part of the constraints.  The latter is used for simulating OOM or
    // preventing unpredictable memory pressure caused by a fuzzer or similar source of
    // unpredictability in tests.
    LogError(FROM_HERE,
             "GenerateUnpopulatedBufferCollectionInfo() failed because size %u > "
             "max_allocation_size %ld",
             *buffer_settings.size_bytes(), parent_device_->settings().max_allocation_size);
    return fpromise::error(ZX_ERR_NO_MEMORY);
  }

  // We initially set vmo_usable_start to bit-fields indicating whether vmo and aux_vmo fields will
  // be set to valid handles later.  This is for purposes of comparison with a later
  // BufferCollectionInfo after an AttachToken().  Before sending to the client, the
  // vmo_usable_start is set to 0.  Even if later we need a non-zero vmo_usable_start to be compared
  // we are extremely unlikely to want a buffer to start at an offset that isn't divisible by 4, so
  // using the two low-order bits for this seems reasonable enough.
  for (uint32_t i = 0; i < result.buffers()->size(); ++i) {
    fuchsia_sysmem2::VmoBuffer vmo_buffer;
    vmo_buffer.vmo_usable_start() = 0ul;
    if (*buffer_settings.is_secure() && *constraints.need_clear_aux_buffers_for_secure()) {
      *vmo_buffer.vmo_usable_start() |= kNeedAuxVmoAlso;
    }
    result.buffers()->at(i) = std::move(vmo_buffer);
  }

  return fpromise::ok(std::move(result));
}

fpromise::result<fuchsia_sysmem2::BufferCollectionInfo, zx_status_t>
LogicalBufferCollection::Allocate(const fuchsia_sysmem2::BufferCollectionConstraints& constraints,
                                  fuchsia_sysmem2::BufferCollectionInfo* builder) {
  TRACE_DURATION("gfx", "LogicalBufferCollection:Allocate", "this", this);

  fuchsia_sysmem2::BufferCollectionInfo& result = *builder;

  fuchsia_sysmem2::SingleBufferSettings& settings = *result.settings();
  fuchsia_sysmem2::BufferMemorySettings& buffer_settings = *settings.buffer_settings();

  // Get memory allocator for settings.
  MemoryAllocator* allocator = parent_device_->GetAllocator(buffer_settings);
  if (!allocator) {
    LogError(FROM_HERE, "No memory allocator for buffer settings");
    return fpromise::error(ZX_ERR_NO_MEMORY);
  }

  if (settings.image_format_constraints().has_value()) {
    const fuchsia_sysmem2::ImageFormatConstraints& image_format_constraints =
        *settings.image_format_constraints();
    inspect_node_.CreateUint(
        "pixel_format",
        sysmem::fidl_underlying_cast(*image_format_constraints.pixel_format()->type()),
        &vmo_properties_);
    if (image_format_constraints.pixel_format()->format_modifier_value().has_value()) {
      inspect_node_.CreateUint("pixel_format_modifier",
                               *image_format_constraints.pixel_format()->format_modifier_value(),
                               &vmo_properties_);
    }
    if (*image_format_constraints.min_coded_width() > 0) {
      inspect_node_.CreateUint("min_coded_width", *image_format_constraints.min_coded_width(),
                               &vmo_properties_);
    }
    if (*image_format_constraints.min_coded_height() > 0) {
      inspect_node_.CreateUint("min_coded_height", *image_format_constraints.min_coded_height(),
                               &vmo_properties_);
    }
    if (*image_format_constraints.required_max_coded_width() > 0) {
      inspect_node_.CreateUint("required_max_coded_width",
                               *image_format_constraints.required_max_coded_width(),
                               &vmo_properties_);
    }
    if (*image_format_constraints.required_max_coded_height() > 0) {
      inspect_node_.CreateUint("required_max_coded_height",
                               *image_format_constraints.required_max_coded_height(),
                               &vmo_properties_);
    }
  }

  inspect_node_.CreateUint("allocator_id", allocator->id(), &vmo_properties_);
  inspect_node_.CreateUint("size_bytes", *buffer_settings.size_bytes(), &vmo_properties_);
  inspect_node_.CreateUint("heap", sysmem::fidl_underlying_cast(*buffer_settings.heap()),
                           &vmo_properties_);
  inspect_node_.CreateUint("allocation_timestamp_ns", zx::clock::get_monotonic().get(),
                           &vmo_properties_);

  // Get memory allocator for aux buffers, if needed.
  MemoryAllocator* maybe_aux_allocator = nullptr;
  std::optional<fuchsia_sysmem2::SingleBufferSettings> maybe_aux_settings;
  ZX_DEBUG_ASSERT(
      !!(*result.buffers()->at(0).vmo_usable_start() & kNeedAuxVmoAlso) ==
      (*buffer_settings.is_secure() && *constraints.need_clear_aux_buffers_for_secure()));
  if (*result.buffers()->at(0).vmo_usable_start() & kNeedAuxVmoAlso) {
    maybe_aux_settings.emplace();
    maybe_aux_settings->buffer_settings().emplace();
    auto& aux_buffer_settings = *maybe_aux_settings->buffer_settings();
    aux_buffer_settings.size_bytes() = *buffer_settings.size_bytes();
    aux_buffer_settings.is_physically_contiguous() = false;
    aux_buffer_settings.is_secure() = false;
    aux_buffer_settings.coherency_domain() = fuchsia_sysmem2::CoherencyDomain::kCpu;
    aux_buffer_settings.heap() = fuchsia_sysmem2::HeapType::kSystemRam;
    maybe_aux_allocator = parent_device_->GetAllocator(aux_buffer_settings);
    ZX_DEBUG_ASSERT(maybe_aux_allocator);
  }

  ZX_DEBUG_ASSERT(*buffer_settings.size_bytes() <= parent_device_->settings().max_allocation_size);

  for (uint32_t i = 0; i < result.buffers()->size(); ++i) {
    auto allocate_result = AllocateVmo(allocator, settings, i);
    if (!allocate_result.is_ok()) {
      LogError(FROM_HERE, "AllocateVmo() failed");
      return fpromise::error(ZX_ERR_NO_MEMORY);
    }
    zx::vmo vmo = allocate_result.take_value();
    auto& vmo_buffer = result.buffers()->at(i);
    vmo_buffer.vmo() = std::move(vmo);
    if (maybe_aux_allocator) {
      ZX_DEBUG_ASSERT(maybe_aux_settings.has_value());
      auto aux_allocate_result = AllocateVmo(maybe_aux_allocator, maybe_aux_settings.value(), i);
      if (!aux_allocate_result.is_ok()) {
        LogError(FROM_HERE, "AllocateVmo() failed (aux)");
        return fpromise::error(ZX_ERR_NO_MEMORY);
      }
      zx::vmo aux_vmo = aux_allocate_result.take_value();
      vmo_buffer.aux_vmo() = std::move(aux_vmo);
    }
    ZX_DEBUG_ASSERT(vmo_buffer.vmo_usable_start().has_value());
    // In case kNeedAuxVmoAlso was set.
    vmo_buffer.vmo_usable_start() = 0;
  }
  vmo_count_property_ = inspect_node_.CreateUint("vmo_count", result.buffers()->size());
  // Make sure we have sufficient barrier after allocating/clearing/flushing any VMO newly allocated
  // by allocator above.
  BarrierAfterFlush();

  // Register failure handler with memory allocator.
  allocator->AddDestroyCallback(reinterpret_cast<intptr_t>(this), [this]() {
    LogAndFailRootNode(FROM_HERE, ZX_ERR_BAD_STATE,
                       "LogicalBufferCollection memory allocator gone - now auto-failing self.");
  });
  memory_allocator_ = allocator;

  return fpromise::ok(std::move(result));
}

fpromise::result<zx::vmo> LogicalBufferCollection::AllocateVmo(
    MemoryAllocator* allocator, const fuchsia_sysmem2::SingleBufferSettings& settings,
    uint32_t index) {
  TRACE_DURATION("gfx", "LogicalBufferCollection::AllocateVmo", "size_bytes",
                 *settings.buffer_settings()->size_bytes());
  zx::vmo child_vmo;
  // Physical VMOs only support slices where the size (and offset) are page_size aligned,
  // so we should also round up when allocating.
  size_t rounded_size_bytes =
      fbl::round_up(*settings.buffer_settings()->size_bytes(), zx_system_get_page_size());
  if (rounded_size_bytes < *settings.buffer_settings()->size_bytes()) {
    LogError(FROM_HERE, "size_bytes overflows when rounding to multiple of page_size");
    return fpromise::error();
  }

  // raw_vmo may itself be a child VMO of an allocator's overall contig VMO,
  // but that's an internal detail of the allocator.  The ZERO_CHILDREN signal
  // will only be set when all direct _and indirect_ child VMOs are fully
  // gone (not just handles closed, but the kernel object is deleted, which
  // avoids races with handle close, and means there also aren't any
  // mappings left).
  zx::vmo raw_parent_vmo;
  std::optional<std::string> name;
  if (name_.has_value()) {
    name = fbl::StringPrintf("%s:%d", name_->name.c_str(), index).c_str();
  }
  zx_status_t status = allocator->Allocate(rounded_size_bytes, name, &raw_parent_vmo);
  if (status != ZX_OK) {
    LogError(FROM_HERE,
             "allocator.Allocate failed - size_bytes: %zu "
             "status: %d",
             rounded_size_bytes, status);
    return fpromise::error();
  }

  zx_info_vmo_t info;
  status = raw_parent_vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    LogError(FROM_HERE, "raw_parent_vmo.get_info(ZX_INFO_VMO) failed - status %d", status);
    return fpromise::error();
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
  // deallocations to be done before failing a new allocation.
  //
  // TODO(fxbug.dev/34590): Zero secure/protected VMOs.
  const auto& heap_properties = allocator->heap_properties();
  ZX_DEBUG_ASSERT(heap_properties.coherency_domain_support().has_value());
  ZX_DEBUG_ASSERT(heap_properties.need_clear().has_value());
  if (*heap_properties.need_clear() && !allocator->is_already_cleared_on_allocate()) {
    uint64_t offset = 0;
    while (offset < info.size_bytes) {
      uint64_t bytes_to_write = std::min(sizeof(kZeroes), info.size_bytes - offset);
      // TODO(fxbug.dev/59796): Use ZX_VMO_OP_ZERO instead.
      status = raw_parent_vmo.write(kZeroes, offset, bytes_to_write);
      if (status != ZX_OK) {
        LogError(FROM_HERE, "raw_parent_vmo.write() failed - status: %d", status);
        return fpromise::error();
      }
      offset += bytes_to_write;
    }
  }
  if (*heap_properties.need_clear() ||
      (heap_properties.need_flush().has_value() && *heap_properties.need_flush())) {
    // Flush out the zeroes written above, or the zeroes that are already in the pages (but not
    // flushed yet) thanks to zx_vmo_create_contiguous(), or zeroes that are already in the pages
    // (but not necessarily flushed yet) thanks to whatever other allocator strategy.  The barrier
    // after this flush is in the caller after all the VMOs are allocated.
    status = raw_parent_vmo.op_range(ZX_VMO_OP_CACHE_CLEAN, 0, info.size_bytes, nullptr, 0);
    if (status != ZX_OK) {
      LogError(FROM_HERE, "raw_parent_vmo.op_range(ZX_VMO_OP_CACHE_CLEAN) failed - status: %d",
               status);
      return fpromise::error();
    }
    // We don't need a BarrierAfterFlush() here because Zircon takes care of that in the
    // zx_vmo_op_range(ZX_VMO_OP_CACHE_CLEAN).
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
        SweepLifetimeTracking();
        // ~node_handle may delete "this".
      }));

  zx::vmo cooked_parent_vmo;
  status = tracked_parent_vmo->vmo().duplicate(kSysmemVmoRights, &cooked_parent_vmo);
  if (status != ZX_OK) {
    LogError(FROM_HERE, "zx::object::duplicate() failed - status: %d", status);
    return fpromise::error();
  }

  zx::vmo local_child_vmo;
  status =
      cooked_parent_vmo.create_child(ZX_VMO_CHILD_SLICE, 0, rounded_size_bytes, &local_child_vmo);
  if (status != ZX_OK) {
    LogError(FROM_HERE, "zx::vmo::create_child() failed - status: %d", status);
    return fpromise::error();
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
    return fpromise::error();
  }
  zx_handle_t raw_parent_vmo_handle = tracked_parent_vmo->vmo().get();
  TrackedParentVmo& parent_vmo_ref = *tracked_parent_vmo;
  auto emplace_result = parent_vmos_.emplace(raw_parent_vmo_handle, std::move(tracked_parent_vmo));
  ZX_DEBUG_ASSERT(emplace_result.second);

  // Now inform the allocator about the child VMO before we return it.
  //
  // We copy / clone settings to buffer_settings parameter.
  status = allocator->SetupChildVmo(parent_vmo_ref.vmo(), local_child_vmo, settings);
  if (status != ZX_OK) {
    LogError(FROM_HERE, "allocator.SetupChildVmo() failed - status: %d", status);
    // In this path, the ~local_child_vmo will async trigger parent_vmo_ref::OnZeroChildren()
    // which will call allocator.Delete() via above do_delete lambda passed to
    // ParentVmo::ParentVmo().
    return fpromise::error();
  }
  if (name.has_value()) {
    local_child_vmo.set_property(ZX_PROP_NAME, name->c_str(), name->size());
  }

  // ~cooked_parent_vmo is fine, since local_child_vmo counts as a child of raw_parent_vmo for
  // ZX_VMO_ZERO_CHILDREN purposes.
  return fpromise::ok(std::move(local_child_vmo));
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

  std::string name = name_.has_value() ? name_->name : "Unknown";

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

static int32_t clamp_difference(uint64_t a, uint64_t b) {
  if (a > b) {
    return 1;
  }
  if (a < b) {
    return -1;
  }
  return 0;
}

// 1 means a > b, 0 means ==, -1 means a < b.
//
// TODO(dustingreen): Pay attention to constraints_->usage, by checking any
// overrides that prefer particular PixelFormat based on a usage / usage
// combination.
int32_t LogicalBufferCollection::CompareImageFormatConstraintsTieBreaker(
    const fuchsia_sysmem2::ImageFormatConstraints& a,
    const fuchsia_sysmem2::ImageFormatConstraints& b) {
  // If there's not any cost difference, fall back to choosing the
  // pixel_format that has the larger type enum value as a tie-breaker.

  int32_t result = clamp_difference(sysmem::fidl_underlying_cast(*a.pixel_format()->type()),
                                    sysmem::fidl_underlying_cast(*b.pixel_format()->type()));

  if (result != 0) {
    return result;
  }

  result = clamp_difference(a.pixel_format()->format_modifier_value().has_value(),
                            b.pixel_format()->format_modifier_value().has_value());

  if (result != 0) {
    return result;
  }

  if (a.pixel_format()->format_modifier_value().has_value() &&
      b.pixel_format()->format_modifier_value().has_value()) {
    result = clamp_difference(*a.pixel_format()->format_modifier_value(),
                              *b.pixel_format()->format_modifier_value());
  }

  return result;
}

int32_t LogicalBufferCollection::CompareImageFormatConstraintsByIndex(
    const fuchsia_sysmem2::BufferCollectionConstraints& constraints, uint32_t index_a,
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
      CompareImageFormatConstraintsTieBreaker(constraints.image_format_constraints()->at(index_a),
                                              constraints.image_format_constraints()->at(index_b));
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
  buffer_collection_->LogInfo(FROM_HERE, "LogicalBufferCollection::TrackedParentVmo::StartWait()");
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
  buffer_collection_->LogInfo(FROM_HERE,
                              "LogicalBufferCollection::TrackedParentVmo::OnZeroChildren()");
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
    if (node.is_connected_type()) {
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

void LogicalBufferCollection::TrackNodeProperties(NodeProperties* node_properties) {
  node_properties_by_node_ref_keep_koid_.insert(
      std::make_pair(node_properties->node_ref_koid(), node_properties));
}

void LogicalBufferCollection::UntrackNodeProperties(NodeProperties* node_properties) {
  node_properties_by_node_ref_keep_koid_.erase(node_properties->node_ref_koid());
}

std::optional<NodeProperties*> LogicalBufferCollection::FindNodePropertiesByNodeRefKoid(
    zx_koid_t node_ref_keep_koid) {
  auto iter = node_properties_by_node_ref_keep_koid_.find(node_ref_keep_koid);
  if (iter == node_properties_by_node_ref_keep_koid_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

#define LOG_UINT32_FIELD(location, prefix, field_name)                                        \
  do {                                                                                        \
    if (!(prefix).field_name().has_value()) {                                                 \
      LogClientInfo(location, node_properties, "!%s.%s().has_value()", #prefix, #field_name); \
    } else {                                                                                  \
      LogClientInfo(location, node_properties, "*%s.%s(): %u", #prefix, #field_name,          \
                    sysmem::fidl_underlying_cast(*(prefix).field_name()));                    \
    }                                                                                         \
  } while (0)

#define LOG_UINT64_FIELD(location, prefix, field_name)                                        \
  do {                                                                                        \
    if (!(prefix).field_name().has_value()) {                                                 \
      LogClientInfo(location, node_properties, "!%s.%s().has_value()", #prefix, #field_name); \
    } else {                                                                                  \
      LogClientInfo(location, node_properties, "*%s.%s(): %" PRIx64, #prefix, #field_name,    \
                    sysmem::fidl_underlying_cast(*(prefix).field_name()));                    \
    }                                                                                         \
  } while (0)

#define LOG_BOOL_FIELD(location, prefix, field_name)                                          \
  do {                                                                                        \
    if (!(prefix).field_name().has_value()) {                                                 \
      LogClientInfo(location, node_properties, "!%s.%s().has_value()", #prefix, #field_name); \
    } else {                                                                                  \
      LogClientInfo(location, node_properties, "*%s.%s(): %u", #prefix, #field_name,          \
                    *(prefix).field_name());                                                  \
    }                                                                                         \
  } while (0)

void LogicalBufferCollection::LogConstraints(
    Location location, NodeProperties* node_properties,
    const fuchsia_sysmem2::BufferCollectionConstraints& constraints) const {
  if (!node_properties) {
    LogInfo(FROM_HERE, "Constraints (aggregated / previously chosen):");
  } else {
    LogInfo(FROM_HERE, "Constraints - NodeProperties: %p", node_properties);
  }

  const fuchsia_sysmem2::BufferCollectionConstraints& c = constraints;

  LOG_UINT32_FIELD(FROM_HERE, c, min_buffer_count);
  LOG_UINT32_FIELD(FROM_HERE, c, min_buffer_count_for_camping);
  LOG_UINT32_FIELD(FROM_HERE, c, min_buffer_count_for_dedicated_slack);
  LOG_UINT32_FIELD(FROM_HERE, c, min_buffer_count_for_shared_slack);

  if (!c.buffer_memory_constraints().has_value()) {
    LogInfo(FROM_HERE, "!c.has_buffer_memory_constraints()");
  } else {
    const fuchsia_sysmem2::BufferMemoryConstraints& bmc = *c.buffer_memory_constraints();
    LOG_UINT32_FIELD(FROM_HERE, bmc, min_size_bytes);
    LOG_UINT32_FIELD(FROM_HERE, bmc, max_size_bytes);
    LOG_BOOL_FIELD(FROM_HERE, bmc, physically_contiguous_required);
    LOG_BOOL_FIELD(FROM_HERE, bmc, secure_required);
    LOG_BOOL_FIELD(FROM_HERE, bmc, cpu_domain_supported);
    LOG_BOOL_FIELD(FROM_HERE, bmc, ram_domain_supported);
    LOG_BOOL_FIELD(FROM_HERE, bmc, inaccessible_domain_supported);
  }

  uint32_t image_format_constraints_count =
      c.image_format_constraints().has_value()
          ? safe_cast<uint32_t>(c.image_format_constraints()->size())
          : 0;
  LogInfo(FROM_HERE, "image_format_constraints.count() %u", image_format_constraints_count);
  for (uint32_t i = 0; i < image_format_constraints_count; ++i) {
    LogInfo(FROM_HERE, "image_format_constraints[%u] (ifc):", i);
    const fuchsia_sysmem2::ImageFormatConstraints& ifc = c.image_format_constraints()->at(i);
    if (!ifc.pixel_format().has_value()) {
      LogInfo(FROM_HERE, "!ifc.has_pixel_format()");
    } else {
      LOG_UINT32_FIELD(FROM_HERE, *ifc.pixel_format(), type);
      LOG_UINT64_FIELD(FROM_HERE, *ifc.pixel_format(), format_modifier_value);
    }
    LOG_UINT32_FIELD(FROM_HERE, ifc, min_coded_width);
    LOG_UINT32_FIELD(FROM_HERE, ifc, max_coded_width);
    LOG_UINT32_FIELD(FROM_HERE, ifc, min_coded_height);
    LOG_UINT32_FIELD(FROM_HERE, ifc, max_coded_height);
    LOG_UINT32_FIELD(FROM_HERE, ifc, min_bytes_per_row);
    LOG_UINT32_FIELD(FROM_HERE, ifc, max_bytes_per_row);
    LOG_UINT32_FIELD(FROM_HERE, ifc, max_coded_width_times_coded_height);
    LOG_UINT32_FIELD(FROM_HERE, ifc, coded_width_divisor);
    LOG_UINT32_FIELD(FROM_HERE, ifc, coded_height_divisor);
    LOG_UINT32_FIELD(FROM_HERE, ifc, bytes_per_row_divisor);
    LOG_UINT32_FIELD(FROM_HERE, ifc, start_offset_divisor);
    LOG_UINT32_FIELD(FROM_HERE, ifc, display_width_divisor);
    LOG_UINT32_FIELD(FROM_HERE, ifc, display_height_divisor);
    LOG_UINT32_FIELD(FROM_HERE, ifc, required_min_coded_width);
    LOG_UINT32_FIELD(FROM_HERE, ifc, required_max_coded_width);
    LOG_UINT32_FIELD(FROM_HERE, ifc, required_min_coded_height);
    LOG_UINT32_FIELD(FROM_HERE, ifc, required_max_coded_height);
    LOG_UINT32_FIELD(FROM_HERE, ifc, required_min_bytes_per_row);
    LOG_UINT32_FIELD(FROM_HERE, ifc, required_max_bytes_per_row);
  }
}

void LogicalBufferCollection::LogPrunedSubTree(NodeProperties* subtree) {
  ignore_result(subtree->DepthFirstPreOrder(
      PrunedSubtreeFilter(*subtree, [this, subtree](const NodeProperties& node_properties) {
        uint32_t depth = 0;
        for (auto iter = &node_properties; iter != subtree; iter = iter->parent()) {
          ++depth;
        }
        std::string indent;
        for (uint32_t i = 0; i < depth; ++i) {
          indent += "  ";
        }
        LogInfo(FROM_HERE, "%sNodeProperties: %p (%s) has_constraints: %u ready: %u name: %s",
                indent.c_str(), &node_properties, node_properties.node_type_name(),
                node_properties.has_constraints(), node_properties.node()->ReadyForAllocation(),
                node_properties.client_debug_info().name.c_str());
        // No need to keep the nodes in a list; we've alread done what we need to do during the
        // visit.
        return false;
      })));
}

void LogicalBufferCollection::LogNodeConstraints(std::vector<NodeProperties*> nodes) {
  for (auto node : nodes) {
    LogInfo(FROM_HERE, "Constraints for NodeProperties: %p (%s)", node, node->node_type_name());
    if (!node->buffer_collection_constraints()) {
      LogInfo(FROM_HERE, "No constraints in node: %p (%s)", node, node->node_type_name());
    } else {
      LogConstraints(FROM_HERE, node, *node->buffer_collection_constraints());
    }
  }
}

}  // namespace sysmem_driver
