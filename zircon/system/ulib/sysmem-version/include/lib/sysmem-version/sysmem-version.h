// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSMEM_VERSION_SYSMEM_VERSION_H_
#define LIB_SYSMEM_VERSION_SYSMEM_VERSION_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <fuchsia/sysmem2/llcpp/fidl.h>

namespace sysmem {

namespace internal {

// Can be replaced with std::type_identity<> when C++20.
template <typename T>
struct TypeIdentity {
  using type = T;
};
template <typename T>
using TypeIdentity_t = typename TypeIdentity<T>::type;

// UnderlyingTypeOrType<T>::type gets std::underlying_type<T>::type if T is an enum, or T otherwise.
template <typename T, typename Enable = void>
struct UnderlyingTypeOrType : TypeIdentity<T> {};
template <typename T>
struct UnderlyingTypeOrType<T, typename std::enable_if<std::is_enum<T>::value>::type>
    : std::underlying_type<T> {};
template <typename T>
using UnderlyingTypeOrType_t = typename UnderlyingTypeOrType<T>::type;

}  // namespace internal

///////////////////////
// V2 Copy/Move from V1
///////////////////////

// We provide copy when the v1 Layout=Simple struct has MaxNumHandles == 0.
// We provide move when the v1 Layout=Simple struct has MaxNumHandles != 0.

// When we provide move, we only provide move from llcpp, not from FIDL C.
//
// See fidl_struct.h's TakeAsLlcpp() for a way to convert from FIDL C to llcpp first.

[[nodiscard]] fuchsia_sysmem2::wire::PixelFormat V2CopyFromV1PixelFormat(
    fidl::AnyArena& allocator, const fuchsia_sysmem::wire::PixelFormat& v1);
[[nodiscard]] fuchsia_sysmem2::wire::PixelFormat V2CopyFromV1PixelFormat(
    fidl::AnyArena& allocator, const fuchsia_sysmem_PixelFormat& v1);

[[nodiscard]] fuchsia_sysmem2::wire::ColorSpace V2CopyFromV1ColorSpace(
    fidl::AnyArena& allocator, const fuchsia_sysmem::wire::ColorSpace& v1);
[[nodiscard]] fuchsia_sysmem2::wire::ColorSpace V2CopyFromV1ColorSpace(
    fidl::AnyArena& allocator, const fuchsia_sysmem_ColorSpace& v1);

[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::ImageFormatConstraints>
V2CopyFromV1ImageFormatConstraints(fidl::AnyArena& allocator,
                                   const fuchsia_sysmem::wire::ImageFormatConstraints& v1);
[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::ImageFormatConstraints>
V2CopyFromV1ImageFormatConstraints(fidl::AnyArena& allocator,
                                   const fuchsia_sysmem_ImageFormatConstraints& v1);

[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::BufferUsage> V2CopyFromV1BufferUsage(
    fidl::AnyArena& allocator, const fuchsia_sysmem::wire::BufferUsage& v1);
[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::BufferUsage> V2CopyFromV1BufferUsage(
    fidl::AnyArena& allocator, const fuchsia_sysmem_BufferUsage& v1);

[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::BufferMemoryConstraints>
V2CopyFromV1BufferMemoryConstraints(fidl::AnyArena& allocator,
                                    const fuchsia_sysmem::wire::BufferMemoryConstraints& v1);
[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::BufferMemoryConstraints>
V2CopyFromV1BufferMemoryConstraints(fidl::AnyArena& allocator,
                                    const fuchsia_sysmem_BufferMemoryConstraints& v1);

[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::BufferCollectionConstraints>
V2CopyFromV1BufferCollectionConstraints(
    fidl::AnyArena& allocator, const fuchsia_sysmem::wire::BufferCollectionConstraints* v1,
    const fuchsia_sysmem::wire::BufferCollectionConstraintsAuxBuffers* aux_buffers_v1);
[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::BufferCollectionConstraints>
V2CopyFromV1BufferCollectionConstraints(
    fidl::AnyArena& allocator, const fuchsia_sysmem_BufferCollectionConstraints* v1,
    const fuchsia_sysmem_BufferCollectionConstraintsAuxBuffers* aux_buffers_v1);

[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::ImageFormat> V2CopyFromV1ImageFormat(
    fidl::AnyArena& allocator, const fuchsia_sysmem::wire::ImageFormat2& v1);
[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::ImageFormat> V2CopyFromV1ImageFormat(
    fidl::AnyArena& allocator, const fuchsia_sysmem_ImageFormat_2& v1);

[[nodiscard]] fuchsia_sysmem2::wire::BufferMemorySettings V2CopyFromV1BufferMemorySettings(
    fidl::AnyArena& allocator, const fuchsia_sysmem::wire::BufferMemorySettings& v1);

[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::SingleBufferSettings>
V2CopyFromV1SingleBufferSettings(fidl::AnyArena& allocator,
                                 const fuchsia_sysmem::wire::SingleBufferSettings& v1);

[[nodiscard]] fuchsia_sysmem2::wire::VmoBuffer V2MoveFromV1VmoBuffer(
    fidl::AnyArena& allocator, fuchsia_sysmem::wire::VmoBuffer&& to_move_v1);
[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::BufferCollectionInfo>
V2MoveFromV1BufferCollectionInfo(fidl::AnyArena& allocator,
                                 fuchsia_sysmem::wire::BufferCollectionInfo2&& to_move_v1);

///////////////////////
// V1 Copy/Move from V2
///////////////////////

[[nodiscard]] fpromise::result<
    std::pair<std::optional<fuchsia_sysmem::wire::BufferCollectionConstraints>,
              std::optional<fuchsia_sysmem::wire::BufferCollectionConstraintsAuxBuffers>>>
V1CopyFromV2BufferCollectionConstraints(
    const fuchsia_sysmem2::wire::BufferCollectionConstraints& v2);
[[nodiscard]] fpromise::result<fuchsia_sysmem::wire::BufferMemoryConstraints>
V1CopyFromV2BufferMemoryConstraints(const fuchsia_sysmem2::wire::BufferMemoryConstraints& v2);
[[nodiscard]] fuchsia_sysmem::wire::BufferUsage V1CopyFromV2BufferUsage(
    const fuchsia_sysmem2::wire::BufferUsage& v2);
[[nodiscard]] fuchsia_sysmem::wire::BufferMemorySettings V1CopyFromV2BufferMemorySettings(
    const fuchsia_sysmem2::wire::BufferMemorySettings& v2);
[[nodiscard]] fuchsia_sysmem::wire::PixelFormat V1CopyFromV2PixelFormat(
    const fuchsia_sysmem2::wire::PixelFormat& v2);
[[nodiscard]] fuchsia_sysmem::wire::ColorSpace V1CopyFromV2ColorSpace(
    const fuchsia_sysmem2::wire::ColorSpace& v2);
[[nodiscard]] fpromise::result<fuchsia_sysmem::wire::ImageFormatConstraints>
V1CopyFromV2ImageFormatConstraints(const fuchsia_sysmem2::wire::ImageFormatConstraints& v2);
[[nodiscard]] fpromise::result<fuchsia_sysmem::wire::ImageFormat2> V1CopyFromV2ImageFormat(
    fuchsia_sysmem2::wire::ImageFormat& v2);
[[nodiscard]] fpromise::result<fuchsia_sysmem::wire::SingleBufferSettings>
V1CopyFromV2SingleBufferSettings(const fuchsia_sysmem2::wire::SingleBufferSettings& v2);
[[nodiscard]] fuchsia_sysmem::wire::VmoBuffer V1MoveFromV2VmoBuffer(
    fuchsia_sysmem2::wire::VmoBuffer&& to_move_v2);
[[nodiscard]] fuchsia_sysmem::wire::VmoBuffer V1AuxBuffersMoveFromV2VmoBuffer(
    fuchsia_sysmem2::wire::VmoBuffer&& to_move_v2);
[[nodiscard]] fpromise::result<fuchsia_sysmem::wire::BufferCollectionInfo2>
V1MoveFromV2BufferCollectionInfo(fuchsia_sysmem2::wire::BufferCollectionInfo&& to_move_v2);
[[nodiscard]] fpromise::result<fuchsia_sysmem::wire::BufferCollectionInfo2>
V1AuxBuffersMoveFromV2BufferCollectionInfo(
    fuchsia_sysmem2::wire::BufferCollectionInfo&& to_move_v2);

///////////
// V2 Clone
///////////

[[nodiscard]] fuchsia_sysmem2::wire::PixelFormat V2ClonePixelFormat(
    fidl::AnyArena& allocator, const fuchsia_sysmem2::wire::PixelFormat& src);
[[nodiscard]] fuchsia_sysmem2::wire::ColorSpace V2CloneColorSpace(
    fidl::AnyArena& allocator, const fuchsia_sysmem2::wire::ColorSpace& src);
[[nodiscard]] fuchsia_sysmem2::wire::BufferMemorySettings V2CloneBufferMemorySettings(
    fidl::AnyArena& allocator, const fuchsia_sysmem2::wire::BufferMemorySettings& src);
[[nodiscard]] fuchsia_sysmem2::wire::ImageFormatConstraints V2CloneImageFormatConstraints(
    fidl::AnyArena& allocator, const fuchsia_sysmem2::wire::ImageFormatConstraints& src);
[[nodiscard]] fuchsia_sysmem2::wire::SingleBufferSettings V2CloneSingleBufferSettings(
    fidl::AnyArena& allocator, const fuchsia_sysmem2::wire::SingleBufferSettings& src);
[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::VmoBuffer, zx_status_t> V2CloneVmoBuffer(
    fidl::AnyArena& allocator, const fuchsia_sysmem2::wire::VmoBuffer& src,
    uint32_t vmo_rights_mask, uint32_t aux_vmo_rights_mask);
[[nodiscard]] fpromise::result<fuchsia_sysmem2::wire::BufferCollectionInfo, zx_status_t>
V2CloneBufferCollectionInfo(fidl::AnyArena& allocator,
                            const fuchsia_sysmem2::wire::BufferCollectionInfo& src,
                            uint32_t vmo_rights_mask, uint32_t aux_vmo_rights_mask);
[[nodiscard]] fuchsia_sysmem2::wire::CoherencyDomainSupport V2CloneCoherencyDomainSuppoort(
    fidl::AnyArena& allocator, const fuchsia_sysmem2::wire::CoherencyDomainSupport& src);
[[nodiscard]] fuchsia_sysmem2::wire::HeapProperties V2CloneHeapProperties(
    fidl::AnyArena& allocator, const fuchsia_sysmem2::wire::HeapProperties& src);
[[nodiscard]] fuchsia_sysmem2::wire::BufferCollectionConstraints V2CloneBufferCollectionConstraints(
    fidl::AnyArena& allocator, const fuchsia_sysmem2::wire::BufferCollectionConstraints& src);
[[nodiscard]] fuchsia_sysmem2::wire::BufferUsage V2CloneBufferUsage(
    fidl::AnyArena& allocator, const fuchsia_sysmem2::wire::BufferUsage& src);
[[nodiscard]] fuchsia_sysmem2::wire::BufferMemoryConstraints V2CloneBufferMemoryConstraints(
    fidl::AnyArena& allocator, const fuchsia_sysmem2::wire::BufferMemoryConstraints& src);

}  // namespace sysmem

#endif  // LIB_SYSMEM_VERSION_SYSMEM_VERSION_H_
