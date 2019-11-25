// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_OBJECT_H_
#define LIB_ZX_OBJECT_H_

#include <lib/zx/object_traits.h>
#include <lib/zx/time.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace zx {

class port;
class profile;

// Wraps and takes ownership of a handle to an object.
//
// Used for code that wants to operate generically on the zx_handle_t value
// inside a |zx::object| and doesn't otherwise need a template parameter.
//
// The handle is automatically closed when the wrapper is destroyed.
class object_base {
 public:
  void reset(zx_handle_t value = ZX_HANDLE_INVALID) {
    close();
    value_ = value;
  }

  bool is_valid() const { return value_ != ZX_HANDLE_INVALID; }
  explicit operator bool() const { return is_valid(); }

  zx_handle_t get() const { return value_; }

  // Reset the underlying handle, and then get the address of the
  // underlying internal handle storage.
  //
  // Note: The intended purpose is to facilitate interactions with C
  // APIs which expect to be provided a pointer to a handle used as
  // an out parameter.
  zx_handle_t* reset_and_get_address() {
    reset();
    return &value_;
  }

  __attribute__((warn_unused_result)) zx_handle_t release() {
    zx_handle_t result = value_;
    value_ = ZX_HANDLE_INVALID;
    return result;
  }

  zx_status_t get_info(uint32_t topic, void* buffer, size_t buffer_size, size_t* actual_count,
                       size_t* avail_count) const {
    return zx_object_get_info(get(), topic, buffer, buffer_size, actual_count, avail_count);
  }

  zx_status_t get_property(uint32_t property, void* value, size_t size) const {
    return zx_object_get_property(get(), property, value, size);
  }

  zx_status_t set_property(uint32_t property, const void* value, size_t size) const {
    return zx_object_set_property(get(), property, value, size);
  }

 protected:
  constexpr object_base() : value_(ZX_HANDLE_INVALID) {}

  explicit object_base(zx_handle_t value) : value_(value) {}

  ~object_base() { close(); }

  object_base(const object_base&) = delete;

  void operator=(const object_base&) = delete;

  void close() {
    if (value_ != ZX_HANDLE_INVALID) {
      zx_handle_close(value_);
      value_ = ZX_HANDLE_INVALID;
    }
  }

  zx_handle_t value_;
};

// Forward declaration for borrow method.
template <typename T>
class unowned;

// Provides type-safe access to operations on a handle.
template <typename T>
class object : public object_base {
 public:
  constexpr object() = default;

  explicit object(zx_handle_t value) : object_base(value) {}

  template <typename U>
  object(object<U>&& other) : object_base(other.release()) {
    static_assert(is_same<T, void>::value, "Receiver must be compatible.");
  }

  template <typename U>
  object<T>& operator=(object<U>&& other) {
    static_assert(is_same<T, void>::value, "Receiver must be compatible.");
    reset(other.release());
    return *this;
  }

  void swap(object<T>& other) {
    zx_handle_t tmp = value_;
    value_ = other.value_;
    other.value_ = tmp;
  }

  zx_status_t duplicate(zx_rights_t rights, object<T>* result) const {
    static_assert(object_traits<T>::supports_duplication, "Object must support duplication.");
    zx_handle_t h = ZX_HANDLE_INVALID;
    zx_status_t status = zx_handle_duplicate(value_, rights, &h);
    result->reset(h);
    return status;
  }

  zx_status_t replace(zx_rights_t rights, object<T>* result) {
    zx_handle_t h = ZX_HANDLE_INVALID;
    zx_status_t status = zx_handle_replace(value_, rights, &h);
    // We store ZX_HANDLE_INVALID to value_ before calling reset on result
    // in case result == this.
    value_ = ZX_HANDLE_INVALID;
    result->reset(h);
    return status;
  }

  zx_status_t wait_one(zx_signals_t signals, zx::time deadline, zx_signals_t* pending) const {
    static_assert(object_traits<T>::supports_wait, "Object is not waitable.");
    return zx_object_wait_one(value_, signals, deadline.get(), pending);
  }

  zx_status_t wait_async(const object<port>& port, uint64_t key, zx_signals_t signals,
                         uint32_t options) const {
    static_assert(object_traits<T>::supports_wait, "Object is not waitable.");
    return zx_object_wait_async(value_, port.get(), key, signals, options);
  }

  static zx_status_t wait_many(zx_wait_item_t* wait_items, uint32_t count, zx::time deadline) {
    static_assert(object_traits<T>::supports_wait, "Object is not waitable.");
    return zx_object_wait_many(wait_items, count, deadline.get());
  }

  zx_status_t signal(uint32_t clear_mask, uint32_t set_mask) const {
    static_assert(object_traits<T>::supports_user_signal, "Object must support user signals.");
    return zx_object_signal(get(), clear_mask, set_mask);
  }

  zx_status_t signal_peer(uint32_t clear_mask, uint32_t set_mask) const {
    static_assert(object_traits<T>::supports_user_signal, "Object must support user signals.");
    static_assert(object_traits<T>::has_peer_handle, "Object must have peer object.");
    return zx_object_signal_peer(get(), clear_mask, set_mask);
  }

  zx_status_t get_child(uint64_t koid, zx_rights_t rights, object<void>* result) const {
    static_assert(object_traits<T>::supports_get_child, "Object must support getting children.");
    // Allow for |result| and |this| being the same container, though that
    // can only happen for |T=void|, due to strict aliasing.
    object<void> h;
    zx_status_t status = zx_object_get_child(value_, koid, rights, h.reset_and_get_address());
    result->reset(h.release());
    return status;
  }

  zx_status_t set_profile(const object<profile>& profile, uint32_t options) const {
    static_assert(object_traits<T>::supports_set_profile,
                  "Object must support scheduling profiles.");
    return zx_object_set_profile(get(), profile.get(), options);
  }

  // Returns a type-safe wrapper of the underlying handle that does not claim ownership.
  unowned<T> borrow() const { return unowned<T>(get()); }

 private:
  template <typename A, typename B>
  struct is_same {
    static const bool value = false;
  };

  template <typename A>
  struct is_same<A, A> {
    static const bool value = true;
  };
};

template <typename T>
bool operator==(const object<T>& a, const object<T>& b) {
  return a.get() == b.get();
}

template <typename T>
bool operator!=(const object<T>& a, const object<T>& b) {
  return !(a == b);
}

template <typename T>
bool operator<(const object<T>& a, const object<T>& b) {
  return a.get() < b.get();
}

template <typename T>
bool operator>(const object<T>& a, const object<T>& b) {
  return a.get() > b.get();
}

template <typename T>
bool operator<=(const object<T>& a, const object<T>& b) {
  return !(a.get() > b.get());
}

template <typename T>
bool operator>=(const object<T>& a, const object<T>& b) {
  return !(a.get() < b.get());
}

template <typename T>
bool operator==(zx_handle_t a, const object<T>& b) {
  return a == b.get();
}

template <typename T>
bool operator!=(zx_handle_t a, const object<T>& b) {
  return !(a == b);
}

template <typename T>
bool operator<(zx_handle_t a, const object<T>& b) {
  return a < b.get();
}

template <typename T>
bool operator>(zx_handle_t a, const object<T>& b) {
  return a > b.get();
}

template <typename T>
bool operator<=(zx_handle_t a, const object<T>& b) {
  return !(a > b.get());
}

template <typename T>
bool operator>=(zx_handle_t a, const object<T>& b) {
  return !(a < b.get());
}

template <typename T>
bool operator==(const object<T>& a, zx_handle_t b) {
  return a.get() == b;
}

template <typename T>
bool operator!=(const object<T>& a, zx_handle_t b) {
  return !(a == b);
}

template <typename T>
bool operator<(const object<T>& a, zx_handle_t b) {
  return a.get() < b;
}

template <typename T>
bool operator>(const object<T>& a, zx_handle_t b) {
  return a.get() > b;
}

template <typename T>
bool operator<=(const object<T>& a, zx_handle_t b) {
  return !(a.get() > b);
}

template <typename T>
bool operator>=(const object<T>& a, zx_handle_t b) {
  return !(a.get() < b);
}

// Wraps a handle to an object to provide type-safe access to its operations
// but does not take ownership of it.  The handle is not closed when the
// wrapper is destroyed.
//
// All use of unowned<object<T>> as an object<T> is via a dereference operator,
// as illustrated below:
//
// void do_something(const zx::event& event);
//
// void example(zx_handle_t event_handle) {
//     do_something(*zx::unowned<event>(event_handle));
// }
//
// Convenience aliases are provided for all object types, for example:
//
// zx::unowned_event(handle)->signal(..)
template <typename T>
class unowned final {
 public:
  explicit unowned(zx_handle_t h) : value_(h) {}
  explicit unowned(const T& owner) : unowned(owner.get()) {}
  explicit unowned(const unowned& other) : unowned(*other) {}
  constexpr unowned() = default;
  unowned(unowned&& other) = default;

  ~unowned() { release_value(); }

  unowned& operator=(const unowned& other) {
    if (&other == this) {
      return *this;
    }

    *this = unowned(other);
    return *this;
  }
  unowned& operator=(unowned&& other) {
    release_value();
    value_ = static_cast<T&&>(other.value_);
    return *this;
  }

  const T& operator*() const { return value_; }
  const T* operator->() const { return &value_; }

 private:
  void release_value() {
    zx_handle_t h = value_.release();
    static_cast<void>(h);
  }

  T value_;
};

template <typename T>
bool operator==(const unowned<T>& a, const unowned<T>& b) {
  return a->get() == b->get();
}

template <typename T>
bool operator!=(const unowned<T>& a, const unowned<T>& b) {
  return !(a == b);
}

template <typename T>
bool operator<(const unowned<T>& a, const unowned<T>& b) {
  return a->get() < b->get();
}

template <typename T>
bool operator>(const unowned<T>& a, const unowned<T>& b) {
  return a->get() > b->get();
}

template <typename T>
bool operator<=(const unowned<T>& a, const unowned<T>& b) {
  return !(a > b);
}

template <typename T>
bool operator>=(const unowned<T>& a, const unowned<T>& b) {
  return !(a < b);
}

template <typename T>
bool operator==(zx_handle_t a, const unowned<T>& b) {
  return a == b->get();
}

template <typename T>
bool operator!=(zx_handle_t a, const unowned<T>& b) {
  return !(a == b);
}

template <typename T>
bool operator<(zx_handle_t a, const unowned<T>& b) {
  return a < b->get();
}

template <typename T>
bool operator>(zx_handle_t a, const unowned<T>& b) {
  return a > b->get();
}

template <typename T>
bool operator<=(zx_handle_t a, const unowned<T>& b) {
  return !(a > b);
}

template <typename T>
bool operator>=(zx_handle_t a, const unowned<T>& b) {
  return !(a < b);
}

template <typename T>
bool operator==(const unowned<T>& a, zx_handle_t b) {
  return a->get() == b;
}

template <typename T>
bool operator!=(const unowned<T>& a, zx_handle_t b) {
  return !(a == b);
}

template <typename T>
bool operator<(const unowned<T>& a, zx_handle_t b) {
  return a->get() < b;
}

template <typename T>
bool operator>(const unowned<T>& a, zx_handle_t b) {
  return a->get() > b;
}

template <typename T>
bool operator<=(const unowned<T>& a, zx_handle_t b) {
  return !(a > b);
}

template <typename T>
bool operator>=(const unowned<T>& a, zx_handle_t b) {
  return !(a < b);
}

}  // namespace zx

#endif  // LIB_ZX_OBJECT_H_
