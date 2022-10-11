// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_ATOMIC_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_ATOMIC_H_

#include <assert.h>

#include <atomic>
#include <cstddef>

#include "internal/atomic.h"
#include "internal/linkage.h"
#include "memory.h"
#include "type_traits.h"
#include "version.h"

namespace cpp20 {

#if defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::atomic_ref;

#else  // Use atomic_ref polyfill.

// Polyfill for |std::atomic_ref<T>|.
template <typename T>
class atomic_ref : public atomic_internal::atomic_ops<atomic_ref<T>, T>,
                   public atomic_internal::arithmetic_ops<atomic_ref<T>, T>,
                   public atomic_internal::bitwise_ops<atomic_ref<T>, T> {
 public:
  static_assert(cpp17::is_trivially_copyable_v<T>, "");

  using value_type = T;
  using difference_type = atomic_internal::difference_t<T>;

  static constexpr bool is_always_lockfree =
      __atomic_always_lock_free(sizeof(T), static_cast<T*>(nullptr));

  static constexpr std::size_t required_alignment =
      atomic_internal::alignment<T>::required_alignment;

  atomic_ref() = delete;
  LIB_STDCOMPAT_INLINE_LINKAGE explicit atomic_ref(T& obj) : ptr_(cpp17::addressof(obj)) {
    check_ptr_alignment();
  }
  LIB_STDCOMPAT_INLINE_LINKAGE atomic_ref(const atomic_ref& ref) noexcept = default;
  atomic_ref& operator=(const atomic_ref&) = delete;
  LIB_STDCOMPAT_INLINE_LINKAGE atomic_ref& operator=(T& desired) {
    this->store(desired);
    return *this;
  }

  LIB_STDCOMPAT_INLINE_LINKAGE bool is_lock_free() const noexcept {
    return __atomic_is_lock_free(sizeof(T), ptr_);
  }

  // TODO(fxb/104509): Implement wait/notify/notify_all for non kernel code.

 private:
  friend atomic_internal::atomic_ops<atomic_ref, T>;
  friend atomic_internal::arithmetic_ops<atomic_ref, T>;
  friend atomic_internal::bitwise_ops<atomic_ref, T>;

  // Checks that pointer is correctly aligned.
  LIB_STDCOMPAT_INLINE_LINKAGE void check_ptr_alignment() const {
    // Pointers not aligned to |required_alignment| are considered UB.
    assert(reinterpret_cast<uintptr_t>(ptr_) % required_alignment == 0);
  }

  T* ptr_ = nullptr;
};

#endif

}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_ATOMIC_H_
