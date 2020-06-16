// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_FBL_INCLUDE_FBL_NAME_H_
#define ZIRCON_KERNEL_LIB_FBL_INCLUDE_FBL_NAME_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <kernel/auto_lock.h>
#include <kernel/spinlock.h>
#include <ktl/algorithm.h>

namespace fbl {

// A class for managing names of kernel objects. Since we don't want
// unbounded lengths, the constructor and setter perform
// truncation. Names include the trailing NUL as part of their
// Size-sized buffer.
template <size_t Size>
class Name {
 public:
  // Need room for at least one character and a NUL to be useful.
  static_assert(Size >= 1u, "Names must have size > 1");

  // Create an empty (i.e., "" with exactly 1 byte: a nul) Name.
  Name() {}

  // Create a name from the given data. This will be guaranteed to
  // be nul terminated, so the given data may be truncated.
  Name(const char* name, size_t len) { set(name, len); }

  ~Name() = default;

  // Copy the Name's data out. The written data is guaranteed to be
  // nul terminated, except when out_len is 0, in which case no data
  // is written.
  void get(size_t out_len, char* out_name) const __NONNULL((3)) {
    memset(out_name, 0, out_len);
    if (out_len > 0u) {
      AutoSpinLock lock(&lock_);
      strlcpy(out_name, name_, ktl::min(out_len, Size));
    }
  }

  // Reset the Name to the given data. This will be guaranteed to
  // be nul terminated, so the given data may be truncated.
  zx_status_t set(const char* name, size_t len) __NONNULL((2)) {
    // ignore characters after the first NUL
    len = strnlen(name, len);

    if (len >= Size)
      len = Size - 1;

    AutoSpinLock lock(&lock_);
    memcpy(name_, name, len);
    memset(name_ + len, 0, Size - len);
    return ZX_OK;
  }

  Name& operator=(const Name<Size>& other) {
    if (this != &other) {
      char buffer[Size];
      other.get(Size, buffer);
      set(buffer, Size);
    }
    return *this;
  }

 private:
  // These Names are often included for diagnostic purposes, and
  // access to the Name might be made under various other locks or
  // in interrupt context. So we use a spinlock to serialize.
  mutable SpinLock lock_;
  // This includes the trailing NUL.
  char name_[Size] TA_GUARDED(lock_) = {};
};

}  // namespace fbl

#endif  // ZIRCON_KERNEL_LIB_FBL_INCLUDE_FBL_NAME_H_
