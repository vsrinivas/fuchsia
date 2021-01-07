// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_NO_DESTRUCTOR_H_
#define FBL_NO_DESTRUCTOR_H_

#include <cstddef>
#include <utility>

namespace fbl {

// The NoDestructor<> class wraps another object type, preventing its destructor
// from running.
//
// The typical use-case of this is to allow static variables to be defined while
// avoiding their destructors from being called at program exit:
//
//   Object& MyFunction() {
//     // Allocate `Object` lazily on first access, in a thread-safe manner (since C++11).
//     static fbl::NoDestructor<Object> object(args);
//
//     // Use the allocated object.
//     object->foo = 1;
//     object->Bar();
//
//     // Because the object is allocated from static storage, it is safe
//     // to return references to the object from the function.
//     return *object;
//   }
//
// Without the `fbl::NoDestructor<>` above, the destructor of Object will be
// called at an unclear time a program termination.
//
// In contrast, using `fbl::NoDestructor` results in the object surviving
// until program exit, avoiding potential ordering issues during program
// shutdown and reducing code bloat.
template <class T>
class NoDestructor {
 public:
  // Construct the underlying object "T".
  template <typename... Args>
  explicit NoDestructor(Args&&... args) noexcept(noexcept(T(std::forward<Args>(args)...))) {
    new (storage_) T(std::forward<Args>(args)...);
  }

  // Disable copy/move construction from fbl::NoDestructor.
  NoDestructor(const NoDestructor& other) = delete;
  NoDestructor& operator=(const NoDestructor& other) = delete;

  // Allow copy/move construction from "T".
  NoDestructor(const T& value) {  // NOLINT(google-explicit-constructor)
    new (&storage_) T(value);
  }
  NoDestructor(T&& value) {  // NOLINT(google-explicit-constructor)
    new (&storage_) T(std::move(value));
  }

  // Get the internal object.
  T* get() { return reinterpret_cast<T*>(&storage_); }
  const T* get() const { return reinterpret_cast<const T*>(&storage_); }

  // Pointer syntax.
  T& operator*() { return *get(); }
  T* operator->() { return get(); }
  const T& operator*() const { return *get(); }
  const T* operator->() const { return get(); }

 private:
  alignas(T) std::byte storage_[sizeof(T)];
};

}  // namespace fbl

#endif  // FBL_NO_DESTRUCTOR_H_
