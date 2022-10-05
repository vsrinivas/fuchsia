// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_VMO_H_
#define SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_VMO_H_

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/status.h>

#include <optional>
#include <utility>

#include "storage-traits.h"

namespace zbitl {

// zbitl::MapUnownedVmo is handled as a storage type that works like
// zx::unowned_vmo.  The difference is that payload access (for CRC32 et al)
// works by mapping a range of the VMO containing the payload into the process
// using the provided VMAR, rather than by zx::vmo::read into an allocated
// buffer of fixed size.  Note that access to the headers is still done via
// zx::vmo::read (and zx::vmo::write for mutation).  You can also map the
// entire image into memory at once and then use an in-memory storage type like
// zbitl::ByteView instead.
class MapUnownedVmo {
 public:
  explicit MapUnownedVmo(zx::unowned_vmo vmo, bool writable = false,
                         zx::unowned_vmar vmar = zx::vmar::root_self())
      : vmo_(std::move(vmo)), vmar_(std::move(vmar)), writable_(writable) {}

  MapUnownedVmo() = default;
  MapUnownedVmo(MapUnownedVmo&&) = default;
  MapUnownedVmo& operator=(MapUnownedVmo&&) = default;

  // The default copy constructor and copy assignment operator are implicitly
  // deleted because the zx::unowned_* types are not copy-constructible, but
  // they are safe to copy here since it's clear from this type's purpose that
  // it always holds non-owning references.

  MapUnownedVmo(const MapUnownedVmo& other)
      : vmo_{zx::unowned_vmo{other.vmo_}},
        vmar_{zx::unowned_vmar{other.vmar_}},
        writable_(other.writable_) {}

  MapUnownedVmo& operator=(const MapUnownedVmo& other) {
    vmo_ = zx::unowned_vmo{other.vmo_};
    vmar_ = zx::unowned_vmar{other.vmar_};
    writable_ = other.writable_;
    return *this;
  }

  ~MapUnownedVmo();

  const zx::vmo& vmo() const { return *vmo_; }
  const zx::vmar& vmar() const { return *vmar_; }

 private:
  friend StorageTraits<MapUnownedVmo>;
  struct Mapping {
    Mapping() = default;

    // These are almost the default move constructor and assignment operator,
    // but they ensure the values are never copied.
    Mapping(Mapping&& other) { *this = std::move(other); }
    Mapping& operator=(Mapping&& other) {
      std::swap(offset_, other.offset_);
      std::swap(address_, other.address_);
      std::swap(size_, other.size_);
      return *this;
    }

    std::byte* data() const { return reinterpret_cast<std::byte*>(address_); }

    ByteView bytes() const { return {data(), size_}; }

    uint64_t offset_ = 0;
    uintptr_t address_ = 0;
    size_t size_ = 0;
    bool write_ = false;
  };

  zx::unowned_vmo vmo_;
  zx::unowned_vmar vmar_;
  Mapping mapping_;
  bool writable_;
};

// zbitl::MapOwnedVmo is like zbitl::MapUnownedVmo, but it owns the VMO handle.
// zbitl::View<zbitl::MapUnownedVmo>::Copy creates a zbitl::MapOwnedVmo.
class MapOwnedVmo : public MapUnownedVmo {
 public:
  explicit MapOwnedVmo(zx::vmo vmo, bool writable = false,
                       zx::unowned_vmar vmar = zx::vmar::root_self())
      : MapUnownedVmo(zx::unowned_vmo{vmo}, writable, zx::unowned_vmar{vmar}),
        owned_vmo_(std::move(vmo)) {}

  MapOwnedVmo() = default;
  MapOwnedVmo(const MapOwnedVmo&) = delete;
  MapOwnedVmo(MapOwnedVmo&&) = default;

  MapOwnedVmo& operator=(const MapOwnedVmo&) = delete;
  MapOwnedVmo& operator=(MapOwnedVmo&& other) = default;

  zx::vmo release() { return std::move(owned_vmo_); }

 private:
  zx::vmo owned_vmo_;
};

template <>
struct StorageTraits<zx::vmo> {
 public:
  /// Errors from zx::vmo calls.
  using error_type = zx_status_t;

  /// Offset into the VMO where the ZBI item payload begins.
  using payload_type = uint64_t;

  // Exposed for testing.
  static constexpr size_t kBufferedReadChunkSize = 8192;

  static std::string_view error_string(error_type error) { return zx_status_get_string(error); }

  // Returns ZX_PROP_VMO_CONTENT_SIZE, if set - or else the page-rounded VMO
  // size.
  static fit::result<error_type, uint32_t> Capacity(const zx::vmo&);

  // Will enlarge the underlying VMO size if needed, updating
  // ZX_PROP_VMO_CONTENT_SIZE to the new capacity value if so.
  static fit::result<error_type> EnsureCapacity(const zx::vmo&, uint32_t capacity_bytes);

  static fit::result<error_type, payload_type> Payload(const zx::vmo&, uint32_t offset,
                                                       uint32_t length) {
    return fit::ok(offset);
  }

  static fit::result<error_type> Read(const zx::vmo& zbi, payload_type payload, void* buffer,
                                      uint32_t length);

  template <typename Callback>
  static auto Read(const zx::vmo& zbi, payload_type payload, uint32_t length, Callback&& callback)
      -> fit::result<error_type, decltype(callback(ByteView{}))> {
    std::optional<decltype(callback(ByteView{}))> result;
    auto cb = [&](ByteView chunk) -> bool {
      result = callback(chunk);
      return result->is_ok();
    };
    using CbType = decltype(cb);
    if (auto read_error = DoRead(
            zbi, payload, length,
            [](void* cb, ByteView chunk) { return (*static_cast<CbType*>(cb))(chunk); }, &cb);
        read_error.is_error()) {
      return fit::error{read_error.error_value()};
    } else {
      ZX_DEBUG_ASSERT(result);
      return fit::ok(*result);
    }
  }

  static fit::result<error_type> Write(const zx::vmo&, uint32_t offset, ByteView);

  static fit::result<error_type, zx::vmo> Create(const zx::vmo&, uint32_t size,
                                                 uint32_t initial_zero_size);

  template <typename SlopCheck>
  static fit::result<error_type, std::optional<std::pair<zx::vmo, uint32_t>>> Clone(
      const zx::vmo& zbi, uint32_t offset, uint32_t length, uint32_t to_offset,
      SlopCheck&& slopcheck) {
    if (slopcheck(offset % ZX_PAGE_SIZE)) {
      return DoClone(zbi, offset, length);
    }
    return fit::ok(std::nullopt);
  }

 private:
  static fit::result<error_type> DoRead(const zx::vmo& zbi, uint64_t offset, uint32_t length,
                                        bool (*)(void*, ByteView), void*);

  static fit::result<error_type, std::optional<std::pair<zx::vmo, uint32_t>>> DoClone(
      const zx::vmo& zbi, uint32_t offset, uint32_t length);
};

template <>
struct StorageTraits<zx::unowned_vmo> {
  using Owned = StorageTraits<zx::vmo>;

  using error_type = Owned::error_type;
  using payload_type = Owned::payload_type;

  static auto error_string(error_type error) { return Owned::error_string(error); }

  static auto Capacity(const zx::unowned_vmo& vmo) { return Owned::Capacity(*vmo); }

  static auto EnsureCapacity(const zx::unowned_vmo& vmo, uint32_t capacity_bytes) {
    return Owned::EnsureCapacity(*vmo, capacity_bytes);
  }

  static auto Payload(const zx::unowned_vmo& vmo, uint32_t offset, uint32_t length) {
    return Owned::Payload(*vmo, offset, length);
  }

  static fit::result<error_type> Read(const zx::unowned_vmo& vmo, payload_type payload,
                                      void* buffer, uint32_t length) {
    return Owned::Read(*vmo, payload, buffer, length);
  }

  template <typename Callback>
  static auto Read(const zx::unowned_vmo& vmo, payload_type payload, uint32_t length,
                   Callback&& callback) {
    return Owned::Read(*vmo, payload, length, std::forward<Callback>(callback));
  }

  static auto Write(const zx::unowned_vmo& vmo, uint32_t offset, ByteView data) {
    return Owned::Write(*vmo, offset, data);
  }

  static auto Create(const zx::unowned_vmo& vmo, uint32_t size, uint32_t initial_zero_size) {
    return Owned::Create(*vmo, size, initial_zero_size);
  }

  template <typename SlopCheck>
  static auto Clone(const zx::unowned_vmo& zbi, uint32_t offset, uint32_t length,
                    uint32_t to_offset, SlopCheck&& slopcheck) {
    return Owned::Clone(*zbi, offset, length, to_offset, std::forward<SlopCheck>(slopcheck));
  }
};

template <>
class StorageTraits<MapUnownedVmo> {
 public:
  using Owned = StorageTraits<zx::vmo>;

  using error_type = Owned::error_type;
  using payload_type = Owned::payload_type;

  static auto error_string(error_type error) { return Owned::error_string(error); }

  static auto Capacity(const MapUnownedVmo& zbi) { return Owned::Capacity(zbi.vmo()); }

  static fit::result<error_type> EnsureCapacity(const MapUnownedVmo& zbi, uint32_t capacity_bytes) {
    return Owned::EnsureCapacity(zbi.vmo(), capacity_bytes);
  }

  static auto Payload(const MapUnownedVmo& zbi, uint32_t offset, uint32_t length) {
    return Owned::Payload(zbi.vmo(), offset, length);
  }

  // If the locality of subsequent reads is low (i.e., if `LowLocality` is
  // true), then mapping the pages containing the data (especially when small)
  // is deemed too high a cost and this method is left unimplemented. In that
  // case, the unbuffered `Read()` is recommended instead.
  template <typename T, bool LowLocality>
  static std::enable_if_t<(alignof(T) <= kStorageAlignment) && !LowLocality,
                          fit::result<error_type, cpp20::span<const T>>>
  Read(MapUnownedVmo& zbi, payload_type payload, uint32_t length) {
    auto result = Map(zbi, payload, length, false);
    if (result.is_error()) {
      return result.take_error();
    }
    return fit::ok(AsSpan<const T>(static_cast<const std::byte*>(result.value()), length));
  }

  static fit::result<error_type> Read(const MapUnownedVmo& zbi, payload_type payload, void* buffer,
                                      uint32_t length) {
    return Owned::Read(zbi.vmo(), payload, buffer, length);
  }

  static auto Write(const MapUnownedVmo& zbi, uint32_t offset, ByteView data) {
    return Owned::Write(zbi.vmo(), offset, data);
  }

  static fit::result<error_type, void*> Write(MapUnownedVmo& zbi, uint32_t offset,
                                              uint32_t length) {
    return Map(zbi, offset, length, true);
  }

  static fit::result<error_type, MapOwnedVmo> Create(const MapUnownedVmo& proto, uint32_t size,
                                                     uint32_t initial_zero_size) {
    auto result = Owned::Create(proto.vmo(), size, initial_zero_size);
    if (result.is_error()) {
      return result.take_error();
    }
    return fit::ok(
        MapOwnedVmo{std::move(result).value(), proto.writable_, zx::unowned_vmar{proto.vmar()}});
  }

  template <typename SlopCheck>
  static fit::result<error_type, std::optional<std::pair<MapOwnedVmo, uint32_t>>> Clone(
      const MapUnownedVmo& zbi, uint32_t offset, uint32_t length, uint32_t to_offset,
      SlopCheck&& slopcheck) {
    auto result =
        Owned::Clone(zbi.vmo(), offset, length, to_offset, std::forward<SlopCheck>(slopcheck));
    if (result.is_error()) {
      return result.take_error();
    }
    if (result.value()) {
      auto [vmo, slop] = std::move(*std::move(result).value());
      return fit::ok(std::make_pair(
          MapOwnedVmo{std::move(vmo), zbi.writable_, zx::unowned_vmar{zbi.vmar()}}, slop));
    }
    return fit::ok(std::nullopt);
  }

 private:
  static fit::result<error_type, void*> Map(MapUnownedVmo& zbi, uint64_t offset, uint32_t length,
                                            bool write);
};

template <>
struct StorageTraits<MapOwnedVmo> : public StorageTraits<MapUnownedVmo> {};

}  // namespace zbitl

#endif  // SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_VMO_H_
