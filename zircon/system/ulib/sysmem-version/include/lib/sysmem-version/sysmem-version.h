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

[[nodiscard]] llcpp::fuchsia::sysmem2::PixelFormat::Builder V2CopyFromV1PixelFormat(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem::PixelFormat& v1);
[[nodiscard]] llcpp::fuchsia::sysmem2::PixelFormat::Builder V2CopyFromV1PixelFormat(
    fidl::Allocator* allocator, const fuchsia_sysmem_PixelFormat& v1);

[[nodiscard]] llcpp::fuchsia::sysmem2::ColorSpace::Builder V2CopyFromV1ColorSpace(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem::ColorSpace& v1);
[[nodiscard]] llcpp::fuchsia::sysmem2::ColorSpace::Builder V2CopyFromV1ColorSpace(
    fidl::Allocator* allocator, const fuchsia_sysmem_ColorSpace& v1);

[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::ImageFormatConstraints::Builder>
V2CopyFromV1ImageFormatConstraints(fidl::Allocator* allocator,
                                   const llcpp::fuchsia::sysmem::ImageFormatConstraints& v1);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::ImageFormatConstraints::Builder>
V2CopyFromV1ImageFormatConstraints(fidl::Allocator* allocator,
                                   const fuchsia_sysmem_ImageFormatConstraints& v1);

[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::BufferUsage::Builder> V2CopyFromV1BufferUsage(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem::BufferUsage& v1);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::BufferUsage::Builder> V2CopyFromV1BufferUsage(
    fidl::Allocator* allocator, const fuchsia_sysmem_BufferUsage& v1);

[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::BufferMemoryConstraints::Builder>
V2CopyFromV1BufferMemoryConstraints(fidl::Allocator* allocator,
                                    const llcpp::fuchsia::sysmem::BufferMemoryConstraints& v1);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::BufferMemoryConstraints::Builder>
V2CopyFromV1BufferMemoryConstraints(fidl::Allocator* allocator,
                                    const fuchsia_sysmem_BufferMemoryConstraints& v1);

[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder>
V2CopyFromV1BufferCollectionConstraints(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem::BufferCollectionConstraints* v1,
    const llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers* aux_buffers_v1);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder>
V2CopyFromV1BufferCollectionConstraints(
    fidl::Allocator* allocator, const fuchsia_sysmem_BufferCollectionConstraints* v1,
    const fuchsia_sysmem_BufferCollectionConstraintsAuxBuffers* aux_buffers_v1);

[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::ImageFormat::Builder> V2CopyFromV1ImageFormat(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem::ImageFormat_2& v1);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::ImageFormat::Builder> V2CopyFromV1ImageFormat(
    fidl::Allocator* allocator, const fuchsia_sysmem_ImageFormat_2& v1);

[[nodiscard]] llcpp::fuchsia::sysmem2::BufferMemorySettings::Builder
V2CopyFromV1BufferMemorySettings(fidl::Allocator* allocator,
                                 const llcpp::fuchsia::sysmem::BufferMemorySettings& v1);

[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::SingleBufferSettings::Builder>
V2CopyFromV1SingleBufferSettings(fidl::Allocator* allocator,
                                 const llcpp::fuchsia::sysmem::SingleBufferSettings& v1);

[[nodiscard]] llcpp::fuchsia::sysmem2::VmoBuffer::Builder V2MoveFromV1VmoBuffer(
    fidl::Allocator* allocator, llcpp::fuchsia::sysmem::VmoBuffer&& to_move_v1);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::BufferCollectionInfo::Builder>
V2MoveFromV1BufferCollectionInfo(fidl::Allocator* allocator,
                                 llcpp::fuchsia::sysmem::BufferCollectionInfo_2&& to_move_v1);

///////////////////////
// V1 Copy/Move from V2
///////////////////////

// TODO(dustingreen): Also accept all the v2 Table::Builder(s).

[[nodiscard]] fit::result<
    std::pair<std::optional<llcpp::fuchsia::sysmem::BufferCollectionConstraints>,
              std::optional<llcpp::fuchsia::sysmem::BufferCollectionConstraintsAuxBuffers>>>
V1CopyFromV2BufferCollectionConstraints(
    const llcpp::fuchsia::sysmem2::BufferCollectionConstraints& v2);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem::BufferMemoryConstraints>
V1CopyFromV2BufferMemoryConstraints(const llcpp::fuchsia::sysmem2::BufferMemoryConstraints& v2);
[[nodiscard]] llcpp::fuchsia::sysmem::BufferUsage V1CopyFromV2BufferUsage(
    const llcpp::fuchsia::sysmem2::BufferUsage& v2);
[[nodiscard]] llcpp::fuchsia::sysmem::BufferMemorySettings V1CopyFromV2BufferMemorySettings(
    const llcpp::fuchsia::sysmem2::BufferMemorySettings& v2);
[[nodiscard]] llcpp::fuchsia::sysmem::PixelFormat V1CopyFromV2PixelFormat(
    const llcpp::fuchsia::sysmem2::PixelFormat& v2);
[[nodiscard]] llcpp::fuchsia::sysmem::ColorSpace V1CopyFromV2ColorSpace(
    const llcpp::fuchsia::sysmem2::ColorSpace& v2);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem::ImageFormatConstraints>
V1CopyFromV2ImageFormatConstraints(const llcpp::fuchsia::sysmem2::ImageFormatConstraints& v2);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem::ImageFormat_2> V1CopyFromV2ImageFormat(
    llcpp::fuchsia::sysmem2::ImageFormat& v2);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem::SingleBufferSettings>
V1CopyFromV2SingleBufferSettings(const llcpp::fuchsia::sysmem2::SingleBufferSettings& v2);
[[nodiscard]] llcpp::fuchsia::sysmem::VmoBuffer V1MoveFromV2VmoBuffer(
    llcpp::fuchsia::sysmem2::VmoBuffer&& to_move_v2);
[[nodiscard]] llcpp::fuchsia::sysmem::VmoBuffer V1AuxBuffersMoveFromV2VmoBuffer(
    llcpp::fuchsia::sysmem2::VmoBuffer&& to_move_v2);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem::BufferCollectionInfo_2>
V1MoveFromV2BufferCollectionInfo(llcpp::fuchsia::sysmem2::BufferCollectionInfo&& to_move_v2);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem::BufferCollectionInfo_2>
V1AuxBuffersMoveFromV2BufferCollectionInfo(
    llcpp::fuchsia::sysmem2::BufferCollectionInfo&& to_move_v2);

///////////
// V2 Clone
///////////

[[nodiscard]] llcpp::fuchsia::sysmem2::PixelFormat::Builder V2ClonePixelFormat(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::PixelFormat& src);
[[nodiscard]] llcpp::fuchsia::sysmem2::ColorSpace::Builder V2CloneColorSpace(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::ColorSpace& src);
[[nodiscard]] llcpp::fuchsia::sysmem2::BufferMemorySettings::Builder V2CloneBufferMemorySettings(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::BufferMemorySettings& src);
[[nodiscard]] llcpp::fuchsia::sysmem2::ImageFormatConstraints::Builder
V2CloneImageFormatConstraints(fidl::Allocator* allocator,
                              const llcpp::fuchsia::sysmem2::ImageFormatConstraints& src);
[[nodiscard]] llcpp::fuchsia::sysmem2::SingleBufferSettings::Builder V2CloneSingleBufferSettings(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::SingleBufferSettings& src);
[[nodiscard]] llcpp::fuchsia::sysmem2::SingleBufferSettings::Builder
V2CloneSingleBufferSettingsBuilder(
    fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::SingleBufferSettings::Builder& src);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::VmoBuffer::Builder, zx_status_t>
V2CloneVmoBuffer(fidl::Allocator* allocator, const llcpp::fuchsia::sysmem2::VmoBuffer& src,
                 uint32_t vmo_rights_mask, uint32_t aux_vmo_rights_mask);
[[nodiscard]] fit::result<llcpp::fuchsia::sysmem2::BufferCollectionInfo::Builder, zx_status_t>
V2CloneBufferCollectionInfo(fidl::Allocator* allocator,
                            const llcpp::fuchsia::sysmem2::BufferCollectionInfo& src,
                            uint32_t vmo_rights_mask, uint32_t aux_vmo_rights_mask);

}  // namespace sysmem

#endif  // LIB_SYSMEM_VERSION_SYSMEM_VERSION_H_
