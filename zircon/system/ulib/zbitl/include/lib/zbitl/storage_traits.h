// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_STORAGE_TRAITS_H_
#define LIB_ZBITL_STORAGE_TRAITS_H_

#include <lib/cksum.h>
#include <lib/fitx/result.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string_view>
#include <type_traits>
#include <version>

#if __cpp_lib_span
#include <span>
#endif

namespace zbitl {

using ByteView = std::basic_string_view<std::byte>;

inline ByteView AsBytes(const void* ptr, size_t len) {
  return {reinterpret_cast<const std::byte*>(ptr), len};
}

template <typename T>
inline ByteView AsBytes(const T& payload) {
  static_assert(std::has_unique_object_representations_v<T>);
  return AsBytes(&payload, sizeof(payload));
}

/// The zbitl::StorageTraits template must be specialized for each type used as
/// the Storage type parameter to zbitl::View (see <lib/zbitl/view.h).  The
/// generic template can only be instantiated with `std::tuple<>` as the
/// Storage type.  This is a stub implementation that always fails with an
/// empty error_type.  It also serves to document the API for StorageTraits
/// specializations.
///
template <typename Storage>
struct StorageTraits {
  static_assert(std::is_same_v<std::tuple<>, Storage>, "missing StorageTraits specialization");

  /// This represents an error accessing the storage, either to read a header
  /// or to access a payload.
  struct error_type {};

  /// This represents an item payload (does not include the header).  The
  /// corresponding zbi_header_t.length gives its size.  This type is wholly
  /// opaque to zbitl::View but must be copyable.  It might be something as
  /// simple as the offset into the whole ZBI, or for in-memory Storage types a
  /// std::span pointing to the contents.
  struct payload_type {};

  /// This returns the upper bound on available space where the ZBI is stored.
  /// The container must fit within this maximum.  Storage past the container's
  /// self-encoded size need not be accessible and will never be accessed.
  /// If the actual upper bound is unknown, this can safely return UINT32_MAX.
  static fitx::result<error_type, uint32_t> Capacity(Storage& zbi) {
    return fitx::error<error_type>{};
  }

  /// This fetches the item (or container) header at the given offset.  The
  /// return type can use either plain `zbi_header_t` or it can use
  /// `std::reference_wrapper<const zbi_header_t>`.  The former case is for
  /// remote access to storage, where fetching the header has to copy it.  The
  /// latter case is for in-memory storage, where the header can just be
  /// accessed in place via a direct pointer.
  static fitx::result<error_type, zbi_header_t> Header(Storage& zbi, uint32_t offset) {
    return fitx::error<error_type>{};
  }

  /// This fetches the item payload view object, whatever that means for this
  /// Storage type.  This is not expected to read the contents, just transfer a
  /// pointer or offset around so they can be explicitly read later.
  static fitx::result<error_type, payload_type> Payload(Storage& zbi, uint32_t offset,
                                                        uint32_t length) {
    return fitx::error<error_type>{};
  }

  /// This computes a payload's CRC32 checksum (the header is combined
  /// separately to finalize the zbi_header_t.crc32 value).  This of necessity
  /// entails reading all the contents; it's only used in Checking::kCrc mode.
  static fitx::result<error_type, uint32_t> Crc32(Storage& zbi, uint32_t offset, uint32_t length) {
    return fitx::error<error_type>{};
  }

  // A specialization defines this only if it supports mutation.  It might be
  // called to write whole or partial headers and/or payloads, but it will
  // never be called with an offset and size that would exceed the capacity
  // previously reported by Capacity (above).  It returns success only if it
  // wrote the whole chunk specified.  If it returns an error, any subset of
  // the chunk that failed to write might be corrupted in the image and the
  // container will always revalidate everything.
  static fitx::result<error_type> Write(Storage& zbi, uint32_t offset, ByteView data) {
    return fitx::error<error_type>{};
  }
};

// Specialization for std::basic_string_view<byte-size type> as Storage.  Its
// payload_type is the same type as Storage, just yielding the substring of the
// original whole-ZBI string_view.
template <typename T>
struct StorageTraits<std::basic_string_view<T>> {
  using Storage = std::basic_string_view<T>;

  static_assert(sizeof(T) == sizeof(uint8_t));

  struct error_type {};

  using payload_type = Storage;

  static fitx::result<error_type, uint32_t> Capacity(Storage& zbi) {
    return fitx::ok(static_cast<uint32_t>(
        std::min(zbi.size(),
                 static_cast<typename Storage::size_type>(std::numeric_limits<uint32_t>::max()))));
  }

  static fitx::result<error_type, std::reference_wrapper<const zbi_header_t>> Header(
      Storage& zbi, uint32_t offset) {
    return fitx::ok(std::ref(
        *reinterpret_cast<const zbi_header_t*>(zbi.substr(offset, sizeof(zbi_header_t)).data())));
  }

  static fitx::result<error_type, payload_type> Payload(Storage& zbi, uint32_t offset,
                                                        uint32_t length) {
    auto payload = zbi.substr(offset, length);
    ZX_DEBUG_ASSERT(payload.size() == length);
    return fitx::ok(std::move(payload));
  }

  static fitx::result<error_type, uint32_t> Crc32(Storage& zbi, uint32_t offset, uint32_t length) {
    auto payload = zbi.substr(offset, length);
    ZX_DEBUG_ASSERT(payload.size() == length);
    return fitx::ok(crc32(0, reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
  }
};

// Specialization for std::span<any type> as Storage.  Its payload_type is the
// same type as Storage, just yielding the subspan of the original whole-ZBI
// span.
#if __cpp_lib_span
inline std::span<std::byte> AsWritableBytes(void* ptr, size_t len) {
  return {reinterpret_cast<std::byte*>(ptr), len};
}

template <typename T, size_t Extent>
struct StorageTraits<std::span<T, Extent>> {
  using Storage = std::span<T, Extent>;

  struct error_type {};

  using payload_type = Storage;

  static fitx::result<error_type, uint32_t> Capacity(Storage& zbi) {
    return fitx::ok(static_cast<uint32_t>(
        std::min(zbi.size_bytes(), static_cast<size_t>(std::numeric_limits<uint32_t>::max()))));
  }

  static fitx::result<error_type, std::reference_wrapper<const zbi_header_t>> Header(
      Storage& zbi, uint32_t offset) {
    return fitx::ok(std::ref(
        *reinterpret_cast<const zbi_header_t*>(zbi.subspan(offset, sizeof(zbi_header_t)).data())));
  }

  static fitx::result<error_type, payload_type> Payload(Storage& zbi, uint32_t offset,
                                                        uint32_t length) {
    auto payload = [&]() {
      if constexpr (std::is_const_v<T>) {
        return std::as_bytes(zbi).subspan(offset, length);
      } else {
        return std::as_writable_bytes(zbi).subspan(offset, length);
      }
    }();
    ZX_DEBUG_ASSERT(payload.size() == length);
    ZX_ASSERT_MSG(payload.size() % sizeof(T) == 0,
                  "payload size not a multiple of storage span element_type size");
    return fitx::ok(payload_type{reinterpret_cast<T*>(payload.data()), payload.size() / sizeof(T)});
  }

  static fitx::result<error_type, uint32_t> Crc32(Storage& zbi, uint32_t offset, uint32_t length) {
    auto payload = std::as_bytes(zbi).subspan(offset, length);
    ZX_DEBUG_ASSERT(payload.size() == length);
    return fitx::ok(crc32(0, reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
  }

  template <typename S = T, typename = std::enable_if_t<!std::is_const_v<S>>>
  static fitx::result<error_type> Write(Storage& zbi, uint32_t offset, ByteView data) {
    // The caller is supposed to maintain these invariants.
    ZX_DEBUG_ASSERT(offset <= zbi.size_bytes());
    ZX_DEBUG_ASSERT(data.size() <= zbi.size_bytes() - offset);
    memcpy(&std::as_writable_bytes(zbi)[offset], data.data(), data.size());
    return fitx::ok();
  }
};
#endif  // __cpp_lib_span

}  // namespace zbitl

#endif  // LIB_ZBITL_STORAGE_TRAITS_H_
