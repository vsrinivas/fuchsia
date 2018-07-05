// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_OBJECT_POOL_H_
#define LIB_ESCHER_UTIL_OBJECT_POOL_H_

#include <utility>
#include <vector>

#include "lib/fxl/logging.h"

namespace escher {

// Default policy for constructing and destroying ObjectPool objects.  Each
// object is constructed and destroyed one-by-one.
//
// When replacing this policy with a different one, clients are free to do
// whatever they want as long as:
// - all 4 of these methods exist, since ObjectPool calls them.
// - a constructor is called before an allocated object is returned
// - a destructor is called before the pool is cleared/destroyed.
// - there is no double construction/destruction
template <typename T>
class DefaultObjectPoolPolicy {
 public:
  // Default construction policy is to use placement-new.
  template <typename... Args>
  inline void InitializePoolObject(T* ptr, Args&&... args) {
    new (ptr) T(std::forward<Args>(args)...);
  }

  // Default destruction policy is to invoke the destructor in-place.
  inline void DestroyPoolObject(T* ptr) { ptr->~T(); }

  // Default block initialization policy is to do nothing; each object is
  // constructed one-by-one via InitializePoolObject().
  inline void InitializePoolObjectBlock(T* objects, size_t block_index,
                                        size_t num_objects) {}

  // Default block destruction policy is to do nothing; each object is destroyed
  // one-by-one via DestroyPoolObject().
  inline void DestroyPoolObjectBlock(T* objects, size_t block_index,
                                     size_t num_objects) {}
};

// An ObjectPool is an allocator for objects of type T.  The underlying memory
// is allocated in contiguous chunks.  The default policy is to construct the
// objects as they are allocated (via InitializePoolObject()) and destroy them
// as they are freed (via DestroyPoolObject()).  However, some objects such as
// Vulkan descriptor sets must be allocated in batches.  For these cases, the
// ObjectPool can be parameterized with a different PolicyT object.
template <typename T, typename PolicyT = DefaultObjectPoolPolicy<T>>
class ObjectPool {
 public:
  template <typename... Args>
  ObjectPool(Args&&... args) : policy_(std::forward<Args>(args)...) {}
  ~ObjectPool() { Clear(); }

  // Allocate an object from the pool, constructing it with the specified
  // arguments.
  template <typename... Args>
  T* Allocate(Args&&... args) {
    if (vacants_.empty()) {
      AllocateBlock();
    }
    T* ptr = vacants_.back();
    vacants_.pop_back();

    policy_.InitializePoolObject(ptr, std::forward<Args>(args)...);
    return ptr;
  }

  // Free the object, releasing it back to the pool for subsequent re-use.
  void Free(T* ptr) {
    policy_.DestroyPoolObject(ptr);
    vacants_.push_back(ptr);
  }

  // Return the number of objects that can be held in the initial block
  // allocation.
  static size_t InitialBlockSize() { return 64U; }

  // Return the number of objects that can be held in the "block_index-th"
  // allocation.
  static size_t NumObjectsInBlock(size_t block_index) {
    return InitialBlockSize() << block_index;
  }

  // Total number of objects that can be allocated from the pool without
  // changing the amount of underlying memory.
  size_t GetCapacity() const {
    size_t num_objects = 0;
    const size_t num_blocks = blocks_.size();
    for (size_t i = 0; i < num_blocks; ++i) {
      num_objects += NumObjectsInBlock(i);
    }
    return num_objects;
  }

  // Return the number of objects that have been allocated but not freed.
  size_t UnfreedObjectCount() const { return GetCapacity() - vacants_.size(); }

  // Release all pool resources.  Illegal to call while there are still unfreed
  // objects.  ObjectPool only releases memory when Clear() is called.
  void Clear() {
    FXL_DCHECK(UnfreedObjectCount() == 0);

    const size_t num_blocks = blocks_.size();
    for (size_t i = 0; i < num_blocks; ++i) {
      const size_t num_objects = NumObjectsInBlock(i);
      policy_.DestroyPoolObjectBlock(blocks_[i].get(), i, num_objects);
    }

    vacants_.clear();
    blocks_.clear();
  }

  using PolicyType = PolicyT;
  const PolicyType& policy() const { return policy_; }

 private:
  // Allocate a new block of objects, and add them all to |vacants_|.  Called
  // by Allocate() when |vacants_| is empty.
  void AllocateBlock() {
    const size_t block_index = blocks_.size();
    const size_t num_objects = NumObjectsInBlock(block_index);
    T* ptr = static_cast<T*>(malloc(num_objects * sizeof(T)));
    blocks_.emplace_back(ptr);
    policy_.InitializePoolObjectBlock(ptr, block_index, num_objects);
    vacants_.reserve(vacants_.capacity() + num_objects);
    for (size_t i = 0; i < num_objects; ++i) {
      vacants_.push_back(ptr + i);
    }
  }

  struct MallocDeleter {
    void operator()(T* ptr) { ::free(ptr); }
  };

  PolicyT policy_;
  std::vector<T*> vacants_;
  std::vector<std::unique_ptr<T, MallocDeleter>> blocks_;
};

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_OBJECT_POOL_H_
