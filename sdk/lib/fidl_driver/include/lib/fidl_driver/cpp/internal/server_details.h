// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_SERVER_DETAILS_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_SERVER_DETAILS_H_

#include <lib/fdf/cpp/arena.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fidl/llcpp/wire_messaging_declarations.h>
#include <lib/fidl_driver/cpp/wire_messaging_declarations.h>

namespace fdf::internal {

// A base class that adds the ability to set and get a contained |fdf::Arena&|.
class BufferCompleterImplBase {
 public:
  explicit BufferCompleterImplBase(fidl::CompleterBase* core, const fdf::Arena& arena)
      : core_(core), arena_(arena) {}

  // This object isn't meant to be passed around.
  BufferCompleterImplBase(BufferCompleterImplBase&&) noexcept = delete;
  BufferCompleterImplBase& operator=(BufferCompleterImplBase&&) noexcept = delete;

 protected:
  fidl::CompleterBase* _core() const { return core_; }

  const fdf::Arena& _arena() { return arena_; }

 private:
  fidl::CompleterBase* core_;
  const fdf::Arena& arena_;
};

// A base class that adds a `.buffer(...)` call to return a caller-allocating completer interface.
template <typename Method>
class CompleterImplBase {
 private:
  using Derived = fidl::internal::WireCompleterImpl<Method>;
  using BufferCompleterImpl = fidl::internal::WireBufferCompleterImpl<Method>;

  // This object isn't meant to be passed around.
  CompleterImplBase(CompleterImplBase&&) noexcept = delete;
  CompleterImplBase& operator=(CompleterImplBase&&) noexcept = delete;

 public:
  // Returns a veneer object which exposes the caller-allocating API, using the
  // provided |arena| to allocate buffers necessary for the reply. Responses
  // will live on those buffers.
  BufferCompleterImpl buffer(const fdf::Arena& arena) { return BufferCompleterImpl(core_, arena); }

 protected:
  explicit CompleterImplBase(fidl::CompleterBase* core) : core_(core) {}

  fidl::CompleterBase* _core() const { return core_; }

  void _set_core(fidl::CompleterBase* core) { core_ = core; }

 private:
  fidl::CompleterBase* core_;
};

}  // namespace fdf::internal

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_SERVER_DETAILS_H_
