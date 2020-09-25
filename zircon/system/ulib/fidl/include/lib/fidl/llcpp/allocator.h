// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ALLOCATOR_H_
#define LIB_FIDL_LLCPP_ALLOCATOR_H_

#include <lib/fidl/llcpp/traits.h>
#include <zircon/assert.h>

#include <functional>
#include <type_traits>

#include "tracking_ptr.h"

namespace fidl {

// Allocator is a base class for a family of classes that implement allocation algorithms
// to allocate objects on the heap or stack.
//
// Usage:
// BufferThenHeapAllocator<2048> allocator;
// tracking_ptr<MyObj> obj = allocator.make<MyObj>(arg1, arg2);
// tracking_ptr<int[]> arr = allocator.make<int[]>(10);
//
// Allocator is intended to work with fidl::tracking_ptr, fidl::unowned_ptr_t and fidl::aligned
// and can be used to build LLCPP objects. Example of building tables:
// BufferThenHeapAllocator<2048> allocator;
// MyTable table = MyTable::Builder(allocator.make<MyTable::Frame>())
//                 .set_some_field(allocator.make<uint64_t>(1234))
//                 .build();
// In the above example, each out-of-line element is allocated using an Allocator.
//
// Allocator implementations must either:
// - Handle destruction themselves (i.e. call destructors where needed when the allocator goes
// out of scope).
// - Return a allocation_result with heap_allocate set to true. This will result in a
// tracking_ptr being returned that will apply the delete/delete[] operator when the tracking_ptr
// goes out of scope
// TODO(fxbug.dev/42059) Support the equivalent of std::make_unique_for_overwrite. (see helpers.h).
class Allocator {
 public:
  // make allocates an object of the specified type and initializes it with the given arguments.
  // It intended to behave identically to std::make_unique<T> but use the allocator rather than
  // the heap.
  // TODO(fxbug.dev/42059) Consider making it possible to pack small objects tighter by having dedicated
  // blocks. More complication always has more performance impact, though.
  template <typename T, typename = std::enable_if_t<!std::is_array<T>::value>, typename... Args>
  tracking_ptr<T> make(Args&&... args) {
    allocation_result result = allocate(AllocationType::kNonArray, sizeof(fidl::aligned<T>), 1,
                                        destructors<T>::make_destructor());
    if (result.data == nullptr) {
      if (result.heap_allocate) {
        return tracking_ptr<T>(std::make_unique<T>(std::forward<Args>(args)...));
      } else {
        // Direct use of UnsafeBufferAllocator risks ending up here:
        ZX_PANIC("Allocator sub-class allocate() failed and heap_allocate was false\n");
      }
    }
    ZX_DEBUG_ASSERT(!result.heap_allocate);
    fidl::aligned<T>* ptr = new (result.data) fidl::aligned<T>(std::forward<Args>(args)...);
    return fidl::unowned_ptr_t<fidl::aligned<T>>(ptr);
  }

  // make allocates an array of the specified type and size.
  // It is intended to behave identically to std::make_unique<T[]> but uses the allocator rather
  // than the heap.
  template <typename T, typename = std::enable_if_t<std::is_array<T>::value>,
            typename ArraylessT = std::remove_extent_t<T>>
  tracking_ptr<T> make(size_t count) {
    allocation_result result = allocate(AllocationType::kArray, sizeof(ArraylessT), count,
                                        destructors<ArraylessT>::make_destructor());
    if (result.data == nullptr) {
      if (result.heap_allocate) {
        return tracking_ptr<ArraylessT[]>(std::make_unique<ArraylessT[]>(count));
      } else {
        // Direct use of UnsafeBufferAllocator risks ending up here:
        ZX_PANIC("Allocator sub-class allocate() failed and heap_allocate was false\n");
      }
    }
    ZX_DEBUG_ASSERT(!result.heap_allocate);
    ArraylessT* ptr = new (result.data) ArraylessT[count]();
    return fidl::unowned_ptr_t<ArraylessT>(ptr);
  }

  template <typename Table, typename std::enable_if<::fidl::IsTable<Table>::value, int>::type = 0>
  typename Table::Builder make_table_builder() {
    return typename Table::Builder(make<typename Table::Frame>());
  }

  template <typename T,
            typename std::enable_if<!std::is_array<T>::value && !fidl::IsVectorView<T>::value,
                                    int>::type = 0>
  typename fidl::VectorView<T> make_vec(size_t count) {
    return fidl::VectorView<T>(make<T[]>(count), count);
  }

  template <typename T,
            typename std::enable_if<!std::is_array<T>::value && !fidl::IsVectorView<T>::value,
                                    int>::type = 0>
  typename fidl::VectorView<T> make_vec(size_t count, size_t capacity) {
    ZX_DEBUG_ASSERT(capacity >= count);
    return fidl::VectorView<T>(make<T[]>(capacity), count);
  }

  template <typename T,
            typename std::enable_if<!std::is_array<T>::value && !::fidl::IsVectorView<T>::value &&
                                        !::fidl::IsStringView<T>::value,
                                    int>::type = 0>
  tracking_ptr<fidl::VectorView<T>> make_vec_ptr(size_t count) {
    return fidl::tracking_ptr<fidl::VectorView<T>>(
        make<fidl::VectorView<T>>(make<T[]>(count), count));
  }

  template <typename T,
            typename std::enable_if<!std::is_array<T>::value && !::fidl::IsVectorView<T>::value &&
                                        !::fidl::IsStringView<T>::value,
                                    int>::type = 0>
  tracking_ptr<fidl::VectorView<T>> make_vec_ptr(size_t count, size_t capacity) {
    return fidl::tracking_ptr<fidl::VectorView<T>>(
        make<fidl::VectorView<T>>(make<T[]>(capacity), count));
  }

  template <typename, typename...>
  friend class FailoverHeapAllocator;

 protected:
  Allocator() {}
  virtual ~Allocator() {}

  // Use a raw function pointer rather than std::function to ensure we don't
  // accidentally heap allocate.
  typedef void (*destructor)(void* ptr, size_t count);
  static constexpr destructor trivial_destructor = nullptr;

  enum class AllocationType {
    kArray = 1,
    kNonArray = 2,
  };

  struct allocation_result {
    void* data;
    // If true, the sub-class allocate() is specifying that we want to allocate from the heap (as
    // failover, or just because we want heap).  Since the heap allocation will end up being
    // deleted using delete/delete[], we need to use new/new[] for the new-ing.  This must be false
    // unless data is nullptr.
    bool heap_allocate;
  };
  virtual allocation_result allocate(AllocationType type, size_t obj_size, size_t count,
                                     destructor dtor) = 0;

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
