// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ZBITL_ALLOCATION_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ZBITL_ALLOCATION_H_

#include <lib/fit/result.h>
#include <lib/zbitl/storage-traits.h>

#include <ktl/string_view.h>

#include "allocation.h"

// This matches the zbitl::View::CopyStorageItem allocator signature.
fit::result<ktl::string_view, Allocation> ZbitlScratchAllocator(size_t size);

template <>
struct zbitl::StorageTraits<Allocation> {
  using Storage = Allocation;
  using SpanTraits = zbitl::StorageTraits<ktl::span<ktl::byte>>;

  // An instance represents a failure mode of being out of memory.
  using error_type = SpanTraits::error_type;
  using payload_type = SpanTraits::payload_type;

  static std::string_view error_string(error_type error) { return "out of memory"; }

  static fit::result<error_type, uint32_t> Capacity(const Storage& storage) {
    return fit::ok(static_cast<uint32_t>(storage.size_bytes()));
  }

  static fit::result<error_type> EnsureCapacity(Storage& storage, uint32_t capacity_bytes) {
    if (capacity_bytes > storage.size_bytes()) {
      fbl::AllocChecker ac;
      storage.Resize(ac, capacity_bytes);
      if (!ac.check()) {
        return fit::error{error_type{}};
      }
    }
    return fit::ok();
  }

  static fit::result<error_type, payload_type> Payload(const Storage& storage, uint32_t offset,
                                                       uint32_t length) {
    auto span = storage.data();
    return SpanTraits::Payload(span, offset, length);
  }

  template <typename U, bool LowLocality>
  static auto Read(const Storage& storage, payload_type payload, uint32_t length) {
    auto span = storage.data();
    return SpanTraits::template Read<U, LowLocality>(span, payload, length);
  }

  static fit::result<error_type> Write(Storage& storage, uint32_t offset, zbitl::ByteView data) {
    auto span = storage.data();
    return SpanTraits::Write(span, offset, data);
  }

  static fit::result<error_type, void*> Write(Storage& storage, uint32_t offset, uint32_t length) {
    auto span = storage.data();
    return SpanTraits::Write(span, offset, length);
  }

  static fit::result<error_type, Storage> Create(Storage& old, uint32_t size,
                                                 uint32_t initial_zero_size) {
    fbl::AllocChecker ac;
    Storage new_storage = Allocation::New(ac, old.type(), size, old.alignment());
    if (!ac.check()) {
      return fit::error{error_type{}};
    }
    if (initial_zero_size > 0) {
      ZX_DEBUG_ASSERT(initial_zero_size <= size);
      memset(new_storage.get(), 0, initial_zero_size);
    }
    return fit::ok(std::move(new_storage));
  }
};

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ZBITL_ALLOCATION_H_
