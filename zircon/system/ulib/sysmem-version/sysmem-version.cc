// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/sysmem-version/sysmem-version.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/fidl-async-2/fidl_struct.h>
#include <lib/sysmem-make-tracking/make_tracking.h>
#include <zircon/assert.h>

#include <map>
#include <set>

#include "log.h"

namespace sysmem {
namespace {

// Can be replaced with std::remove_cvref<> when C++20.
template <typename T>
struct RemoveCVRef
    : ::sysmem::internal::TypeIdentity<std::remove_cv_t<std::remove_reference_t<T>>> {};
template <typename T>
using RemoveCVRef_t = typename RemoveCVRef<T>::type;

template <typename T, typename Enable = void>
struct IsFidlScalar : std::false_type {};
template <typename T>
struct IsFidlScalar<
    T, typename std::enable_if<fidl::IsFidlType<T>::value &&
                               (std::is_arithmetic<T>::value || std::is_enum<T>::value)>::type>
    : std::true_type {};

template <typename V2Type, typename V1Type, typename Enable = void>
struct IsCompatibleFidlScalarTypes : std::false_type {};
template <typename V2Type, typename V1Type>
struct IsCompatibleFidlScalarTypes<
    V2Type, V1Type,
    typename std::enable_if<
        // must be able to write to v2
        !std::is_const<typename std::remove_reference<V2Type>::type>::value &&
        IsFidlScalar<typename RemoveCVRef<V2Type>::type>::value &&
        IsFidlScalar<typename RemoveCVRef<V1Type>::type>::value &&
        std::is_same<typename ::sysmem::internal::UnderlyingTypeOrType<
                         typename RemoveCVRef<V2Type>::type>::type,
                     typename ::sysmem::internal::UnderlyingTypeOrType<
                         typename RemoveCVRef<V1Type>::type>::type>::value>::type>
    : std::true_type {};
template <typename V2, typename V1>
inline constexpr bool IsCompatibleFidlScalarTypes_v = IsCompatibleFidlScalarTypes<V2, V1>::value;

// The C++ style guide discourages macros, but does not prohibit them.  To operate on a bunch of
// separate fields with different names, it's a choice among tons of error-prone repetetive
// verbosity, macros, or more abstraction than I think anyone would want.  Macros are the least-bad
// option (among those options considred so far).  Feel free to propose another option.

// This macro is needed to cut down on the noise from the exact same error check occurring every
// place we might early return a failure.
#define OK_OR_RET_ERROR(foo)  \
  do {                        \
    if (!(foo).is_ok()) {     \
      LOG(ERROR, "!is_ok()"); \
      return fit::error();    \
    }                         \
  } while (false)

// This macro is needed to ensure that we don't cross-wire fields as we're converting from V1 to V2
// and to cut down on the noise from the exact same code structure for most fields.  Also, this way
// we can include a static check that the type of the v2 field exactly matches the type of the v1
// field, which doesn't generate any actual code, yet needs to be repeated for each field being
// converted.
//
// This handles scalar fields, including enum fields.  It doesn't handle vector fields, tensor
// fields, struct fields, or table fields.
//
// All bool fields are set regardless of false or true.  Other scalar fields are only set if not
// equal to zero.
#define PROCESS_SCALAR_FIELD_V1(field_name)                                                   \
  do {                                                                                        \
    using V2FieldType = std::remove_reference<decltype(v2b.build().field_name())>::type;      \
    /* double parens are significant here */                                                  \
    using V1FieldType = std::remove_reference<decltype((v1.field_name))>::type;               \
    static_assert(IsCompatibleFidlScalarTypes_v<V2FieldType, V1FieldType>);                   \
    if (std::is_same<bool, RemoveCVRef<V1FieldType>::type>::value ||                          \
        static_cast<bool>(v1.field_name)) {                                                   \
      v2b.set_##field_name(MakeTracking(allocator, static_cast<V2FieldType>(v1.field_name))); \
    }                                                                                         \
  } while (false)

#define PROCESS_SCALAR_FIELD_V2(field_name)                                      \
  do {                                                                           \
    using V1FieldType = decltype(v1.field_name);                                 \
    using V2FieldType = std::remove_reference<decltype(v2.field_name())>::type;  \
    static_assert(IsCompatibleFidlScalarTypes<V1FieldType, V2FieldType>::value); \
    if (v2.has_##field_name()) {                                                 \
      v1.field_name = static_cast<V1FieldType>(v2.field_name());                 \
    } else {                                                                     \
      v1.field_name = static_cast<V1FieldType>(0);                               \
    }                                                                            \
  } while (false)

#define ASSIGN_SCALAR(dst, src)                                     \
  do {                                                              \
    using DstType = decltype((dst));                                \
    using SrcType = decltype((src));                                \
    static_assert(IsCompatibleFidlScalarTypes_v<DstType, SrcType>); \
    using DstNoRef = std::remove_reference<DstType>::type;          \
    (dst) = static_cast<DstNoRef>(src);                             \
  } while (false)

template <size_t N>
fit::result<fidl::VectorView<llcpp::fuchsia::sysmem2::HeapType>> V2CopyFromV1HeapPermittedArray(
    fidl::Allocator* allocator, const fidl::Array<llcpp::fuchsia::sysmem::HeapType, N>& v1a,
    const uint32_t v1_count) {
  ZX_DEBUG_ASSERT(allocator);
  ZX_DEBUG_ASSERT(v1_count);
  if (v1_count > v1a.size()) {
    LOG(ERROR, "v1_count > v1a.size() - v1_count: %u v1a.size(): %zu", v1_count, v1a.size());
    return fit::error();
  }
  auto v2a = allocator->make_vec<llcpp::fuchsia::sysmem2::HeapType>(v1_count);
  for (uint32_t i = 0; i < v1_count; i++) {
    ASSIGN_SCALAR(v2a[i], v1a[i]);
  }
  return fit::ok(std::move(v2a));
}

template <size_t N>
fit::result<fidl::VectorView<llcpp::fuchsia::sysmem2::ColorSpace>> V2CopyFromV1ColorSpaceArray(
    fidl::Allocator* allocator, const fidl::Array<llcpp::fuchsia::sysmem::ColorSpace, N>& v1a,
    uint32_t v1_count) {
  ZX_DEBUG_ASSERT(allocator);
  ZX_DEBUG_ASSERT(v1_count);
  if (v1_count > v1a.size()) {
    LOG(ERROR, "v1_count > v1a.size() - v1_count: %u v1a.size(): %zu", v1_count, v1a.size());
    return fit::error();
  }
  auto v2a = allocator->make_vec<llcpp::fuchsia::sysmem2::ColorSpace>(v1_count);
  for (uint32_t i = 0; i < v1_count; i++) {
    v2a[i] = V2CopyFromV1ColorSpace(allocator, v1a[i]).build();
  }
  return fit::ok(std::move(v2a));
}

template <size_t N>
fit::result<fidl::VectorView<llcpp::fuchsia::sysmem2::ImageFormatConstraints>>
V2CopyFromV1ImageFormatConstraintsArray(
    fidl::Allocator* allocator,
    const fidl::Array<llcpp::fuchsia::sysmem::ImageFormatConstraints, N>& v1a,
    const uint32_t v1_count) {
  ZX_DEBUG_ASSERT(allocator);
  ZX_DEBUG_ASSERT(v1_count);
  if (v1_count > v1a.size()) {
    LOG(ERROR, "v1_count > v1a.size() - v1_count: %u v1a.size(): %zu", v1_count, v1a.size());
    return fit::error();
  }
  auto v2a = allocator->make_vec<llcpp::fuchsia::sysmem2::ImageFormatConstraints>(v1_count);
  for (uint32_t i = 0; i < v1_count; i++) {
    auto result = V2CopyFromV1ImageFormatConstraints(allocator, v1a[i]);
    OK_OR_RET_ERROR(result);
    v2a[i] = result.take_value().build();
  }
  return fit::ok(std::move(v2a));
}

fit::result<> V2CopyFromV1BufferCollectionConstraintsMain(
    fidl::Allocator* allocator,
    llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder* v2b_param,
    const llcpp::fuchsia::sysmem::BufferCollectionConstraints& v1) {
  ZX_DEBUG_ASSERT(allocator);
  ZX_DEBUG_ASSERT(v2b_param);
  llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder& v2b = *v2b_param;

  // This sets usage regardless of whether the client set any usage bits within uage.  That's
  // checked later (regardless of v1 or v2 client).  If a v1 client said !has_constraints, we
  // won't call the current method and usage field will remain un-set so that
  // Constraints2.IsEmpty() overall.
  {
    auto result = V2CopyFromV1BufferUsage(allocator, v1.usage);
    OK_OR_RET_ERROR(result);
    v2b.set_usage(sysmem::MakeTracking(allocator, result.take_value().build()));
  }

  PROCESS_SCALAR_FIELD_V1(min_buffer_count_for_camping);
  PROCESS_SCALAR_FIELD_V1(min_buffer_count_for_dedicated_slack);
  PROCESS_SCALAR_FIELD_V1(min_buffer_count_for_shared_slack);
  PROCESS_SCALAR_FIELD_V1(min_buffer_count);
  PROCESS_SCALAR_FIELD_V1(max_buffer_count);
  if (v1.has_buffer_memory_constraints) {
    auto result = V2CopyFromV1BufferMemoryConstraints(allocator, v1.buffer_memory_constraints);
    OK_OR_RET_ERROR(result);
    v2b.set_buffer_memory_constraints(sysmem::MakeTracking(allocator, result.take_value().build()));
  }
  if (v1.image_format_constraints_count) {
    auto result = V2CopyFromV1ImageFormatConstraintsArray(allocator, v1.image_format_constraints,
                                                          v1.image_format_constraints_count);
    OK_OR_RET_ERROR(result);
    v2b.set_image_format_constraints(sysmem::MakeTracking(allocator, result.take_value()));
  }
  return fit::ok();
}

fit::result<> V2CopyFromV1BufferCollectionConstraintsAuxBuffers(
    fidl::Allocator* allocator,
    llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder* v2b_param,
    const llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers& v1) {
  ZX_DEBUG_ASSERT(allocator);
  ZX_DEBUG_ASSERT(v2b_param);
  llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder& v2b = *v2b_param;
  PROCESS_SCALAR_FIELD_V1(need_clear_aux_buffers_for_secure);
  PROCESS_SCALAR_FIELD_V1(allow_clear_aux_buffers_for_secure);
  return fit::ok();
}

}  // namespace

llcpp::fuchsia::sysmem2::PixelFormat::Builder V2CopyFromV1PixelFormat(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem::PixelFormat& v1) {
  ZX_DEBUG_ASSERT(allocator);
  auto v2b = allocator->make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>();
  PROCESS_SCALAR_FIELD_V1(type);
  if (v1.has_format_modifier) {
    v2b.set_format_modifier_value(sysmem::MakeTracking(allocator, v1.format_modifier.value));
  }
  return v2b;
}

llcpp::fuchsia::sysmem2::PixelFormat::Builder V2CopyFromV1PixelFormat(
    fidl::Allocator* allocator, const fuchsia_sysmem_PixelFormat& v1) {
  ZX_DEBUG_ASSERT(allocator);
  using CStruct = FidlStruct<fuchsia_sysmem_PixelFormat, llcpp::fuchsia::sysmem::PixelFormat>;
  return V2CopyFromV1PixelFormat(allocator, *CStruct::BorrowAsLlcpp(&v1));
}

llcpp::fuchsia::sysmem2::ColorSpace::Builder V2CopyFromV1ColorSpace(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem::ColorSpace& v1) {
  ZX_DEBUG_ASSERT(allocator);
  auto v2b = allocator->make_table_builder<llcpp::fuchsia::sysmem2::ColorSpace>();
  PROCESS_SCALAR_FIELD_V1(type);
  return v2b;
}

llcpp::fuchsia::sysmem2::ColorSpace::Builder V2CopyFromV1ColorSpace(
    fidl::Allocator* allocator, const fuchsia_sysmem_ColorSpace& v1) {
  using CStruct = FidlStruct<fuchsia_sysmem_ColorSpace, llcpp::fuchsia::sysmem::ColorSpace>;
  return V2CopyFromV1ColorSpace(allocator, *CStruct::BorrowAsLlcpp(&v1));
}

fit::result<llcpp::fuchsia::sysmem2::ImageFormatConstraints::Builder>
V2CopyFromV1ImageFormatConstraints(fidl::Allocator* allocator,
                                   const llcpp::fuchsia::sysmem::ImageFormatConstraints& v1) {
  ZX_DEBUG_ASSERT(allocator);
  auto v2b = allocator->make_table_builder<llcpp::fuchsia::sysmem2::ImageFormatConstraints>();
  v2b.set_pixel_format(
      MakeTracking(allocator, V2CopyFromV1PixelFormat(allocator, v1.pixel_format).build()));
  if (v1.color_spaces_count) {
    auto result = V2CopyFromV1ColorSpaceArray(allocator, v1.color_space, v1.color_spaces_count);
    OK_OR_RET_ERROR(result);
    v2b.set_color_spaces(MakeTracking(allocator, result.take_value()));
  }
  PROCESS_SCALAR_FIELD_V1(min_coded_width);
  PROCESS_SCALAR_FIELD_V1(max_coded_width);
  PROCESS_SCALAR_FIELD_V1(min_coded_height);
  PROCESS_SCALAR_FIELD_V1(max_coded_height);
  PROCESS_SCALAR_FIELD_V1(min_bytes_per_row);
  PROCESS_SCALAR_FIELD_V1(max_bytes_per_row);
  PROCESS_SCALAR_FIELD_V1(max_coded_width_times_coded_height);
  if (v1.layers) {
    if (v1.layers > 1) {
      LOG(ERROR, "v1.layers > 1");
      return fit::error();
    }
    // v2 ImageFormatConstraints doesn't have layers field (at least not yet), on purpose.  If it
    // ever gains a layers field, most likely we won't translate the v1 layers field to any v2
    // layers field.
  }
  PROCESS_SCALAR_FIELD_V1(coded_width_divisor);
  PROCESS_SCALAR_FIELD_V1(coded_height_divisor);
  PROCESS_SCALAR_FIELD_V1(bytes_per_row_divisor);
  PROCESS_SCALAR_FIELD_V1(start_offset_divisor);
  PROCESS_SCALAR_FIELD_V1(display_width_divisor);
  PROCESS_SCALAR_FIELD_V1(display_height_divisor);
  PROCESS_SCALAR_FIELD_V1(required_min_coded_width);
  PROCESS_SCALAR_FIELD_V1(required_max_coded_width);
  PROCESS_SCALAR_FIELD_V1(required_min_coded_height);
  PROCESS_SCALAR_FIELD_V1(required_max_coded_height);
  PROCESS_SCALAR_FIELD_V1(required_min_bytes_per_row);
  PROCESS_SCALAR_FIELD_V1(required_max_bytes_per_row);
  return fit::ok(std::move(v2b));
}

fit::result<llcpp::fuchsia::sysmem2::ImageFormatConstraints::Builder>
V2CopyFromV1ImageFormatConstraints(fidl::Allocator* allocator,
                                   const fuchsia_sysmem_ImageFormatConstraints& v1) {
  using CStruct = FidlStruct<fuchsia_sysmem_ImageFormatConstraints,
                             llcpp::fuchsia::sysmem::ImageFormatConstraints>;
  return V2CopyFromV1ImageFormatConstraints(allocator, *CStruct::BorrowAsLlcpp(&v1));
}

fit::result<llcpp::fuchsia::sysmem2::BufferUsage::Builder> V2CopyFromV1BufferUsage(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem::BufferUsage& v1) {
  ZX_DEBUG_ASSERT(allocator);
  auto v2b = allocator->make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>();
  using foo = std::remove_reference<decltype((v1.none))>::type;
  static_assert(std::is_const<foo>::value);
  PROCESS_SCALAR_FIELD_V1(none);
  PROCESS_SCALAR_FIELD_V1(cpu);
  PROCESS_SCALAR_FIELD_V1(vulkan);
  PROCESS_SCALAR_FIELD_V1(display);
  PROCESS_SCALAR_FIELD_V1(video);
  return fit::ok(std::move(v2b));
}

fit::result<llcpp::fuchsia::sysmem2::BufferUsage::Builder> V2CopyFromV1BufferUsage(
    fidl::Allocator* allocator, const fuchsia_sysmem_BufferUsage& v1) {
  using CStruct = FidlStruct<fuchsia_sysmem_BufferUsage, llcpp::fuchsia::sysmem::BufferUsage>;
  return V2CopyFromV1BufferUsage(allocator, *CStruct::BorrowAsLlcpp(&v1));
}

fit::result<llcpp::fuchsia::sysmem2::BufferMemoryConstraints::Builder>
V2CopyFromV1BufferMemoryConstraints(fidl::Allocator* allocator,
                                    const llcpp::fuchsia::sysmem::BufferMemoryConstraints& v1) {
  ZX_DEBUG_ASSERT(allocator);
  auto v2b = allocator->make_table_builder<llcpp::fuchsia::sysmem2::BufferMemoryConstraints>();
  PROCESS_SCALAR_FIELD_V1(min_size_bytes);
  PROCESS_SCALAR_FIELD_V1(max_size_bytes);
  PROCESS_SCALAR_FIELD_V1(physically_contiguous_required);
  PROCESS_SCALAR_FIELD_V1(secure_required);
  PROCESS_SCALAR_FIELD_V1(ram_domain_supported);
  PROCESS_SCALAR_FIELD_V1(cpu_domain_supported);
  PROCESS_SCALAR_FIELD_V1(inaccessible_domain_supported);
  if (v1.heap_permitted_count) {
    auto result =
        V2CopyFromV1HeapPermittedArray(allocator, v1.heap_permitted, v1.heap_permitted_count);
    OK_OR_RET_ERROR(result);
    v2b.set_heap_permitted(MakeTracking(allocator, result.take_value()));
  }
  return fit::ok(std::move(v2b));
}

fit::result<llcpp::fuchsia::sysmem2::BufferMemoryConstraints::Builder>
V2CopyFromV1BufferMemoryConstraints(fidl::Allocator* allocator,
                                    const fuchsia_sysmem_BufferMemoryConstraints& v1) {
  using CStruct = FidlStruct<fuchsia_sysmem_BufferMemoryConstraints,
                             llcpp::fuchsia::sysmem::BufferMemoryConstraints>;
  return V2CopyFromV1BufferMemoryConstraints(allocator, *CStruct::BorrowAsLlcpp(&v1));
}

// If !v1 && !aux_buffers_v1, the result will be fit::is_ok(), but result.value().IsEmpty().
fit::result<llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder>
V2CopyFromV1BufferCollectionConstraints(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem::BufferCollectionConstraints* v1,
    const llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers* aux_buffers_v1) {
  ZX_DEBUG_ASSERT(allocator);
  // Should be enforced by the caller.
  ZX_DEBUG_ASSERT(v1 || !aux_buffers_v1);
  llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder v2b =
      allocator->make_table_builder<llcpp::fuchsia::sysmem2::BufferCollectionConstraints>();

  if (v1) {
    auto result = V2CopyFromV1BufferCollectionConstraintsMain(allocator, &v2b, *v1);
    OK_OR_RET_ERROR(result);
  }

  if (aux_buffers_v1) {
    auto result =
        V2CopyFromV1BufferCollectionConstraintsAuxBuffers(allocator, &v2b, *aux_buffers_v1);
    OK_OR_RET_ERROR(result);
  }

  return fit::ok(std::move(v2b));
}

fit::result<llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder>
V2CopyFromV1BufferCollectionConstraints(
    fidl::Allocator* allocator, const fuchsia_sysmem_BufferCollectionConstraints* v1,
    const fuchsia_sysmem_BufferCollectionConstraintsAuxBuffers* aux_buffers_v1) {
  using CStructMain = FidlStruct<fuchsia_sysmem_BufferCollectionConstraints,
                                 llcpp::fuchsia::sysmem::BufferCollectionConstraints>;
  using CStructAux = FidlStruct<fuchsia_sysmem_BufferCollectionConstraintsAuxBuffers,
                                llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers>;
  return V2CopyFromV1BufferCollectionConstraints(allocator, CStructMain::BorrowAsLlcpp(v1),
                                                 CStructAux::BorrowAsLlcpp(aux_buffers_v1));
}

fit::result<llcpp::fuchsia::sysmem2::ImageFormat::Builder> V2CopyFromV1ImageFormat(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem::ImageFormat_2& v1) {
  ZX_DEBUG_ASSERT(allocator);
  llcpp::fuchsia::sysmem2::ImageFormat::Builder v2b =
      allocator->make_table_builder<llcpp::fuchsia::sysmem2::ImageFormat>();
  v2b.set_pixel_format(
      sysmem::MakeTracking(allocator, V2CopyFromV1PixelFormat(allocator, v1.pixel_format).build()));
  PROCESS_SCALAR_FIELD_V1(coded_width);
  PROCESS_SCALAR_FIELD_V1(coded_height);
  PROCESS_SCALAR_FIELD_V1(bytes_per_row);
  PROCESS_SCALAR_FIELD_V1(display_width);
  PROCESS_SCALAR_FIELD_V1(display_height);
  if (v1.layers > 1) {
    LOG(ERROR, "v1.layers > 1");
    return fit::error();
  }
  v2b.set_color_space(
      sysmem::MakeTracking(allocator, V2CopyFromV1ColorSpace(allocator, v1.color_space).build()));
  if (v1.has_pixel_aspect_ratio) {
    v2b.set_pixel_aspect_ratio_width(sysmem::MakeTracking(allocator, v1.pixel_aspect_ratio_width));
    v2b.set_pixel_aspect_ratio_height(
        sysmem::MakeTracking(allocator, v1.pixel_aspect_ratio_height));
  } else {
    ZX_DEBUG_ASSERT(!v2b.has_pixel_aspect_ratio_width());
    ZX_DEBUG_ASSERT(!v2b.has_pixel_aspect_ratio_height());
  }
  return fit::ok(std::move(v2b));
}

fit::result<llcpp::fuchsia::sysmem2::ImageFormat::Builder> V2CopyFromV1ImageFormat(
    fidl::Allocator* allocator, const fuchsia_sysmem_ImageFormat_2& v1) {
  using CStruct = FidlStruct<fuchsia_sysmem_ImageFormat_2, llcpp::fuchsia::sysmem::ImageFormat_2>;
  return V2CopyFromV1ImageFormat(allocator, *CStruct::BorrowAsLlcpp(&v1));
}

[[nodiscard]] llcpp::fuchsia::sysmem2::BufferMemorySettings::Builder
V2CopyFromV1BufferMemorySettings(fidl::Allocator* allocator,
                                 const llcpp::fuchsia::sysmem::BufferMemorySettings& v1) {
  ZX_DEBUG_ASSERT(allocator);
  llcpp::fuchsia::sysmem2::BufferMemorySettings::Builder v2b =
      allocator->make_table_builder<llcpp::fuchsia::sysmem2::BufferMemorySettings>();
  PROCESS_SCALAR_FIELD_V1(size_bytes);
  PROCESS_SCALAR_FIELD_V1(is_physically_contiguous);
  PROCESS_SCALAR_FIELD_V1(is_secure);
  PROCESS_SCALAR_FIELD_V1(coherency_domain);
  PROCESS_SCALAR_FIELD_V1(heap);
  return v2b;
}

fit::result<llcpp::fuchsia::sysmem2::SingleBufferSettings::Builder>
V2CopyFromV1SingleBufferSettings(fidl::Allocator* allocator,
                                 const llcpp::fuchsia::sysmem::SingleBufferSettings& v1) {
  ZX_DEBUG_ASSERT(allocator);
  llcpp::fuchsia::sysmem2::SingleBufferSettings::Builder v2b =
      allocator->make_table_builder<llcpp::fuchsia::sysmem2::SingleBufferSettings>();
  v2b.set_buffer_settings(sysmem::MakeTracking(
      allocator, V2CopyFromV1BufferMemorySettings(allocator, v1.buffer_settings)));
  if (v1.has_image_format_constraints) {
    auto image_format_constraints_result =
        V2CopyFromV1ImageFormatConstraints(allocator, v1.image_format_constraints);
    if (!image_format_constraints_result.is_ok()) {
      LOG(ERROR, "!image_format_constraints_result.is_ok()");
      return fit::error();
    }
    v2b.set_image_format_constraints(
        sysmem::MakeTracking(allocator, image_format_constraints_result.take_value()));
  }
  return fit::ok(std::move(v2b));
}

llcpp::fuchsia::sysmem2::VmoBuffer::Builder V2MoveFromV1VmoBuffer(
    fidl::Allocator* allocator, llcpp::fuchsia::sysmem::VmoBuffer&& to_move_v1) {
  ZX_DEBUG_ASSERT(allocator);
  llcpp::fuchsia::sysmem::VmoBuffer v1 = std::move(to_move_v1);
  llcpp::fuchsia::sysmem2::VmoBuffer::Builder v2b =
      allocator->make_table_builder<llcpp::fuchsia::sysmem2::VmoBuffer>();
  if (v1.vmo) {
    v2b.set_vmo(sysmem::MakeTracking(allocator, std::move(v1.vmo)));
  }
  PROCESS_SCALAR_FIELD_V1(vmo_usable_start);
  ZX_DEBUG_ASSERT(!v2b.has_aux_vmo());
  return v2b;
}

fit::result<llcpp::fuchsia::sysmem2::BufferCollectionInfo::Builder>
V2MoveFromV1BufferCollectionInfo(fidl::Allocator* allocator,
                                 llcpp::fuchsia::sysmem::BufferCollectionInfo_2&& to_move_v1) {
  ZX_DEBUG_ASSERT(allocator);
  llcpp::fuchsia::sysmem::BufferCollectionInfo_2 v1 = std::move(to_move_v1);
  llcpp::fuchsia::sysmem2::BufferCollectionInfo::Builder v2b =
      allocator->make_table_builder<llcpp::fuchsia::sysmem2::BufferCollectionInfo>();
  auto settings_result = V2CopyFromV1SingleBufferSettings(allocator, v1.settings);
  if (!settings_result.is_ok()) {
    LOG(ERROR, "!settings_result.is_ok()");
    return fit::error();
  }
  v2b.set_settings(sysmem::MakeTracking(allocator, settings_result.take_value()));
  if (v1.buffer_count) {
    v2b.set_buffers(allocator->make_vec_ptr<llcpp::fuchsia::sysmem2::VmoBuffer>(v1.buffer_count));
    for (uint32_t i = 0; i < v1.buffer_count; ++i) {
      v2b.buffers()[i] = V2MoveFromV1VmoBuffer(allocator, std::move(v1.buffers[i])).build();
    }
  }
  return fit::ok(std::move(v2b));
}

fit::result<std::pair<std::optional<llcpp::fuchsia::sysmem::BufferCollectionConstraints>,
                      std::optional<llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers>>>
V1CopyFromV2BufferCollectionConstraints(
    const llcpp::fuchsia::sysmem2::BufferCollectionConstraints& v2) {
  llcpp::fuchsia::sysmem::BufferCollectionConstraints v1{};
  if (v2.IsEmpty()) {
    return fit::ok(
        std::pair<std::optional<llcpp::fuchsia::sysmem::BufferCollectionConstraints>,
                  std::optional<llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers>>());
  }
  if (v2.has_usage()) {
    v1.usage = V1CopyFromV2BufferUsage(v2.usage());
  }
  PROCESS_SCALAR_FIELD_V2(min_buffer_count_for_camping);
  PROCESS_SCALAR_FIELD_V2(min_buffer_count_for_dedicated_slack);
  PROCESS_SCALAR_FIELD_V2(min_buffer_count_for_shared_slack);
  PROCESS_SCALAR_FIELD_V2(min_buffer_count);
  PROCESS_SCALAR_FIELD_V2(max_buffer_count);
  ZX_DEBUG_ASSERT(!v1.has_buffer_memory_constraints);
  if (v2.has_buffer_memory_constraints()) {
    v1.has_buffer_memory_constraints = true;
    auto buffer_memory_constraints_result =
        V1CopyFromV2BufferMemoryConstraints(v2.buffer_memory_constraints());
    if (!buffer_memory_constraints_result.is_ok()) {
      LOG(ERROR, "!buffer_memory_constraints_result.is_ok()");
      return fit::error();
    }
    v1.buffer_memory_constraints = buffer_memory_constraints_result.take_value();
  }
  ZX_DEBUG_ASSERT(!v1.image_format_constraints_count);
  if (v2.has_image_format_constraints()) {
    if (v2.image_format_constraints().count() >
        llcpp::fuchsia::sysmem::MAX_COUNT_BUFFER_COLLECTION_CONSTRAINTS_IMAGE_FORMAT_CONSTRAINTS) {
      LOG(ERROR,
          "v2 image_format_constraints count > v1 "
          "MAX_COUNT_BUFFER_COLLECTION_CONSTRAINTS_IMAGE_FORMAT_CONSTRAINTS");
      return fit::error();
    }
    v1.image_format_constraints_count = v2.image_format_constraints().count();
    for (uint32_t i = 0; i < v2.image_format_constraints().count(); ++i) {
      auto image_format_constraints_result =
          V1CopyFromV2ImageFormatConstraints(v2.image_format_constraints()[i]);
      if (!image_format_constraints_result.is_ok()) {
        LOG(ERROR, "!image_format_constraints_result.is_ok()");
        return fit::error();
      }
      v1.image_format_constraints[i] = image_format_constraints_result.take_value();
    }
  }

  std::optional<llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers> v1_aux_buffers;
  if (v2.has_need_clear_aux_buffers_for_secure() || v2.has_allow_clear_aux_buffers_for_secure()) {
    llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers v1{};
    PROCESS_SCALAR_FIELD_V2(need_clear_aux_buffers_for_secure);
    PROCESS_SCALAR_FIELD_V2(allow_clear_aux_buffers_for_secure);
    v1_aux_buffers.emplace(v1);
  }

  return fit::ok(std::make_pair(std::move(v1), std::move(v1_aux_buffers)));
}

fit::result<llcpp::fuchsia::sysmem::BufferMemoryConstraints> V1CopyFromV2BufferMemoryConstraints(
    const llcpp::fuchsia::sysmem2::BufferMemoryConstraints& v2) {
  llcpp::fuchsia::sysmem::BufferMemoryConstraints v1{};
  PROCESS_SCALAR_FIELD_V2(min_size_bytes);
  PROCESS_SCALAR_FIELD_V2(max_size_bytes);
  PROCESS_SCALAR_FIELD_V2(physically_contiguous_required);
  PROCESS_SCALAR_FIELD_V2(secure_required);
  PROCESS_SCALAR_FIELD_V2(ram_domain_supported);
  PROCESS_SCALAR_FIELD_V2(cpu_domain_supported);
  PROCESS_SCALAR_FIELD_V2(inaccessible_domain_supported);
  ZX_DEBUG_ASSERT(!v1.heap_permitted_count);
  if (v2.has_heap_permitted()) {
    if (v2.heap_permitted().count() >
        llcpp::fuchsia::sysmem::MAX_COUNT_BUFFER_MEMORY_CONSTRAINTS_HEAP_PERMITTED) {
      LOG(ERROR, "v2 heap_permitted count > v1 MAX_COUNT_BUFFER_MEMORY_CONSTRAINTS_HEAP_PERMITTED");
      return fit::error();
    }
    v1.heap_permitted_count = v2.heap_permitted().count();
    for (uint32_t i = 0; i < v2.heap_permitted().count(); ++i) {
      ASSIGN_SCALAR(v1.heap_permitted[i], v2.heap_permitted()[i]);
    }
  }
  return fit::ok(std::move(v1));
}

llcpp::fuchsia::sysmem::BufferUsage V1CopyFromV2BufferUsage(
    const llcpp::fuchsia::sysmem2::BufferUsage& v2) {
  llcpp::fuchsia::sysmem::BufferUsage v1{};
  PROCESS_SCALAR_FIELD_V2(none);
  PROCESS_SCALAR_FIELD_V2(cpu);
  PROCESS_SCALAR_FIELD_V2(vulkan);
  PROCESS_SCALAR_FIELD_V2(display);
  PROCESS_SCALAR_FIELD_V2(video);
  return v1;
}

// v2 must have all fields set.
llcpp::fuchsia::sysmem::BufferMemorySettings V1CopyFromV2BufferMemorySettings(
    const llcpp::fuchsia::sysmem2::BufferMemorySettings& v2) {
  llcpp::fuchsia::sysmem::BufferMemorySettings v1{};
  PROCESS_SCALAR_FIELD_V2(size_bytes);
  PROCESS_SCALAR_FIELD_V2(is_physically_contiguous);
  PROCESS_SCALAR_FIELD_V2(is_secure);
  PROCESS_SCALAR_FIELD_V2(coherency_domain);
  PROCESS_SCALAR_FIELD_V2(heap);
  return v1;
}

llcpp::fuchsia::sysmem::PixelFormat V1CopyFromV2PixelFormat(
    const llcpp::fuchsia::sysmem2::PixelFormat& v2) {
  llcpp::fuchsia::sysmem::PixelFormat v1;
  PROCESS_SCALAR_FIELD_V2(type);
  v1.has_format_modifier = v2.has_format_modifier_value();
  if (v2.has_format_modifier_value()) {
    v1.format_modifier.value = v2.format_modifier_value();
  }
  return v1;
}

llcpp::fuchsia::sysmem::ColorSpace V1CopyFromV2ColorSpace(
    const llcpp::fuchsia::sysmem2::ColorSpace& v2) {
  llcpp::fuchsia::sysmem::ColorSpace v1{};
  PROCESS_SCALAR_FIELD_V2(type);
  return v1;
}

fit::result<llcpp::fuchsia::sysmem::ImageFormatConstraints> V1CopyFromV2ImageFormatConstraints(
    const llcpp::fuchsia::sysmem2::ImageFormatConstraints& v2) {
  llcpp::fuchsia::sysmem::ImageFormatConstraints v1;
  v1.pixel_format = V1CopyFromV2PixelFormat(v2.pixel_format());
  ZX_DEBUG_ASSERT(!v1.color_spaces_count);
  if (v2.has_color_spaces()) {
    if (v2.color_spaces().count() >
        llcpp::fuchsia::sysmem::MAX_COUNT_IMAGE_FORMAT_CONSTRAINTS_COLOR_SPACES) {
      LOG(ERROR,
          "v2.color_spaces().count() > "
          "llcpp::fuchsia::sysmem::MAX_COUNT_IMAGE_FORMAT_CONSTRAINTS_COLOR_SPACES");
      return fit::error();
    }
    v1.color_spaces_count = v2.color_spaces().count();
    for (uint32_t i = 0; i < v2.color_spaces().count(); ++i) {
      v1.color_space[i] = V1CopyFromV2ColorSpace(v2.color_spaces()[i]);
    }
  }
  PROCESS_SCALAR_FIELD_V2(min_coded_width);
  PROCESS_SCALAR_FIELD_V2(max_coded_width);
  PROCESS_SCALAR_FIELD_V2(min_coded_height);
  PROCESS_SCALAR_FIELD_V2(max_coded_height);
  PROCESS_SCALAR_FIELD_V2(min_bytes_per_row);
  PROCESS_SCALAR_FIELD_V2(max_bytes_per_row);
  PROCESS_SCALAR_FIELD_V2(max_coded_width_times_coded_height);
  v1.layers = 1;
  PROCESS_SCALAR_FIELD_V2(coded_width_divisor);
  PROCESS_SCALAR_FIELD_V2(coded_height_divisor);
  PROCESS_SCALAR_FIELD_V2(bytes_per_row_divisor);
  PROCESS_SCALAR_FIELD_V2(start_offset_divisor);
  PROCESS_SCALAR_FIELD_V2(display_width_divisor);
  PROCESS_SCALAR_FIELD_V2(display_height_divisor);
  PROCESS_SCALAR_FIELD_V2(required_min_coded_width);
  PROCESS_SCALAR_FIELD_V2(required_max_coded_width);
  PROCESS_SCALAR_FIELD_V2(required_min_coded_height);
  PROCESS_SCALAR_FIELD_V2(required_max_coded_height);
  PROCESS_SCALAR_FIELD_V2(required_min_bytes_per_row);
  PROCESS_SCALAR_FIELD_V2(required_max_bytes_per_row);
  return fit::ok(std::move(v1));
}

fit::result<llcpp::fuchsia::sysmem::ImageFormat_2> V1CopyFromV2ImageFormat(
    llcpp::fuchsia::sysmem2::ImageFormat& v2) {
  llcpp::fuchsia::sysmem::ImageFormat_2 v1;
  if (v2.has_pixel_format()) {
    v1.pixel_format = V1CopyFromV2PixelFormat(v2.pixel_format());
  }
  PROCESS_SCALAR_FIELD_V2(coded_width);
  PROCESS_SCALAR_FIELD_V2(coded_height);
  PROCESS_SCALAR_FIELD_V2(bytes_per_row);
  PROCESS_SCALAR_FIELD_V2(display_width);
  PROCESS_SCALAR_FIELD_V2(display_height);
  v1.layers = 1;
  if (v2.has_color_space()) {
    v1.color_space = V1CopyFromV2ColorSpace(v2.color_space());
  }
  v1.has_pixel_aspect_ratio =
      v2.has_pixel_aspect_ratio_width() && v2.has_pixel_aspect_ratio_height() ? true : false;
  if (v1.has_pixel_aspect_ratio) {
    v1.pixel_aspect_ratio_width = v2.pixel_aspect_ratio_width();
    v1.pixel_aspect_ratio_height = v2.pixel_aspect_ratio_height();
  }
  return fit::ok(std::move(v1));
}

fit::result<llcpp::fuchsia::sysmem::SingleBufferSettings> V1CopyFromV2SingleBufferSettings(
    const llcpp::fuchsia::sysmem2::SingleBufferSettings& v2) {
  llcpp::fuchsia::sysmem::SingleBufferSettings v1;
  v1.buffer_settings = V1CopyFromV2BufferMemorySettings(v2.buffer_settings());
  v1.has_image_format_constraints = v2.has_image_format_constraints();
  if (v2.has_image_format_constraints()) {
    auto image_format_constraints_result =
        V1CopyFromV2ImageFormatConstraints(v2.image_format_constraints());
    if (!image_format_constraints_result.is_ok()) {
      LOG(ERROR, "!image_format_constraints_result.is_ok()");
      return fit::error();
    }
    v1.image_format_constraints = image_format_constraints_result.take_value();
  }
  return fit::ok(std::move(v1));
}

// Intentionally just consumes aux_vmo.  The implied extra handle duplications from this behavior go
// away when all participants speak V2.  For V1 participants that need the aux_vmo, there's a
// completely different method and struct for getting that.
llcpp::fuchsia::sysmem::VmoBuffer V1MoveFromV2VmoBuffer(
    llcpp::fuchsia::sysmem2::VmoBuffer&& to_move_v2) {
  // Always take ownership even on failure.
  llcpp::fuchsia::sysmem2::VmoBuffer v2 = std::move(to_move_v2);
  llcpp::fuchsia::sysmem::VmoBuffer v1;
  if (v2.has_vmo()) {
    v1.vmo = std::move(v2.vmo());
  }
  PROCESS_SCALAR_FIELD_V2(vmo_usable_start);
  // ~v2 will ~aux_vmo, intentionally - see function comment above.
  return v1;
}

// Intentionally just consumes vmo (but not aux_vmo).  The implied extra handle duplications from
// this behavior go away when all participants speak V2.
llcpp::fuchsia::sysmem::VmoBuffer V1AuxBuffersMoveFromV2VmoBuffer(
    llcpp::fuchsia::sysmem2::VmoBuffer&& to_move_v2) {
  // Always take ownership even on failure.
  llcpp::fuchsia::sysmem2::VmoBuffer v2 = std::move(to_move_v2);
  llcpp::fuchsia::sysmem::VmoBuffer v1;
  if (v2.has_aux_vmo()) {
    v1.vmo = std::move(v2.aux_vmo());
  }
  PROCESS_SCALAR_FIELD_V2(vmo_usable_start);
  // ~v2 will ~vmo, intentionally - see function comment above.
  return v1;
}

fit::result<llcpp::fuchsia::sysmem::BufferCollectionInfo_2> V1MoveFromV2BufferCollectionInfo(
    llcpp::fuchsia::sysmem2::BufferCollectionInfo&& to_move_v2) {
  // This move is mainly to make it very clear what's going on here, but also to ensure that we
  // don't take any dependency on incorrect/stale failure to move out any of the handles in to_move.
  //
  // Always take ownership of the incoming handles, even on failure.
  llcpp::fuchsia::sysmem2::BufferCollectionInfo v2 = std::move(to_move_v2);
  llcpp::fuchsia::sysmem::BufferCollectionInfo_2 v1;
  if (v2.has_buffers()) {
    if (v2.buffers().count() > llcpp::fuchsia::sysmem::MAX_COUNT_BUFFER_COLLECTION_INFO_BUFFERS) {
      LOG(ERROR,
          "v2.buffers().count() > "
          "llcpp::fuchsia::sysmem::MAX_COUNT_BUFFER_COLLECTION_INFO_BUFFERS");
      return fit::error();
    }
    v1.buffer_count = v2.buffers().count();
    for (uint32_t i = 0; i < v2.buffers().count(); ++i) {
      v1.buffers[i] = V1MoveFromV2VmoBuffer(std::move(v2.buffers()[i]));
    }
  }
  auto settings_result = V1CopyFromV2SingleBufferSettings(v2.settings());
  if (!settings_result.is_ok()) {
    LOG(ERROR, "!settings_result.is_ok()");
    return fit::error();
  }
  v1.settings = settings_result.take_value();
  return fit::ok(std::move(v1));
}

[[nodiscard]] fit::result<llcpp::fuchsia::sysmem::BufferCollectionInfo_2>
V1AuxBuffersMoveFromV2BufferCollectionInfo(
    llcpp::fuchsia::sysmem2::BufferCollectionInfo&& to_move_v2) {
  // This move is mainly to make it very clear what's going on here, but also to ensure that we
  // don't take any dependency on incorrect/stale failure to move out any of the handles in to_move.
  //
  // Always take ownership of the incoming handles, even on failure.
  llcpp::fuchsia::sysmem2::BufferCollectionInfo v2 = std::move(to_move_v2);
  llcpp::fuchsia::sysmem::BufferCollectionInfo_2 v1;
  if (v2.has_buffers()) {
    if (v2.buffers().count() > llcpp::fuchsia::sysmem::MAX_COUNT_BUFFER_COLLECTION_INFO_BUFFERS) {
      LOG(ERROR,
          "v2.buffers().count() > "
          "llcpp::fuchsia::sysmem::MAX_COUNT_BUFFER_COLLECTION_INFO_BUFFERS");
      return fit::error();
    }
    v1.buffer_count = v2.buffers().count();
    for (uint32_t i = 0; i < v2.buffers().count(); ++i) {
      v1.buffers[i] = V1AuxBuffersMoveFromV2VmoBuffer(std::move(v2.buffers()[i]));
    }
  }
  auto settings_result = V1CopyFromV2SingleBufferSettings(v2.settings());
  if (!settings_result.is_ok()) {
    LOG(ERROR, "!settings_result.is_ok()");
    return fit::error();
  }
  v1.settings = settings_result.take_value();
  return fit::ok(std::move(v1));
}

llcpp::fuchsia::sysmem2::PixelFormat::Builder V2ClonePixelFormat(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::PixelFormat& src) {
  auto builder = allocator->make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>();
  if (src.has_type()) {
    builder.set_type(sysmem::MakeTracking(allocator, src.type()));
  }
  if (src.has_format_modifier_value()) {
    builder.set_format_modifier_value(sysmem::MakeTracking(allocator, src.format_modifier_value()));
  }
  return builder;
}

llcpp::fuchsia::sysmem2::ColorSpace::Builder V2CloneColorSpace(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::ColorSpace& src) {
  auto builder = allocator->make_table_builder<llcpp::fuchsia::sysmem2::ColorSpace>();
  if (src.has_type()) {
    builder.set_type(sysmem::MakeTracking(allocator, src.type()));
  }
  return builder;
}

llcpp::fuchsia::sysmem2::BufferMemorySettings::Builder V2CloneBufferMemorySettings(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::BufferMemorySettings& src) {
  auto builder = allocator->make_table_builder<llcpp::fuchsia::sysmem2::BufferMemorySettings>();
  if (src.has_size_bytes()) {
    builder.set_size_bytes(sysmem::MakeTracking(allocator, src.size_bytes()));
  }
  if (src.has_is_physically_contiguous()) {
    builder.set_is_physically_contiguous(
        sysmem::MakeTracking(allocator, src.is_physically_contiguous()));
  }
  if (src.has_is_secure()) {
    builder.set_is_secure(sysmem::MakeTracking(allocator, src.is_secure()));
  }
  if (src.has_coherency_domain()) {
    builder.set_coherency_domain(sysmem::MakeTracking(allocator, src.coherency_domain()));
  }
  if (src.has_heap()) {
    builder.set_heap(sysmem::MakeTracking(allocator, src.heap()));
  }
  return builder;
}

llcpp::fuchsia::sysmem2::ImageFormatConstraints::Builder V2CloneImageFormatConstraints(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::ImageFormatConstraints& src) {
  auto builder = allocator->make_table_builder<llcpp::fuchsia::sysmem2::ImageFormatConstraints>();
  if (src.has_pixel_format()) {
    builder.set_pixel_format(
        sysmem::MakeTracking(allocator, V2ClonePixelFormat(allocator, src.pixel_format()).build()));
  }
  if (src.has_color_spaces()) {
    builder.set_color_spaces(
        allocator->make_vec_ptr<llcpp::fuchsia::sysmem2::ColorSpace>(src.color_spaces().count()));
    for (uint32_t i = 0; i < src.color_spaces().count(); ++i) {
      builder.color_spaces()[i] = V2CloneColorSpace(allocator, src.color_spaces()[i]).build();
    }
  }
  if (src.has_min_coded_width()) {
    builder.set_min_coded_width(sysmem::MakeTracking(allocator, src.min_coded_width()));
  }
  if (src.has_max_coded_width()) {
    builder.set_max_coded_width(sysmem::MakeTracking(allocator, src.max_coded_width()));
  }
  if (src.has_min_coded_height()) {
    builder.set_min_coded_height(sysmem::MakeTracking(allocator, src.min_coded_height()));
  }
  if (src.has_max_coded_height()) {
    builder.set_max_coded_height(sysmem::MakeTracking(allocator, src.max_coded_height()));
  }
  if (src.has_min_bytes_per_row()) {
    builder.set_min_bytes_per_row(sysmem::MakeTracking(allocator, src.min_bytes_per_row()));
  }
  if (src.has_max_bytes_per_row()) {
    builder.set_max_bytes_per_row(sysmem::MakeTracking(allocator, src.max_bytes_per_row()));
  }
  if (src.has_max_coded_width_times_coded_height()) {
    builder.set_max_coded_width_times_coded_height(
        sysmem::MakeTracking(allocator, src.max_coded_width_times_coded_height()));
  }
  if (src.has_coded_width_divisor()) {
    builder.set_coded_width_divisor(sysmem::MakeTracking(allocator, src.coded_width_divisor()));
  }
  if (src.has_coded_height_divisor()) {
    builder.set_coded_height_divisor(sysmem::MakeTracking(allocator, src.coded_height_divisor()));
  }
  if (src.has_bytes_per_row_divisor()) {
    builder.set_bytes_per_row_divisor(sysmem::MakeTracking(allocator, src.bytes_per_row_divisor()));
  }
  if (src.has_start_offset_divisor()) {
    builder.set_start_offset_divisor(sysmem::MakeTracking(allocator, src.start_offset_divisor()));
  }
  if (src.has_display_width_divisor()) {
    builder.set_display_width_divisor(sysmem::MakeTracking(allocator, src.display_width_divisor()));
  }
  if (src.has_display_height_divisor()) {
    builder.set_display_height_divisor(
        sysmem::MakeTracking(allocator, src.display_height_divisor()));
  }
  if (src.has_required_min_coded_width()) {
    builder.set_required_min_coded_width(
        sysmem::MakeTracking(allocator, src.required_min_coded_width()));
  }
  if (src.has_required_max_coded_width()) {
    builder.set_required_max_coded_width(
        sysmem::MakeTracking(allocator, src.required_max_coded_width()));
  }
  if (src.has_required_min_coded_height()) {
    builder.set_required_min_coded_height(
        sysmem::MakeTracking(allocator, src.required_min_coded_height()));
  }
  if (src.has_required_max_coded_height()) {
    builder.set_required_max_coded_height(
        sysmem::MakeTracking(allocator, src.required_max_coded_height()));
  }
  if (src.has_required_min_bytes_per_row()) {
    builder.set_required_min_bytes_per_row(
        sysmem::MakeTracking(allocator, src.required_min_bytes_per_row()));
  }
  if (src.has_required_max_bytes_per_row()) {
    builder.set_required_max_bytes_per_row(
        sysmem::MakeTracking(allocator, src.required_max_bytes_per_row()));
  }
  return builder;
}

llcpp::fuchsia::sysmem2::SingleBufferSettings::Builder V2CloneSingleBufferSettings(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::SingleBufferSettings& src) {
  auto builder = allocator->make_table_builder<llcpp::fuchsia::sysmem2::SingleBufferSettings>();
  if (src.has_buffer_settings()) {
    builder.set_buffer_settings(sysmem::MakeTracking(
        allocator, V2CloneBufferMemorySettings(allocator, src.buffer_settings()).build()));
  }
  if (src.has_image_format_constraints()) {
    builder.set_image_format_constraints(sysmem::MakeTracking(
        allocator,
        V2CloneImageFormatConstraints(allocator, src.image_format_constraints()).build()));
  }
  return builder;
}

llcpp::fuchsia::sysmem2::SingleBufferSettings::Builder V2CloneSingleBufferSettingsBuilder(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::SingleBufferSettings::Builder& src) {
  auto builder = allocator->make_table_builder<llcpp::fuchsia::sysmem2::SingleBufferSettings>();
  if (src.has_buffer_settings()) {
    builder.set_buffer_settings(sysmem::MakeTracking(
        allocator, V2CloneBufferMemorySettings(allocator, src.buffer_settings()).build()));
  }
  if (src.has_image_format_constraints()) {
    builder.set_image_format_constraints(sysmem::MakeTracking(
        allocator,
        V2CloneImageFormatConstraints(allocator, src.image_format_constraints()).build()));
  }
  return builder;
}

fit::result<llcpp::fuchsia::sysmem2::VmoBuffer::Builder, zx_status_t> V2CloneVmoBuffer(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::VmoBuffer& src,
    uint32_t vmo_rights_mask, uint32_t aux_vmo_rights_mask) {
  auto builder = allocator->make_table_builder<llcpp::fuchsia::sysmem2::VmoBuffer>();
  if (src.has_vmo()) {
    zx_info_handle_basic_t info{};
    zx_status_t get_info_status =
        src.vmo().get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    if (get_info_status != ZX_OK) {
      LOG(ERROR, "get_info_status: %d", get_info_status);
      return fit::error(get_info_status);
    }
    zx::vmo clone_vmo;
    zx_status_t duplicate_status = src.vmo().duplicate(info.rights & vmo_rights_mask, &clone_vmo);
    if (duplicate_status != ZX_OK) {
      LOG(ERROR, "duplicate_status: %d", duplicate_status);
      return fit::error(duplicate_status);
    }
    builder.set_vmo(sysmem::MakeTracking(allocator, std::move(clone_vmo)));
  }
  if (src.has_vmo_usable_start()) {
    builder.set_vmo_usable_start(sysmem::MakeTracking(allocator, src.vmo_usable_start()));
  }
  if (src.has_aux_vmo()) {
    zx_info_handle_basic_t info{};
    zx_status_t get_info_status =
        src.aux_vmo().get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    if (get_info_status != ZX_OK) {
      LOG(ERROR, "get_info_status: %d", get_info_status);
      return fit::error(get_info_status);
    }
    zx::vmo clone_vmo;
    zx_status_t duplicate_status =
        src.aux_vmo().duplicate(info.rights & aux_vmo_rights_mask, &clone_vmo);
    if (duplicate_status != ZX_OK) {
      LOG(ERROR, "duplicate_status: %d", duplicate_status);
      return fit::error(duplicate_status);
    }
    builder.set_aux_vmo(sysmem::MakeTracking(allocator, std::move(clone_vmo)));
  }
  return fit::ok(std::move(builder));
}

fit::result<llcpp::fuchsia::sysmem2::BufferCollectionInfo::Builder, zx_status_t>
V2CloneBufferCollectionInfo(fidl::Allocator* allocator,
                            const llcpp::fuchsia::sysmem2::BufferCollectionInfo& src,
                            uint32_t vmo_rights_mask, uint32_t aux_vmo_rights_mask) {
  auto builder = allocator->make_table_builder<llcpp::fuchsia::sysmem2::BufferCollectionInfo>();
  if (src.has_settings()) {
    builder.set_settings(sysmem::MakeTracking(
        allocator, V2CloneSingleBufferSettings(allocator, src.settings()).build()));
  }
  if (src.has_buffers()) {
    builder.set_buffers(
        allocator->make_vec_ptr<llcpp::fuchsia::sysmem2::VmoBuffer>(src.buffers().count()));
    for (uint32_t i = 0; i < src.buffers().count(); ++i) {
      auto clone_result =
          V2CloneVmoBuffer(allocator, src.buffers()[i], vmo_rights_mask, aux_vmo_rights_mask);
      if (!clone_result.is_ok()) {
        return clone_result.take_error_result();
      }
      builder.buffers()[i] = clone_result.take_value().build();
    }
  }
  return fit::ok(std::move(builder));
}

llcpp::fuchsia::sysmem2::CoherencyDomainSupport::Builder V2CloneCoherencyDomainSuppoort(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::CoherencyDomainSupport& src) {
  auto builder = allocator->make_table_builder<llcpp::fuchsia::sysmem2::CoherencyDomainSupport>();
  if (src.has_cpu_supported()) {
    builder.set_cpu_supported(sysmem::MakeTracking(allocator, src.cpu_supported()));
  }
  if (src.has_ram_supported()) {
    builder.set_ram_supported(sysmem::MakeTracking(allocator, src.ram_supported()));
  }
  if (src.has_inaccessible_supported()) {
    builder.set_inaccessible_supported(
        sysmem::MakeTracking(allocator, src.inaccessible_supported()));
  }
  return builder;
}

llcpp::fuchsia::sysmem2::HeapProperties::Builder V2CloneHeapProperties(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::HeapProperties& src) {
  auto builder = allocator->make_table_builder<llcpp::fuchsia::sysmem2::HeapProperties>();
  if (src.has_coherency_domain_support()) {
    builder.set_coherency_domain_support(sysmem::MakeTracking(
        allocator,
        V2CloneCoherencyDomainSuppoort(allocator, src.coherency_domain_support()).build()));
  }
  if (src.has_need_clear()) {
    builder.set_need_clear(sysmem::MakeTracking(allocator, src.need_clear()));
  }
  return builder;
}

}  // namespace sysmem
