// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZXIO_PRIVATE_H_
#define ZIRCON_SYSTEM_ULIB_ZXIO_PRIVATE_H_

#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zxio/extensions.h>
#include <lib/zxio/zxio.h>
#include <zircon/types.h>

#include <algorithm>
#include <functional>

namespace {

template <typename F>
zx_status_t zxio_do_vector(const zx_iovec_t* vector, size_t vector_count, size_t* out_actual,
                           F fn) {
  size_t total = 0;
  for (size_t i = 0; i < vector_count; ++i) {
    size_t actual;
    zx_status_t status = fn(vector[i].buffer, vector[i].capacity, &actual);
    if (status != ZX_OK) {
      // This can't be `i > 0` because the first buffer supplied by the caller
      // might've been of length zero, and we may never have attempted an I/O
      // operation with it.
      if (total > 0) {
        break;
      }
      return status;
    }
    total += actual;
    if (actual != vector[i].capacity) {
      // Short.
      break;
    }
  }
  *out_actual = total;
  return ZX_OK;
}

template <typename F>
zx_status_t zxio_vmo_do_vector(size_t start, size_t length, size_t* offset,
                               const zx_iovec_t* vector, size_t vector_count, size_t* out_actual,
                               F fn) {
  if (*offset > length) {
    return ZX_ERR_INVALID_ARGS;
  }
  return zxio_do_vector(vector, vector_count, out_actual,
                        [&](void* buffer, size_t capacity, size_t* out_actual) {
                          capacity = std::min(capacity, length - *offset);
                          zx_status_t status = fn(buffer, start + *offset, capacity);
                          if (status != ZX_OK) {
                            return status;
                          }
                          *offset += capacity;
                          *out_actual = capacity;
                          return ZX_OK;
                        });
}

}  // namespace

// A utility which helps implementing the C-style |zxio_ops_t| ops table
// from a C++ class. The specific backend implementation should inherit
// from |HasIo| as the first base class, ensuring that the |zxio_t| part
// appears that the beginning of its object layout.
class HasIo {
 protected:
  explicit HasIo(const zxio_ops_t& ops) { zxio_init(&io_, &ops); }

  zxio_t* io() { return &io_; }
  const zxio_t* io() const { return &io_; }

  template <typename T>
  struct Adaptor {
    static_assert(std::is_base_of<HasIo, T>::value);
    static_assert(sizeof(T) <= sizeof(zxio_storage_t),
                  "C++ implementation class must fit inside zxio_storage_t.");
    static_assert(!std::is_polymorphic_v<T>,
                  "C++ implementation class must be not contain vtables.");

    // Converts a member function in the implementation C++ class to a signature
    // compatible with the definition in the ops table.
    //
    // This class assumes the |zxio_t*| pointer as passed as the first argument to
    // all |zxio_ops_t| entries is the pointer to the C++ implementation instance.
    //
    // For example, given the |release| call with the following signature:
    //
    //   zx_status_t (*release)(zxio_t* io, zx_handle_t* out_handle);
    //
    // The C++ implementation may define a member function with this signature:
    //
    //   zx_status_t MyClass::Release(zx_handle_t* out_handle);
    //
    // And |Adaptor<MyClass>::From<&Release>| will evaluate to a function with a
    // signature compatible to the C-style definition, treating |io| as a pointer
    // to the |HasIo|, invoking the corresponding member function automatically.
    template <auto fn, typename... Args>
    static zx_status_t From(Args... args) {
      auto memfn = std::mem_fn(fn);
      return FromImpl(memfn, args...);
    }

   private:
    template <typename MemFn, typename... Args>
    static zx_status_t FromImpl(MemFn memfn, zxio_t* io, Args... args) {
      T* instance = reinterpret_cast<T*>(io);
      return memfn(instance, args...);
    }
  };

 private:
  static constexpr void CheckLayout();

  zxio_t io_;
};

constexpr void HasIo::CheckLayout() {
  static_assert(offsetof(HasIo, io_) == 0);
  static_assert(alignof(HasIo) == alignof(zxio_t));
}

bool zxio_is_valid(const zxio_t* io);

zx_status_t zxio_datagram_pipe_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                     zxio_flags_t flags, size_t* out_actual);

zx_status_t zxio_datagram_pipe_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                      zxio_flags_t flags, size_t* out_actual);

zx_status_t zxio_vmo_seek(zxio_t* io, zxio_seek_origin_t start, int64_t offset, size_t* out_offset);

void zxio_node_init(zxio_node_t* node, zx_handle_t control, const zxio_extension_ops_t* ops);

// Common functionalities shared by the fuchsia.io v1 |node| and |remote| transports.
// These operate on the raw FIDL channel directly, as |node| and |remote|
// have different object layouts.

// Send a |fuchsia.io/Node.Close| message on |control|. Note: does not close the channel.
zx_status_t zxio_raw_remote_close(zx::unowned_channel control);

zx_status_t zxio_raw_remote_clone(zx::unowned_channel source, zx_handle_t* out_handle);

zx_status_t zxio_raw_remote_attr_get(zx::unowned_channel control, zxio_node_attributes_t* out_attr);

zx_status_t zxio_raw_remote_attr_set(zx::unowned_channel control,
                                     const zxio_node_attributes_t* attr);

#endif  // ZIRCON_SYSTEM_ULIB_ZXIO_PRIVATE_H_
