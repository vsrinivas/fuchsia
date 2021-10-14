// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_H_
#define LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_H_

#include <zircon/assert.h>
#include <zircon/fidl.h>

#include <cstdint>

namespace fidl {

namespace internal {

class Handle {
 public:
  explicit constexpr Handle(uint32_t value) : value_(value) {}
  Handle(const Handle&) = default;
  Handle& operator=(const Handle&) = default;
  Handle(Handle&&) noexcept = default;
  Handle& operator=(Handle&&) noexcept = default;

  constexpr uint32_t value() const { return value_; }

 private:
  uint32_t value_;
};
constexpr Handle kInvalidHandle = Handle(0);

enum class TransportType { Channel };

struct TransportVTable {
  TransportType type;
  // Close the handle.
  void (*close)(Handle);
};

class AnyUnownedTransport {
 public:
  template <typename Transport>
  static constexpr AnyUnownedTransport Make(Handle handle) noexcept {
    return AnyUnownedTransport(&Transport::VTable, handle);
  }

  AnyUnownedTransport(const AnyUnownedTransport&) = default;
  AnyUnownedTransport& operator=(const AnyUnownedTransport&) = default;
  AnyUnownedTransport(AnyUnownedTransport&& other) noexcept = default;
  AnyUnownedTransport& operator=(AnyUnownedTransport&& other) noexcept = default;

  template <typename Transport>
  typename Transport::UnownedType get() const {
    ZX_ASSERT(vtable_->type == Transport::VTable.type);
    return typename Transport::UnownedType(handle_.value());
  }

  // TODO(fxbug.dev/85734) Add more methods here, e.g. write() using vtable.

 private:
  friend class AnyTransport;
  explicit constexpr AnyUnownedTransport(const TransportVTable* vtable, Handle handle)
      : vtable_(vtable), handle_(handle) {}

  [[maybe_unused]] const TransportVTable* vtable_;
  [[maybe_unused]] Handle handle_;
};

class AnyTransport {
 public:
  template <typename Transport>
  static AnyTransport Make(Handle handle) noexcept {
    return AnyTransport(&Transport::VTable, handle);
  }

  AnyTransport(const AnyTransport&) = delete;
  AnyTransport& operator=(const AnyTransport&) = delete;

  AnyTransport(AnyTransport&& other) noexcept : vtable_(other.vtable_), handle_(other.handle_) {
    other.handle_ = kInvalidHandle;
  }
  AnyTransport& operator=(AnyTransport&& other) noexcept {
    vtable_ = other.vtable_;
    handle_ = other.handle_;
    other.handle_ = kInvalidHandle;
    return *this;
  }
  ~AnyTransport() {
    if (handle_.value() != kInvalidHandle.value()) {
      vtable_->close(handle_);
    }
  }

  constexpr AnyUnownedTransport borrow() const { return AnyUnownedTransport(vtable_, handle_); }

  template <typename Transport>
  typename Transport::UnownedType get() const {
    ZX_ASSERT(vtable_->type == Transport::VTable.type);
    return typename Transport::UnownedType(handle_.value());
  }

  template <typename Transport>
  typename Transport::OwnedType release() {
    ZX_ASSERT(vtable_->type == Transport::VTable.type);
    Handle temp = handle_;
    handle_ = kInvalidHandle;
    return typename Transport::OwnedType(temp.value());
  }

  // TODO(fxbug.dev/85734) Add more methods here, e.g. write() using vtable.

 private:
  explicit constexpr AnyTransport(const TransportVTable* vtable, Handle handle)
      : vtable_(vtable), handle_(handle) {}

  const TransportVTable* vtable_;
  Handle handle_;
};

AnyUnownedTransport MakeAnyUnownedTransport(const AnyTransport& transport);

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_H_
