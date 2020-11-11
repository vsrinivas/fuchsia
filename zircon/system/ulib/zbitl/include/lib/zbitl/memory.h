// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_MEMORY_H_
#define LIB_ZBITL_MEMORY_H_

#include <cstring>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/span.h>
#include <zircon/assert.h>

#include "storage_traits.h"

namespace zbitl {

template <typename T>
class StorageTraits<fbl::Span<T>> {
 public:
  using Storage = fbl::Span<T>;

  struct error_type {};

  using payload_type = fbl::Span<T>;

  static std::string_view error_string(error_type error) { return {}; }

  static fitx::result<error_type, uint32_t> Capacity(const Storage& storage) {
    return fitx::ok(static_cast<uint32_t>(storage.size()));
  }

  static fitx::result<error_type, std::reference_wrapper<const zbi_header_t>> Header(
      const Storage& storage, uint32_t offset) {
    ZX_DEBUG_ASSERT(offset <= storage.size() - sizeof(zbi_header_t));
    return fitx::ok(std::ref(*reinterpret_cast<const zbi_header_t*>(
        storage.subspan(offset, sizeof(zbi_header_t)).data())));
  }

  static fitx::result<error_type, payload_type> Payload(const Storage& storage, uint32_t offset,
                                                        uint32_t length) {
    ZX_DEBUG_ASSERT(offset <= storage.size() - length);
    auto payload = storage.subspan(offset, length);
    ZX_ASSERT_MSG(payload.size() % sizeof(T) == 0,
                  "payload size not a multiple of storage fbl::Span element_type size");
    return fitx::ok(payload);
  }

  static fitx::result<error_type, ByteView> Read(const Storage& storage, payload_type payload,
                                                 uint32_t length) {
    auto payload_bytes = fbl::as_bytes(payload);
    ByteView bytes{payload_bytes.data(), payload_bytes.size()};
    ZX_DEBUG_ASSERT(bytes.size() == length);
    return fitx::ok(bytes);
  }

  template <typename S = T, typename = std::enable_if_t<!std::is_const_v<S>>>
  static fitx::result<error_type> Write(Storage& storage, uint32_t offset, ByteView data) {
    memcpy(Write(storage, offset, static_cast<uint32_t>(data.size())).value(), data.data(),
           data.size());
    return fitx::ok();
  }

  template <typename S = T, typename = std::enable_if_t<!std::is_const_v<S>>>
  static fitx::result<error_type, void*> Write(Storage& storage, uint32_t offset, uint32_t length) {
    // The caller is supposed to maintain these invariants.
    ZX_DEBUG_ASSERT(offset <= storage.size());
    ZX_DEBUG_ASSERT(length <= storage.size() - offset);
    return fitx::ok(&storage[offset]);
  }
};

// fbl::Array<T> works like std::span<T> + std::unique_ptr<T[]>.
template <typename T>
class StorageTraits<fbl::Array<T>> {
 public:
  using Storage = fbl::Array<T>;
  using SpanTraits = StorageTraits<fbl::Span<T>>;

  // An instance represents a failure mode of being out of memory.
  struct error_type {};

  using payload_type = fbl::Span<T>;

  static std::string_view error_string(error_type error) { return "out of memory"; }

  static fbl::Span<std::byte> AsBytes(const Storage& storage) {
    return {reinterpret_cast<std::byte*>(storage.data()), storage.size() * sizeof(T)};
  }

  static fbl::Span<T> AsSpan(const Storage& storage) { return {storage.data(), storage.size()}; }

  static fitx::result<error_type, uint32_t> Capacity(const Storage& storage) {
    return SpanTraits::Capacity(AsSpan(storage)).take_value();
  }

  static fitx::result<error_type> EnsureCapacity(Storage& storage, uint32_t capacity_bytes) {
    if (size_t current = AsBytes(storage).size(); current < capacity_bytes) {
      const size_t n = (capacity_bytes + sizeof(T) - 1) / sizeof(T);

      fbl::AllocChecker ac;
      Storage new_storage(new (&ac) T[n], n);
      if (!ac.check()) {
        return fitx::error{error_type{}};
      }

      memcpy(new_storage.data(), storage.data(), current);
      storage.swap(new_storage);
    }
    return fitx::ok();
  }

  static fitx::result<error_type, std::reference_wrapper<const zbi_header_t>> Header(
      const Storage& storage, uint32_t offset) {
    return SpanTraits::Header(AsSpan(storage), offset).take_value();
  }

  static fitx::result<error_type, payload_type> Payload(const Storage& storage, uint32_t offset,
                                                        uint32_t length) {
    return SpanTraits::Payload(AsSpan(storage), offset, length).take_value();
  }

  static fitx::result<error_type, ByteView> Read(const Storage& storage, payload_type payload,
                                                 uint32_t length) {
    return SpanTraits::Read(AsSpan(storage), payload, length).take_value();
  }

  template <typename S = T, typename = std::enable_if_t<!std::is_const_v<S>>>
  static fitx::result<error_type> Write(Storage& storage, uint32_t offset, ByteView data) {
    auto span = AsSpan(storage);
    auto result = SpanTraits::Write(span, offset, data);
    ZX_DEBUG_ASSERT(result.is_ok());
    return fitx::ok();
  }

  template <typename S = T, typename = std::enable_if_t<!std::is_const_v<S>>>
  static fitx::result<error_type, void*> Write(Storage& storage, uint32_t offset, uint32_t length) {
    auto span = AsSpan(storage);
    return SpanTraits::Write(span, offset, length).take_value();
  }

  static fitx::result<error_type, Storage> Create(Storage& old, size_t size) {
    const size_t n = (size + sizeof(T) - 1) / sizeof(T);
    fbl::AllocChecker ac;
    Storage new_storage(new (&ac) T[n], n);
    if (!ac.check()) {
      return fitx::error{error_type{}};
    }
    return fitx::ok(std::move(new_storage));
  }
};

}  // namespace zbitl

#endif  // LIB_ZBITL_MEMORY_H_
