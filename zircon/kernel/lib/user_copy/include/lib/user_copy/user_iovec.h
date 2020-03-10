// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USER_COPY_INCLUDE_LIB_USER_COPY_USER_IOVEC_H_
#define ZIRCON_KERNEL_LIB_USER_COPY_INCLUDE_LIB_USER_COPY_USER_IOVEC_H_

#include <lib/user_copy/user_ptr.h>
#include <stddef.h>
#include <zircon/types.h>

#include <ktl/forward.h>
#include <ktl/type_traits.h>

namespace internal {

template <InOutPolicy Policy>
class user_iovec {
 public:
  enum { is_out = ((Policy & kOut) == kOut) };

  // special operator to return the nullness of the pointer
  explicit operator bool() const { return vector_ != nullptr; }

  using DataType = typename ktl::conditional<is_out, char, const char>::type;
  using PtrType = user_ptr<DataType, Policy>;
  using VecType = typename ktl::conditional<is_out, zx_iovec_t, const zx_iovec_t>::type;

  explicit user_iovec(VecType* vector, size_t count) : vector_(vector), count_(count) {}

  zx_status_t GetTotalCapacity(size_t* out_capacity) const {
    size_t total_capacity = 0;
    zx_status_t status = ForEach([&total_capacity](PtrType ptr, size_t capacity) {
      if (add_overflow(total_capacity, capacity, &total_capacity)) {
        return ZX_ERR_INVALID_ARGS;
      }
      return ZX_ERR_NEXT;
    });
    if (status != ZX_OK) {
      return status;
    }
    *out_capacity = total_capacity;
    return ZX_OK;
  }

  // Iterate through the iovec.
  //
  // The expected signature for the callback is:
  //   zx_status_t (callback)(PtrType ptr, size_t capacity).
  //
  // To continue to the next buffer in the vector, return ZX_ERR_NEXT. To stop
  // iterating successfully, return ZX_ERR_STOP. Returning any other error will
  // also stop the iteration but will cause ForEach to retun that error instead
  // of ZX_OK.
  template <typename Callback>
  zx_status_t ForEach(Callback callback) const {
    user_in_ptr<const zx_iovec_t> base(vector_);
    for (size_t i = 0; i < count_; ++i) {
      user_in_ptr<const zx_iovec_t> user_current = base.element_offset(i);
      zx_iovec_t current = {};
      zx_status_t status = user_current.copy_from_user(&current);
      if (status != ZX_OK) {
        return status;
      }
      status = callback(PtrType(static_cast<DataType*>(current.buffer)), current.capacity);
      if (status == ZX_ERR_NEXT) {
        continue;
      }
      if (status == ZX_ERR_STOP) {
        break;
      }
      return status;
    }
    return ZX_OK;
  }

 private:
  VecType* vector_;
  size_t count_;
};

}  // namespace internal

using user_in_iovec_t = internal::user_iovec<internal::kIn>;
using user_out_iovec_t = internal::user_iovec<internal::kOut>;
using user_inout_iovec_t = internal::user_iovec<internal::kInOut>;

inline user_in_iovec_t make_user_in_iovec(user_in_ptr<const zx_iovec_t> vector, size_t count) {
  return user_in_iovec_t(vector.get(), count);
}

inline user_out_iovec_t make_user_out_iovec(user_out_ptr<zx_iovec_t> vector, size_t count) {
  return user_out_iovec_t(vector.get(), count);
}

inline user_inout_iovec_t make_user_inout_iovec(user_inout_ptr<zx_iovec_t> vector, size_t count) {
  return user_inout_iovec_t(vector.get(), count);
}

#endif  // ZIRCON_KERNEL_LIB_USER_COPY_INCLUDE_LIB_USER_COPY_USER_IOVEC_H_
