// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_VMO_H_
#define LIB_ZBITL_VMO_H_

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include <optional>
#include <utility>

#include "storage_traits.h"

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
  explicit MapUnownedVmo(zx::unowned_vmo vmo, zx::unowned_vmar vmar = zx::vmar::root_self())
      : vmo_(std::move(vmo)), vmar_(std::move(vmar)) {}

  MapUnownedVmo() = default;
  MapUnownedVmo(MapUnownedVmo&&) = default;
  MapUnownedVmo& operator=(MapUnownedVmo&&) = default;

  // The default copy constructor and copy assignment operator are implicitly
  // deleted because the zx::unowned_* types are not copy-constructible, but
  // they are safe to copy here since it's clear from this type's purpose that
  // it always holds non-owning references.

  MapUnownedVmo(const MapUnownedVmo& other)
      : vmo_{zx::unowned_vmo{other.vmo_}}, vmar_{zx::unowned_vmar{other.vmar_}} {}

  MapUnownedVmo& operator=(const MapUnownedVmo& other) {
    vmo_ = zx::unowned_vmo{other.vmo_};
    vmar_ = zx::unowned_vmar{other.vmar_};
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

    ByteView bytes() const { return {reinterpret_cast<const std::byte*>(address_), size_}; }

    uint64_t offset_ = 0;
    uintptr_t address_ = 0;
    size_t size_ = 0;
  };

  zx::unowned_vmo vmo_;
  zx::unowned_vmar vmar_;
  Mapping mapping_;
};

// zbitl::MapOwnedVmo is like zbitl::MapUnownedVmo, but it owns the VMO handle.
// zbitl::View<zbitl::MapUnownedVmo>::Copy creates a zbitl::MapOwnedVmo.
class MapOwnedVmo : public MapUnownedVmo {
 public:
  explicit MapOwnedVmo(zx::vmo vmo, zx::unowned_vmar vmar = zx::vmar::root_self())
      : MapUnownedVmo(zx::unowned_vmo{vmo}, zx::unowned_vmar{vmar}), owned_vmo_(std::move(vmo)) {}

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

  static fitx::result<error_type, uint32_t> Capacity(const zx::vmo&);

  static fitx::result<error_type, zbi_header_t> Header(const zx::vmo&, uint32_t offset);

  static fitx::result<error_type, payload_type> Payload(const zx::vmo&, uint32_t offset,
                                                        uint32_t length) {
    return fitx::ok(offset);
  }

  template <typename Callback>
  static auto Read(const zx::vmo& zbi, payload_type payload, uint32_t length, Callback&& callback)
      -> fitx::result<error_type, decltype(callback(ByteView{}))> {
    decltype(callback(ByteView{})) result = fitx::ok();
    auto cb = [&](ByteView chunk) -> bool {
      result = callback(chunk);
      return result.is_ok();
    };
    using CbType = decltype(cb);
    if (auto read_error = DoRead(
            zbi, payload, length,
            [](void* cb, ByteView chunk) { return (*static_cast<CbType*>(cb))(chunk); }, &cb);
        read_error.is_error()) {
      return fitx::error{read_error.error_value()};
    } else {
      return fitx::ok(result);
    }
  }

  static fitx::result<error_type> Write(const zx::vmo&, uint32_t offset, ByteView);

 private:
  static fitx::result<error_type> DoRead(const zx::vmo& zbi, uint64_t offset, uint32_t length,
                                         bool (*)(void*, ByteView), void*);
};

template <>
struct StorageTraits<zx::unowned_vmo> {
  using Owned = StorageTraits<zx::vmo>;

  using error_type = Owned::error_type;
  using payload_type = Owned::payload_type;

  static auto Capacity(const zx::unowned_vmo& vmo) { return Owned::Capacity(*vmo); }

  static auto Header(const zx::unowned_vmo& vmo, uint32_t offset) {
    return Owned::Header(*vmo, offset);
  }

  static auto Payload(const zx::unowned_vmo& vmo, uint32_t offset, uint32_t length) {
    return Owned::Payload(*vmo, offset, length);
  }

  template <typename Callback>
  static auto Read(const zx::unowned_vmo& vmo, payload_type payload, uint32_t length,
                   Callback&& callback) {
    return Owned::Read(*vmo, payload, length, std::forward<Callback>(callback));
  }

  static auto Write(const zx::unowned_vmo& vmo, uint32_t offset, ByteView data) {
    return Owned::Write(*vmo, offset, data);
  }
};

template <>
class StorageTraits<MapUnownedVmo> {
 public:
  using Owned = StorageTraits<zx::vmo>;

  using error_type = Owned::error_type;
  using payload_type = Owned::payload_type;

  static auto Capacity(const MapUnownedVmo& zbi) { return Owned::Capacity(zbi.vmo()); }

  static auto Header(const MapUnownedVmo& zbi, uint32_t offset) {
    return Owned::Header(zbi.vmo(), offset);
  }

  static auto Payload(const MapUnownedVmo& zbi, uint32_t offset, uint32_t length) {
    return Owned::Payload(zbi.vmo(), offset, length);
  }

  template <typename Callback>
  static auto Read(MapUnownedVmo& zbi, payload_type payload, uint32_t length, Callback&& callback)
      -> fitx::result<error_type, decltype(callback(ByteView{}))> {
    if (auto mapped = DoRead(zbi, payload, length); mapped.is_error()) {
      return mapped.take_error();
    } else {
      return fitx::ok(callback(mapped.value()));
    }
  }

  static auto Write(const MapUnownedVmo& zbi, uint32_t offset, ByteView data) {
    return Owned::Write(zbi.vmo(), offset, data);
  }

 private:
  static fitx::result<error_type, ByteView> DoRead(MapUnownedVmo& zbi, payload_type payload,
                                                   uint32_t length);
};

template <>
class StorageTraits<MapOwnedVmo> : public StorageTraits<MapUnownedVmo> {};

}  // namespace zbitl

#endif  // LIB_ZBITL_VMO_H_
