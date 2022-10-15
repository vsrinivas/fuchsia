// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fidl/fuchsia.sysmem2/cpp/wire.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/sysmem-version/sysmem-version.h>

#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <vector>

#include <fbl/array.h>
#include <zxtest/zxtest.h>

namespace v1 = fuchsia_sysmem;
namespace v2 = fuchsia_sysmem2;

namespace {

constexpr uint32_t kRunCount = 300;

template <typename FidlType>
class LinearSnap {
  static_assert(fidl::IsFidlType<FidlType>::value);

 public:
  static constexpr size_t kMaxDataSize = 64 * 1024;
  static constexpr size_t kMaxHandleCount = 1024;

  static std::unique_ptr<LinearSnap> MoveFrom(FidlType&& to_move_in) {
    return std::unique_ptr<LinearSnap<FidlType>>(new LinearSnap(std::move(to_move_in)));
  }

  // This value is similar to an in-place decode received LLCPP message, in that it can be moved out
  // syntatically, but really any ObjectView<>(s) are non-owned, so callers should take care to
  // not use the returned logical FidlType& (even if syntatically moved out) beyond ~LinearSnap.
  FidlType& value() { return *decoded_.value().PrimaryObject(); }

  const fidl::BytePart snap_bytes() const {
    return fidl::BytePart(const_cast<uint8_t*>(snap_data_), snap_data_size_, snap_data_size_);
  }

  const fidl::HandlePart snap_handles() const {
    return fidl::HandlePart(const_cast<zx_handle_t*>(snap_handles_), snap_handles_count_,
                            snap_handles_count_);
  }

  const fidl_channel_handle_metadata_t* snap_handle_metadata() const {
    return snap_handle_metadata_;
  }

 private:
  explicit LinearSnap(FidlType&& to_move_in) {
    alignas(FIDL_ALIGNMENT) FidlType aligned = std::move(to_move_in);
    // TODO(fxbug.dev/45252): Use FIDL at rest.
    fidl::unstable::UnownedEncodedMessage<FidlType> encoded(fidl::internal::WireFormatVersion::kV2,
                                                            linear_data_, kMaxDataSize, &aligned);
    ZX_ASSERT(encoded.ok());
    fidl::OutgoingMessage& outgoing_message = encoded.GetOutgoingMessage();
    fidl::OutgoingMessage::CopiedBytes outgoing_message_bytes_(outgoing_message.CopyBytes());
    ZX_ASSERT(outgoing_message_bytes_.size() <= sizeof(snap_data_));
    memcpy(snap_data_, outgoing_message_bytes_.data(), outgoing_message_bytes_.size());
    snap_data_size_ = static_cast<uint32_t>(outgoing_message_bytes_.size());
    ZX_ASSERT(outgoing_message.handle_actual() * sizeof(zx_handle_t) <= sizeof(snap_handles_));
    memcpy(snap_handles_, outgoing_message.handles(),
           outgoing_message.handle_actual() * sizeof(zx_handle_t));
    ZX_ASSERT(outgoing_message.handle_actual() * sizeof(fidl_channel_handle_metadata_t) <=
              sizeof(snap_handle_metadata_));
    memcpy(snap_handle_metadata_,
           outgoing_message.handle_metadata<fidl::internal::ChannelTransport>(),
           outgoing_message.handle_actual() * sizeof(fidl_channel_handle_metadata_t));
    snap_handles_count_ = outgoing_message.handle_actual();
    outgoing_to_incoming_result_.emplace(encoded.GetOutgoingMessage());
    ZX_ASSERT(outgoing_to_incoming_result_.value().ok());
    // TODO(fxbug.dev/45252): Use FIDL at rest.
    decoded_.emplace(fidl::internal::WireFormatVersion::kV2,
                     std::move(outgoing_to_incoming_result_.value().incoming_message()));
    ZX_ASSERT(decoded_.value().ok());
  }

  // During MoveFrom, used for linearizing, encoding.
  alignas(FIDL_ALIGNMENT) uint8_t linear_data_[kMaxDataSize] = {};

  alignas(FIDL_ALIGNMENT) uint8_t snap_data_[kMaxDataSize] = {};
  zx_handle_t snap_handles_[kMaxHandleCount] = {};
  fidl_channel_handle_metadata_t snap_handle_metadata_[kMaxHandleCount] = {};
  uint32_t snap_data_size_ = {};
  uint32_t snap_handles_count_ = {};

  std::optional<fidl::OutgoingToIncomingMessage> outgoing_to_incoming_result_;
  std::optional<fidl::unstable::DecodedMessage<FidlType>> decoded_;
};

template <typename FidlType>
std::unique_ptr<LinearSnap<FidlType>> SnapMoveFrom(FidlType&& to_move_in) {
  return LinearSnap<FidlType>::MoveFrom(std::move(to_move_in));
}

template <typename FidlType>
bool IsEqualImpl(const LinearSnap<FidlType>& a, const LinearSnap<FidlType>& b, bool by_koid) {
  if (a.snap_bytes().actual() != b.snap_bytes().actual()) {
    return false;
  }
  if (0 != memcmp(a.snap_bytes().data(), b.snap_bytes().data(), a.snap_bytes().actual())) {
    return false;
  }
  if (a.snap_handles().actual() != b.snap_handles().actual()) {
    return false;
  }
  if (!by_koid) {
    if (0 != memcmp(a.snap_handles().data(), b.snap_handles().data(),
                    a.snap_handles().actual() * sizeof(zx_handle_t))) {
      return false;
    }
    if (0 != memcmp(a.snap_handle_metadata(), b.snap_handle_metadata(),
                    a.snap_handles().actual() * sizeof(fidl_channel_handle_metadata_t))) {
      return false;
    }
  } else {
    for (uint32_t i = 0; i < a.snap_handles().actual(); ++i) {
      zx_info_handle_basic_t a_info{};
      zx_info_handle_basic_t b_info{};
      ZX_ASSERT(ZX_OK == zx_object_get_info(a.snap_handles().data()[i], ZX_INFO_HANDLE_BASIC,
                                            &a_info, sizeof(a_info), nullptr, nullptr));
      ZX_ASSERT(ZX_OK == zx_object_get_info(b.snap_handles().data()[i], ZX_INFO_HANDLE_BASIC,
                                            &b_info, sizeof(a_info), nullptr, nullptr));
      if (a_info.koid != b_info.koid) {
        return false;
      }
    }
  }
  return true;
}

template <typename FidlType>
bool IsEqual(const LinearSnap<FidlType>& a, const LinearSnap<FidlType>& b) {
  return IsEqualImpl(a, b, false);
}

template <typename FidlType>
bool IsEqualByKoid(const LinearSnap<FidlType>& a, const LinearSnap<FidlType>& b) {
  return IsEqualImpl(a, b, true);
}

std::random_device random_device;
std::mt19937 prng(random_device());

template <typename T, std::enable_if_t<!std::is_same_v<T, bool>, bool> = true>
void random(T* field) {
  // If one of these complains, consider adding a random<>() specialization below.
  static_assert(std::is_integral<T>::value);
  static_assert(!std::is_enum<T>::value);

  static std::uniform_int_distribution distribution(std::numeric_limits<T>::min(),
                                                    std::numeric_limits<T>::max());
  while (true) {
    *field = distribution(prng);
    // Avoid picking 0, because that'd cause fields to be set or not-set inconsistently, which would
    // likely cause occasional test flakes.
    if (*field == 0) {
      continue;
    }
    return;
  }
}

void random(bool* field) {
  // Always return true because zero/false will never be set in the general `random` implementation.
  *field = true;
}

template <>
void random<v1::wire::HeapType>(v1::wire::HeapType* field) {
  // TODO(fxbug.dev/53067): Use generated-code array of valid values instead, when/if available.
  static constexpr uint64_t valid[] = {
      /*SYSTEM_RAM =*/0u,
      /*AMLOGIC_SECURE =*/1152921504606912512u,
      /*AMLOGIC_SECURE_VDEC =*/1152921504606912513u,
      /*GOLDFISH_DEVICE_LOCAL =*/1152921504606978048u,
      /*GOLDFISH_HOST_VISIBLE =*/1152921504606978049u,
      /*FRAMEBUFFER =*/1152921504607043585u,
  };
  uint32_t index;
  random(&index);
  index %= std::size(valid);
  *field = static_cast<v1::wire::HeapType>(valid[index]);
}

template <>
void random<v1::wire::PixelFormatType>(v1::wire::PixelFormatType* field) {
  // TODO(fxbug.dev/53067): Use generated-code array of valid values instead, when/if available.
  static constexpr uint32_t valid[] = {
      /*INVALID =*/0u,
      /*R8G8B8A8 =*/1u,
      /*BGRA32 =*/101u,
      /*I420 =*/102u,
      /*M420 =*/103u,
      /*NV12 =*/104u,
      /*YUY2 =*/105u,
      /*MJPEG =*/106u,
      /*YV12 =*/107u,
      /*BGR24 =*/108u,
      /*RGB565 =*/109u,
      /*RGB332 =*/110u,
      /*RGB2220 =*/111u,
      /*L8 =*/112u,
  };
  uint32_t index;
  random(&index);
  index %= std::size(valid);
  *field = static_cast<v1::wire::PixelFormatType>(valid[index]);
}

template <>
void random<v1::wire::ColorSpaceType>(v1::wire::ColorSpaceType* field) {
  // TODO(fxbug.dev/53067): Use generated-code array of valid values instead, when/if available.
  static constexpr uint32_t valid[] = {
      /*INVALID =*/0u,
      /*SRGB =*/1u,
      /*REC601_NTSC =*/2u,
      /*REC601_NTSC_FULL_RANGE =*/3u,
      /*REC601_PAL =*/4u,
      /*REC601_PAL_FULL_RANGE =*/5u,
      /*REC709 =*/6u,
      /*REC2020 =*/7u,
      /*REC2100 =*/8u,
  };
  uint32_t index;
  random(&index);
  index %= std::size(valid);
  *field = static_cast<v1::wire::ColorSpaceType>(valid[index]);
}

template <>
void random<v1::wire::CoherencyDomain>(v1::wire::CoherencyDomain* field) {
  // TODO(fxbug.dev/53067): Use generated-code array of valid values instead, when/if available.
  static constexpr uint32_t valid[] = {
      /*CPU =*/0u,
      /*RAM =*/1u,
      /*INACCESSIBLE =*/2u,
  };
  uint32_t index;
  random(&index);
  index %= std::size(valid);
  *field = static_cast<v1::wire::CoherencyDomain>(valid[index]);
}

v1::wire::BufferUsage V1RandomBufferUsage() {
  v1::wire::BufferUsage r{};
  random(&r.none);
  random(&r.cpu);
  random(&r.vulkan);
  random(&r.display);
  random(&r.video);
  return r;
}

v1::wire::BufferMemoryConstraints V1RandomBufferMemoryConstraints() {
  v1::wire::BufferMemoryConstraints r{};
  random(&r.min_size_bytes);
  random(&r.max_size_bytes);
  random(&r.physically_contiguous_required);
  random(&r.secure_required);
  random(&r.ram_domain_supported);
  random(&r.cpu_domain_supported);
  random(&r.inaccessible_domain_supported);
  random(&r.heap_permitted_count);
  r.heap_permitted_count %= fuchsia_sysmem::wire::kMaxCountBufferMemoryConstraintsHeapPermitted;
  for (uint32_t i = 0; i < r.heap_permitted_count; ++i) {
    random(&r.heap_permitted[i]);
  }
  return r;
}

v1::wire::PixelFormat V1RandomPixelFormat() {
  v1::wire::PixelFormat r{};
  random(&r.type);
  random(&r.has_format_modifier);
  if (r.has_format_modifier) {
    random(&r.format_modifier.value);
  }
  return r;
}

v1::wire::ColorSpace V1RandomColorSpace() {
  v1::wire::ColorSpace r{};
  random(&r.type);
  return r;
}

v1::wire::ImageFormatConstraints V1RandomImageFormatConstraints() {
  v1::wire::ImageFormatConstraints r{};
  r.pixel_format = V1RandomPixelFormat();
  random(&r.color_spaces_count);
  r.color_spaces_count %= fuchsia_sysmem::wire::kMaxCountImageFormatConstraintsColorSpaces;
  for (uint32_t i = 0; i < r.color_spaces_count; ++i) {
    r.color_space[i] = V1RandomColorSpace();
  }
  random(&r.min_coded_width);
  random(&r.max_coded_width);
  random(&r.min_coded_height);
  random(&r.max_coded_height);
  random(&r.min_bytes_per_row);
  random(&r.max_bytes_per_row);
  random(&r.max_coded_width_times_coded_height);
  // Both 0 and 1 are accepted by conversion code - but only 1 allows the value to be equal after
  // round trip, so just use that.
  r.layers = 1;
  random(&r.coded_width_divisor);
  random(&r.coded_height_divisor);
  random(&r.bytes_per_row_divisor);
  random(&r.start_offset_divisor);
  random(&r.display_width_divisor);
  random(&r.display_height_divisor);
  random(&r.required_min_coded_width);
  random(&r.required_max_coded_width);
  random(&r.required_min_coded_height);
  random(&r.required_max_coded_height);
  random(&r.required_min_bytes_per_row);
  random(&r.required_max_bytes_per_row);
  return r;
}

v1::wire::ImageFormat2 V1RandomImageFormat() {
  v1::wire::ImageFormat2 r{};
  r.pixel_format = V1RandomPixelFormat();
  random(&r.coded_width);
  random(&r.coded_height);
  random(&r.bytes_per_row);
  random(&r.display_width);
  random(&r.display_height);
  // By design, the only value that'll round-trip is 1, so just use 1 here.
  r.layers = 1;
  r.color_space = V1RandomColorSpace();
  random(&r.has_pixel_aspect_ratio);
  if (r.has_pixel_aspect_ratio) {
    random(&r.pixel_aspect_ratio_width);
    random(&r.pixel_aspect_ratio_height);
  }
  return r;
}

v1::wire::BufferMemorySettings V1RandomBufferMemorySettings() {
  v1::wire::BufferMemorySettings r{};
  random(&r.size_bytes);
  random(&r.is_physically_contiguous);
  random(&r.is_secure);
  random(&r.coherency_domain);
  random(&r.heap);
  return r;
}

v1::wire::SingleBufferSettings V1RandomSingleBufferSettings() {
  v1::wire::SingleBufferSettings r{};
  r.buffer_settings = V1RandomBufferMemorySettings();
  random(&r.has_image_format_constraints);
  if (r.has_image_format_constraints) {
    r.image_format_constraints = V1RandomImageFormatConstraints();
  }
  return r;
}

v1::wire::VmoBuffer V1RandomVmoBuffer() {
  v1::wire::VmoBuffer r{};
  // Arbitrary is good enough - we don't need truly "random" for this.
  zx::vmo arbitrary_vmo;
  ZX_ASSERT(ZX_OK == zx::vmo::create(ZX_PAGE_SIZE, 0, &arbitrary_vmo));
  r.vmo = std::move(arbitrary_vmo);
  random(&r.vmo_usable_start);
  return r;
}

v1::wire::BufferCollectionInfo2 V1RandomBufferCollectionInfo() {
  v1::wire::BufferCollectionInfo2 r{};
  random(&r.buffer_count);
  r.buffer_count %= v1::wire::kMaxCountBufferCollectionInfoBuffers;
  r.settings = V1RandomSingleBufferSettings();
  for (uint32_t i = 0; i < r.buffer_count; ++i) {
    r.buffers[i] = V1RandomVmoBuffer();
  }
  return r;
}

v1::wire::BufferCollectionConstraints V1RandomBufferCollectionConstraints() {
  v1::wire::BufferCollectionConstraints r{};
  r.usage = V1RandomBufferUsage();
  random(&r.min_buffer_count_for_camping);
  random(&r.min_buffer_count_for_dedicated_slack);
  random(&r.min_buffer_count_for_shared_slack);
  random(&r.min_buffer_count);
  random(&r.max_buffer_count);
  random(&r.has_buffer_memory_constraints);
  if (r.has_buffer_memory_constraints) {
    r.buffer_memory_constraints = V1RandomBufferMemoryConstraints();
  }
  random(&r.image_format_constraints_count);
  r.image_format_constraints_count %=
      fuchsia_sysmem::wire::kMaxCountBufferCollectionConstraintsImageFormatConstraints;
  for (uint32_t i = 0; i < r.image_format_constraints_count; ++i) {
    r.image_format_constraints[i] = V1RandomImageFormatConstraints();
  }
  return r;
}

v1::wire::BufferCollectionConstraintsAuxBuffers V1RandomBufferCollectionConstraintsAuxBuffers() {
  v1::wire::BufferCollectionConstraintsAuxBuffers r{};
  random(&r.need_clear_aux_buffers_for_secure);
  random(&r.allow_clear_aux_buffers_for_secure);
  return r;
}

}  // namespace

TEST(SysmemVersion, EncodedEquality) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    auto v1_buffer_usage = V1RandomBufferUsage();
    auto snap_1 = SnapMoveFrom(std::move(v1_buffer_usage));
    auto snap_2 = SnapMoveFrom(std::move(snap_1->value()));
    EXPECT_TRUE(IsEqual(*snap_1, *snap_2));
  }
}

TEST(SysmemVersion, BufferUsage) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    auto v1_1 = V1RandomBufferUsage();
    auto snap_1 = SnapMoveFrom(std::move(v1_1));
    auto v2 = sysmem::V2CopyFromV1BufferUsage(allocator, snap_1->value()).take_value();
    auto v1_2 = sysmem::V1CopyFromV2BufferUsage(v2);
    auto snap_2 = SnapMoveFrom(std::move(v1_2));
    EXPECT_TRUE(IsEqual(*snap_1, *snap_2));
  }
}

TEST(SysmemVersion, PixelFormat) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    auto v1_1 = V1RandomPixelFormat();
    auto snap_1 = SnapMoveFrom(std::move(v1_1));
    auto v2_1 = sysmem::V2CopyFromV1PixelFormat(allocator, snap_1->value());
    auto v2_2 = sysmem::V2ClonePixelFormat(allocator, v2_1);
    auto v1_2 = sysmem::V1CopyFromV2PixelFormat(v2_2);
    auto snap_2 = SnapMoveFrom(std::move(v1_2));
    EXPECT_TRUE(IsEqual(*snap_1, *snap_2));
  }
}

TEST(SysmemVersion, ColorSpace) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    auto v1_1 = V1RandomColorSpace();
    auto snap_1 = SnapMoveFrom(std::move(v1_1));
    auto v2_1 = sysmem::V2CopyFromV1ColorSpace(allocator, snap_1->value());
    auto v2_2 = sysmem::V2CloneColorSpace(allocator, v2_1);
    auto v1_2 = sysmem::V1CopyFromV2ColorSpace(v2_2);
    auto snap_2 = SnapMoveFrom(std::move(v1_2));
    EXPECT_TRUE(IsEqual(*snap_1, *snap_2));
  }
}

TEST(SysmemVersion, ImageFormatConstraints) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    auto v1_1 = V1RandomImageFormatConstraints();
    auto snap_1 = SnapMoveFrom(std::move(v1_1));
    auto v2_1 = sysmem::V2CopyFromV1ImageFormatConstraints(allocator, snap_1->value()).take_value();
    auto v2_2 = sysmem::V2CloneImageFormatConstraints(allocator, v2_1);
    auto v1_2_result = sysmem::V1CopyFromV2ImageFormatConstraints(v2_2);
    EXPECT_TRUE(v1_2_result.is_ok());
    auto v1_2 = v1_2_result.take_value();
    auto snap_2 = SnapMoveFrom(std::move(v1_2));
    EXPECT_TRUE(IsEqual(*snap_1, *snap_2));
  }
}

TEST(SysmemVersion, BufferMemoryConstraints) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    auto v1_1 = V1RandomBufferMemoryConstraints();
    auto snap_1 = SnapMoveFrom(std::move(v1_1));
    auto v2 = sysmem::V2CopyFromV1BufferMemoryConstraints(allocator, snap_1->value()).take_value();
    auto v1_2_result = sysmem::V1CopyFromV2BufferMemoryConstraints(v2);
    EXPECT_TRUE(v1_2_result.is_ok());
    auto v1_2 = v1_2_result.take_value();
    auto snap_2 = SnapMoveFrom(std::move(v1_2));
    EXPECT_TRUE(IsEqual(*snap_1, *snap_2));
  }
}

TEST(SysmemVersion, ImageFormat) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    auto v1_1 = V1RandomImageFormat();
    auto snap_1 = SnapMoveFrom(std::move(v1_1));
    auto v2 = sysmem::V2CopyFromV1ImageFormat(allocator, snap_1->value()).take_value();
    auto v1_2_result = sysmem::V1CopyFromV2ImageFormat(v2);
    EXPECT_TRUE(v1_2_result.is_ok());
    auto v1_2 = v1_2_result.take_value();
    auto snap_2 = SnapMoveFrom(std::move(v1_2));
    EXPECT_TRUE(IsEqual(*snap_1, *snap_2));
  }
}

TEST(SysmemVersion, BufferMemorySettings) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    auto v1_1 = V1RandomBufferMemorySettings();
    auto snap_1 = SnapMoveFrom(std::move(v1_1));
    auto v2_1 = sysmem::V2CopyFromV1BufferMemorySettings(allocator, snap_1->value());
    auto v2_2 = sysmem::V2CloneBufferMemorySettings(allocator, v2_1);
    auto v1_2 = sysmem::V1CopyFromV2BufferMemorySettings(v2_2);
    auto snap_2 = SnapMoveFrom(std::move(v1_2));
    EXPECT_TRUE(IsEqual(*snap_1, *snap_2));
  }
}

TEST(SysmemVersion, SingleBufferSettings) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    auto v1_1 = V1RandomSingleBufferSettings();
    auto snap_1 = SnapMoveFrom(std::move(v1_1));
    auto v2_1_result = sysmem::V2CopyFromV1SingleBufferSettings(allocator, snap_1->value());
    EXPECT_TRUE(v2_1_result.is_ok());
    auto v2_1 = v2_1_result.take_value();
    auto v2_2 = sysmem::V2CloneSingleBufferSettings(allocator, v2_1);
    auto v1_2_result = sysmem::V1CopyFromV2SingleBufferSettings(v2_2);
    EXPECT_TRUE(v1_2_result.is_ok());
    auto v1_2 = v1_2_result.take_value();
    auto snap_2 = SnapMoveFrom(std::move(v1_2));
    EXPECT_TRUE(IsEqual(*snap_1, *snap_2));

    auto v2_builder_result = sysmem::V2CopyFromV1SingleBufferSettings(allocator, snap_1->value());
    EXPECT_TRUE(v2_builder_result.is_ok());
    auto v2_3 = sysmem::V2CloneSingleBufferSettings(allocator, v2_builder_result.value());
    auto v1_3_result = sysmem::V1CopyFromV2SingleBufferSettings(v2_3);
    EXPECT_TRUE(v1_3_result.is_ok());
    auto v1_3 = v1_3_result.take_value();
    auto snap_3 = SnapMoveFrom(std::move(v1_3));
    EXPECT_TRUE(IsEqual(*snap_1, *snap_3));
  }
}

TEST(SysmemVersion, VmoBuffer) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    auto v1_1 = V1RandomVmoBuffer();
    auto snap_1 = SnapMoveFrom(std::move(v1_1));
    auto v2_1 = sysmem::V2MoveFromV1VmoBuffer(allocator, std::move(snap_1->value()));
    auto v2_2_result =
        sysmem::V2CloneVmoBuffer(allocator, v2_1, std::numeric_limits<uint32_t>::max(),
                                 std::numeric_limits<uint32_t>::max());
    EXPECT_TRUE(v2_2_result.is_ok());
    auto v2_2 = v2_2_result.take_value();
    auto v1_2 = sysmem::V1MoveFromV2VmoBuffer(std::move(v2_1));
    auto snap_2 = SnapMoveFrom(std::move(v1_2));
    EXPECT_TRUE(IsEqual(*snap_1, *snap_2));
    auto v1_3 = sysmem::V1MoveFromV2VmoBuffer(std::move(v2_2));
    auto snap_3 = SnapMoveFrom(std::move(v1_3));
    EXPECT_FALSE(IsEqual(*snap_1, *snap_3));
    EXPECT_TRUE(IsEqualByKoid(*snap_1, *snap_3));
    EXPECT_TRUE(IsEqualByKoid(*snap_2, *snap_3));
  }
}

TEST(SysmemVersion, BufferCollectionInfo) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    auto v1_1 = V1RandomBufferCollectionInfo();
    auto snap_1 = SnapMoveFrom(std::move(v1_1));
    auto v2_1_result =
        sysmem::V2MoveFromV1BufferCollectionInfo(allocator, std::move(snap_1->value()));
    EXPECT_TRUE(v2_1_result.is_ok());
    auto v2_1 = v2_1_result.take_value();
    auto v2_2_result =
        sysmem::V2CloneBufferCollectionInfo(allocator, v2_1, std::numeric_limits<uint32_t>::max(),
                                            std::numeric_limits<uint32_t>::max());
    EXPECT_TRUE(v2_2_result.is_ok());
    auto v2_2 = v2_2_result.take_value();
    auto v1_2_result = sysmem::V1MoveFromV2BufferCollectionInfo(std::move(v2_1));
    EXPECT_TRUE(v1_2_result.is_ok());
    auto v1_2 = v1_2_result.take_value();
    auto snap_2 = SnapMoveFrom(std::move(v1_2));
    EXPECT_TRUE(IsEqual(*snap_1, *snap_2));
    auto v1_3_result = sysmem::V1MoveFromV2BufferCollectionInfo(std::move(v2_2));
    EXPECT_TRUE(v1_3_result.is_ok());
    auto v1_3 = v1_3_result.take_value();
    auto snap_3 = SnapMoveFrom(std::move(v1_3));
    EXPECT_TRUE(!IsEqual(*snap_1, *snap_3) || snap_3->value().buffer_count == 0);
    EXPECT_TRUE(IsEqualByKoid(*snap_1, *snap_3));
    EXPECT_TRUE(IsEqualByKoid(*snap_2, *snap_3));
  }
}

TEST(SysmemVersion, BufferCollectionConstraints) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    auto v1_1 = V1RandomBufferCollectionConstraints();
    auto v1_aux_1 = V1RandomBufferCollectionConstraintsAuxBuffers();
    auto snap_1 = SnapMoveFrom(std::move(v1_1));
    auto snap_aux_1 = SnapMoveFrom(std::move(v1_aux_1));
    bool has_main;
    random(&has_main);
    bool has_aux = false;
    if (has_main) {
      random(&has_aux);
    }
    v1::wire::BufferCollectionConstraints* maybe_main = has_main ? &snap_1->value() : nullptr;
    v1::wire::BufferCollectionConstraintsAuxBuffers* maybe_aux =
        has_aux ? &snap_aux_1->value() : nullptr;
    auto v2 = sysmem::V2CopyFromV1BufferCollectionConstraints(allocator, maybe_main, maybe_aux)
                  .take_value();
    auto v2_clone = sysmem::V2CloneBufferCollectionConstraints(allocator, v2);
    auto v1_2_result = sysmem::V1CopyFromV2BufferCollectionConstraints(v2);
    EXPECT_TRUE(v1_2_result.is_ok());
    auto v1_2_pair = v1_2_result.take_value();

    auto v2_snap = SnapMoveFrom(std::move(v2));
    auto v2_clone_snap = SnapMoveFrom(std::move(v2_clone));
    EXPECT_TRUE(IsEqual(*v2_snap, *v2_clone_snap));

    if (has_main) {
      auto v1_2_optional = std::move(v1_2_pair.first);
      EXPECT_TRUE(!!v1_2_optional);
      auto v1_2 = std::move(v1_2_optional.value());
      auto snap_2 = SnapMoveFrom(std::move(v1_2));
      EXPECT_TRUE(IsEqual(*snap_1, *snap_2));
    } else {
      auto v1_2 = v1::wire::BufferCollectionConstraints{};
      auto snap_2 = SnapMoveFrom(std::move(v1_2));
      EXPECT_TRUE(IsEqual(*snap_1, *snap_2));
    }

    auto v1_aux_2_optional = std::move(v1_2_pair.second);
    EXPECT_EQ(has_aux, !!v1_aux_2_optional);
    if (v1_aux_2_optional) {
      auto v1_aux_2 = std::move(v1_aux_2_optional.value());
      auto snap_aux_2 = SnapMoveFrom(std::move(v1_aux_2));
      EXPECT_TRUE(IsEqual(*snap_aux_1, *snap_aux_2));
    }

    auto v2_2 = sysmem::V2CloneBufferCollectionConstraints(allocator, v2);
    auto snap_v2 = SnapMoveFrom(std::move(v2));
    auto snap_v2_2 = SnapMoveFrom(std::move(v2_2));
    EXPECT_TRUE(IsEqual(*snap_v2, *snap_v2_2));
  }
}

TEST(SysmemVersion, CoherencyDomainSupport) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    bool cpu_supported;
    bool ram_supported;
    bool inaccessible_supported;
    random(&cpu_supported);
    random(&ram_supported);
    random(&inaccessible_supported);

    v2::wire::CoherencyDomainSupport v2_1(allocator);
    v2_1.set_cpu_supported(cpu_supported);
    v2_1.set_ram_supported(ram_supported);
    v2_1.set_inaccessible_supported(inaccessible_supported);

    v2::wire::CoherencyDomainSupport v2_2 = sysmem::V2CloneCoherencyDomainSuppoort(allocator, v2_1);
    EXPECT_TRUE(v2_2.has_cpu_supported());
    EXPECT_TRUE(v2_2.has_ram_supported());
    EXPECT_TRUE(v2_2.has_inaccessible_supported());

    EXPECT_EQ(v2_2.cpu_supported(), v2_1.cpu_supported());
    EXPECT_EQ(v2_2.ram_supported(), v2_1.ram_supported());
    EXPECT_EQ(v2_2.inaccessible_supported(), v2_1.inaccessible_supported());
  }
}

TEST(SysmemVersion, HeapProperties) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    fidl::Arena allocator;
    bool cpu_supported;
    bool ram_supported;
    bool inaccessible_supported;
    bool need_clear;
    random(&cpu_supported);
    random(&ram_supported);
    random(&inaccessible_supported);
    random(&need_clear);

    v2::wire::HeapProperties v2_1(allocator);
    v2_1.set_need_clear(need_clear);
    {
      v2::wire::CoherencyDomainSupport coherency_domain_support(allocator);
      coherency_domain_support.set_cpu_supported(cpu_supported);
      coherency_domain_support.set_ram_supported(ram_supported);
      coherency_domain_support.set_inaccessible_supported(inaccessible_supported);
      v2_1.set_coherency_domain_support(allocator, std::move(coherency_domain_support));
    }

    v2::wire::HeapProperties v2_2 = sysmem::V2CloneHeapProperties(allocator, v2_1);
    EXPECT_TRUE(v2_2.has_coherency_domain_support());
    EXPECT_TRUE(v2_2.coherency_domain_support().has_cpu_supported());
    EXPECT_TRUE(v2_2.coherency_domain_support().has_ram_supported());
    EXPECT_TRUE(v2_2.coherency_domain_support().has_inaccessible_supported());
    EXPECT_TRUE(v2_2.has_need_clear());

    EXPECT_EQ(v2_2.coherency_domain_support().cpu_supported(),
              v2_1.coherency_domain_support().cpu_supported());
    EXPECT_EQ(v2_2.coherency_domain_support().ram_supported(),
              v2_1.coherency_domain_support().ram_supported());
    EXPECT_EQ(v2_2.coherency_domain_support().inaccessible_supported(),
              v2_1.coherency_domain_support().inaccessible_supported());
    EXPECT_EQ(v2_2.need_clear(), v2_1.need_clear());
  }
}

TEST(SysmemVersion, HeapType) {
  for (uint32_t run = 0; run < kRunCount; ++run) {
    uint64_t heap_type_v2;
    random(&heap_type_v2);
    v2::HeapType v2_1 = static_cast<v2::HeapType>(heap_type_v2);
    v1::HeapType v1_1 = sysmem::V1CopyFromV2HeapType(v2_1);
    uint64_t heap_type_v1 = static_cast<uint64_t>(v1_1);
    EXPECT_EQ(heap_type_v1, heap_type_v2);
    v2::HeapType v2_2 = sysmem::V2CopyFromV1HeapType(v1_1);
    EXPECT_EQ(v2_1, v2_2);
    uint64_t heap_type_v2_2 = static_cast<uint64_t>(v2_2);
    EXPECT_EQ(heap_type_v2_2, heap_type_v2);
  }
}
