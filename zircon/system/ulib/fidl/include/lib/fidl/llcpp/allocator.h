// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ALLOCATOR_H_
#define LIB_FIDL_LLCPP_ALLOCATOR_H_

#include <functional>
#include <type_traits>

#include "tracking_ptr.h"

namespace fidl {

// Allocator is a base class for a family of classes that implement allocation algorithms
// to allocate objects on the heap or stack.
//
// Usage:
// BufferAllocator<2048> allocator;
// tracking_ptr<MyObj> obj = allocator.make<MyObj>(arg1, arg2);
// tracking_ptr<int[]> arr = allocator.make<int[]>(10);
//
// Allocator is intended to work with fidl::tracking_ptr, fidl::unowned_ptr and fidl::aligned
// and can be used to build LLCPP objects. Example of building tables:
// BufferAllocator<2048> allocator;
// MyTable table = MyTable::Builder(allocator.make<MyTable::Frame)
//                 .set_some_field(allocator.make<uint64_t>(1234))
//                 .build();
// In the above example, each out-of-line element is allocated using an Allocator.
//
// Allocator implementations must either:
// - Handle destruction themselves (i.e. call destructors where needed when the allocator goes
// out of scope).
// - Return a allocation_result with requires_delete set to true. This will result in a
// tracking_ptr being returned that will apply the delete/delete[] operator when the tracking_ptr
// goes out of scope
// TODO(fxb/42059) Support the equivalent of std::make_unique_for_overwrite. (see helpers.h).
class Allocator {
 public:
  // make allocates an object of the specified type and initializes it with the given arguments.
  // It intended to behave identically to std::make_unique<T> but use the allocator rather than
  // the heap.
  // TODO(fxb/42059) Consider making it possible to pack small objects tighter by having dedicated
  // blocks. More complication always has more performance impact, though.
  template <typename T, typename = std::enable_if_t<!std::is_array<T>::value>, typename... Args>
  tracking_ptr<T> make(Args&&... args) {
    allocation_result result = allocate(sizeof(T), 1, destructors<T>::make_destructor());
    fidl::aligned<T>* ptr = new (result.data) fidl::aligned<T>(std::forward<Args>(args)...);

#if TRACKING_PTR_ENABLE_UNIQUE_PTR_CONSTRUCTOR
    if (result.requires_delete) {
      return std::unique_ptr<T>(&ptr->value);
    } else {
#endif
      return fidl::unowned_ptr<fidl::aligned<T>>(ptr);
#if TRACKING_PTR_ENABLE_UNIQUE_PTR_CONSTRUCTOR
    }
#endif
  }

  // make allocates an array of the specified type and size.
  // It is intended to behave identically to std::make_unique<T[]> but uses the allocator rather
  // than the heap.
  template <typename T, typename = std::enable_if_t<std::is_array<T>::value>,
            typename ArraylessT = std::remove_extent_t<T>>
  tracking_ptr<T> make(size_t count) {
    allocation_result result =
        allocate(sizeof(ArraylessT), count, destructors<ArraylessT>::make_destructor());
    ArraylessT* ptr = new (result.data) ArraylessT[count]();

#if TRACKING_PTR_ENABLE_UNIQUE_PTR_CONSTRUCTOR
    if (result.requires_delete) {
      return std::unique_ptr<T>(&ptr->value);
    } else {
#endif
      return fidl::unowned_ptr<ArraylessT>(ptr);
#if TRACKING_PTR_ENABLE_UNIQUE_PTR_CONSTRUCTOR
    }
#endif
  }

 protected:
  Allocator() {}
  ~Allocator() {}

  // Use a raw function pointer rather than std::function to ensure we don't
  // accidentally heap allocate.
  typedef void (*destructor)(void* ptr, size_t count);
  static constexpr destructor trivial_destructor = nullptr;

  struct allocation_result {
    void* data;
    bool requires_delete;
  };
  virtual allocation_result allocate(size_t obj_size, uint32_t count, destructor dtor) = 0;

 private:
  template <typename T>
  struct destructors {
    static destructor make_destructor() {
      if (std::is_trivially_destructible<T>::value) {
        return trivial_destructor;
      }
      return nontrivial_destructor;
    }
    static void nontrivial_destructor(void* ptr, size_t count) {
      T* obj = reinterpret_cast<T*>(ptr);
      for (; count > 0; count--) {
        obj->~T();
        obj++;
      }
    }
  };
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ALLOCATOR_H_
