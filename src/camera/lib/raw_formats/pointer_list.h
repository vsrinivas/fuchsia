// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CAMERA_LIB_RAW_FORMATS_POINTER_LIST_H_
#define SRC_CAMERA_LIB_RAW_FORMATS_POINTER_LIST_H_

#include <concepts>
#include <type_traits>

namespace camera::raw {

// A type is cloneable if it has a clone() method which returns a pointer to that type.
template <typename T>
concept cloneable = std::is_member_function_pointer_v<decltype(&T::clone)> &&
                    std::is_same_v<decltype(std::declval<T&>().clone()), T*>;

/* A list of pointers. Depending on the constructor used, the memory which contains the list of
   pointers as well as the memory those pointers point to can be owned by either this object or
   owned externally.

   The "non-owning" constructor allows a PointerList to be initialized from static arrays and used
   at compile time. The "owning" constructor dynamically allocates space to store a number of
   pointers up to the given capacity. Then emplace_back can be used to dynamically construct a new
   element and store its pointer in the list. The memory for the element will be freed when the
   PointerList is destroyed.

   Copying a PointerList which owns the list and element memory will result in a deep copy (and thus
   further heap allocation).

   This class provides no thread safety guarantees for accessors. However the "non-owning"
   functionality allows instances to be created as constexpr constants which can be used safely
   from anywhere (provided the pointers stored are truly to constant data).

   Hopefully this can be replaced once the implementation of c++20 constexpr STL containers is done.
*/
template <typename T>
  requires cloneable<T> || std::is_trivially_copyable_v<T>
class PointerList {
 public:
  constexpr PointerList()
      : owns_memory_(false),
        capacity_(0),
        size_(0),
        element_array_size_(0),
        static_ptr_list_(nullptr),
        dynamic_ptr_list_(nullptr) {}

  // "Non-owning" constructor for static arrays of pointers to scalar values. Caller must ensure
  // provided pointers remain valid.
  constexpr PointerList(const T* const* ptr_list, uint64_t size)
      : owns_memory_(false),
        capacity_(size),
        size_(size),
        element_array_size_(0),
        static_ptr_list_(ptr_list),
        dynamic_ptr_list_(nullptr) {}

  // "Non-owning" constructor for static arrays of pointers to arrays of trivially copyable types.
  // Caller must ensure provided pointers remain valid.
  constexpr PointerList(const T* const* ptr_list, uint64_t size, uint64_t element_array_size)
      : owns_memory_(false),
        capacity_(size),
        size_(size),
        element_array_size_(element_array_size),
        static_ptr_list_(ptr_list),
        dynamic_ptr_list_(nullptr) {}

  // "Owning" constructor dynamically allocates space for 'capacity' T pointers.
  constexpr explicit PointerList(uint64_t capacity)
      : owns_memory_(true),
        capacity_(capacity),
        size_(0),
        element_array_size_(0),
        static_ptr_list_(nullptr),
        dynamic_ptr_list_(new T*[capacity]) {}

  // "Owning" constructor dynamically allocates space for 'capacity' pointers to arrays of T. Those
  // arrays of T are not allocated until an element is added.
  constexpr PointerList(uint64_t capacity, uint64_t element_array_size)
      : owns_memory_(true),
        capacity_(capacity),
        size_(0),
        element_array_size_(element_array_size),
        static_ptr_list_(nullptr),
        dynamic_ptr_list_(new T*[capacity]()) {}

  // Copy constructor does a deep copy if the object we're copying owns it's memory.
  constexpr PointerList(const PointerList& o)
      : owns_memory_(o.owns_memory_),
        capacity_(o.capacity_),
        size_(o.size_),
        element_array_size_(o.element_array_size_),
        static_ptr_list_(o.static_ptr_list_) {
    if (!owns_memory_) {
      dynamic_ptr_list_ = nullptr;
      return;
    }

    dynamic_ptr_list_ = new T*[capacity_];
    for (uint64_t i = 0; i < size_; ++i) {
      // If T is cloneable, that means it can't be an array of trivially copyable types. Just clone
      // it and continue.
      if constexpr (cloneable<T>) {
        dynamic_ptr_list_[i] = o.dynamic_ptr_list_[i]->clone();
      } else {
        // If T is an array pointer, allocate a new array and copy the contents. Otherwise heap
        // allocate a scalar and copy it.
        if (element_array_size_ > 0) {
          dynamic_ptr_list_[i] = new T[element_array_size_];
          for (uint64_t j = 0; j < element_array_size_; ++j) {
            dynamic_ptr_list_[i][j] = o.dynamic_ptr_list_[i][j];
          }
        } else {
          dynamic_ptr_list_[i] = new T;
          *(dynamic_ptr_list_[i]) = *(o.dynamic_ptr_list_[i]);
        }
      }
    }
  }

  constexpr PointerList(PointerList&& o) noexcept
      : owns_memory_(o.owns_memory_),
        capacity_(o.capacity_),
        size_(o.size_),
        element_array_size_(o.element_array_size_),
        static_ptr_list_(o.static_ptr_list_),
        dynamic_ptr_list_(o.dynamic_ptr_list_) {
    o.owns_memory_ = false;
    o.capacity_ = 0;
    o.size_ = 0;
    o.element_array_size_ = 0;
    o.static_ptr_list_ = nullptr;
    o.dynamic_ptr_list_ = nullptr;
  }

  constexpr ~PointerList() {
    if (owns_memory_ && dynamic_ptr_list_ != nullptr) {
      for (uint64_t i = 0; i < size_; ++i) {
        if (element_array_size_ > 0) {
          delete[] dynamic_ptr_list_[i];
        } else {
          delete dynamic_ptr_list_[i];
        }
      }
      delete[] dynamic_ptr_list_;
    }
  }

  constexpr uint64_t size() const { return size_; }
  constexpr uint64_t capacity() const { return capacity_; }
  constexpr uint64_t element_array_size() const { return element_array_size_; }

#ifdef RAW_FORMATS_POINTER_LIST_TEST
  // Conditionally exposed for unit test purposes since we can't use asserts or do logging in code
  // that can run at compile time.
  constexpr bool owns_memory() const { return owns_memory_; }
  constexpr const T* const* static_list() { return static_ptr_list_; }
  constexpr T** dynamic_list() { return dynamic_ptr_list_; }
#endif  // RAW_FORMATS_POINTER_LIST_TEST

  constexpr const T* at(uint64_t index) const {
    if (index >= size_)
      return nullptr;
    if (owns_memory_) {
      return dynamic_ptr_list_[index];
    }
    return static_ptr_list_[index];
  }

  constexpr const T* operator[](uint64_t index) const { return at(index); }

  // Push a new pointer into the list. This object assumes responsibility for calling delete on the
  // pointer.
  constexpr bool push_back(T* ptr) {
    if (!owns_memory_ || size_ + 1 > capacity_)
      return false;

    dynamic_ptr_list_[size_] = ptr;
    size_ += 1;
    return true;
  }

  // Constructs a new heap allocated object with the given arguments and stores the pointer in
  // the list, increasing the size by 1.
  template <typename U, typename... Args>
    requires std::same_as<U, T> || std::derived_from<U, T>
  constexpr bool emplace_back(Args&&... args) {
    if (!owns_memory_ || size_ + 1 > capacity_)
      return false;

    dynamic_ptr_list_[size_] = new U(std::forward<Args>(args)...);
    size_ += 1;
    return true;
  }

 private:
  bool owns_memory_;
  // capacity_ is the maximum number of pointers that can be stored in the array pointed to by
  // either static_ptr_list_ or dynamic_ptr_list_. size_ is the number of pointers that are
  // actually stored in the array. The only way these can be different is if the contents are
  // dynamically allocated (owns_memory_ is true and dynamic_ptr_list_ is in use), and they are
  // typically expected to wind up the same once the list is fully initialized with push_back
  // and/or emplace_back.
  uint64_t capacity_;
  uint64_t size_;
  // A value of 0 indicates the pointers in the stored list are to scalar values. A value of 1 or
  // more indicates that the stored pointers are pointers to arrays of that size.
  uint64_t element_array_size_;

  const T* const* static_ptr_list_;
  T** dynamic_ptr_list_;
};

}  // namespace camera::raw

#endif  // SRC_CAMERA_LIB_RAW_FORMATS_POINTER_LIST_H_
