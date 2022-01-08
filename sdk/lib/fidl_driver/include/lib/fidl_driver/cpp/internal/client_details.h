// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_CLIENT_DETAILS_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_CLIENT_DETAILS_H_

#include <lib/fdf/cpp/arena.h>
#include <lib/fidl/llcpp/client_base.h>

namespace fdf {
namespace internal {

// |BufferClientImplBase| stores the core state for client messaging
// implementations that use |ClientBase|, and where the message encoding buffers
// are provided by an allocator.
class BufferClientImplBase {
 public:
  explicit BufferClientImplBase(fidl::internal::ClientBase* client_base, fdf::Arena&& arena)
      : client_base_(client_base), arena_(std::move(arena)) {}

 protected:
  // Used by implementations to access the transport, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  fidl::internal::ClientBase* _client_base() const { return client_base_; }

  // Used by implementations to access the arena, hence prefixed with an
  // underscore to avoid the unlikely event of a name collision.
  fdf::Arena& _arena() { return arena_; }

 private:
  fidl::internal::ClientBase* client_base_;
  fdf::Arena arena_;
};

// A veneer interface object which delegates calls to |Impl| using the "->"
// operator.
template <typename Impl>
struct BufferClientVeneer {
 public:
  BufferClientVeneer(fidl::internal::ClientBase* client_base, fdf::Arena&& arena)
      : impl_(client_base, std::move(arena)) {}

  // Copying/moving around this object is dangerous as it may lead to dangling
  // references to the |ClientBase|. Disable these operations for now.
  BufferClientVeneer(const BufferClientVeneer&) = delete;
  BufferClientVeneer& operator=(const BufferClientVeneer&) = delete;
  BufferClientVeneer(BufferClientVeneer&&) = delete;
  BufferClientVeneer& operator=(BufferClientVeneer&&) = delete;

  // Returns a pointer to the concrete messaging implementation.
  Impl* operator->() {
    static_assert(std::is_base_of_v<BufferClientImplBase, Impl>);
    return static_cast<Impl*>(&impl_);
  }

 private:
  Impl impl_;
};

}  // namespace internal
}  // namespace fdf

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_CLIENT_DETAILS_H_
