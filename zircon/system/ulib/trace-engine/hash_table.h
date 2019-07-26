// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements a non-allocating hashtable for |StringEntry| and
// |ThreadEntry|. "Non-allocating" here means that no allocations are done
// while the engine is running, instead all needed space is allocated when
// the engine is initialized.
// This implementation is derived from fbl::SinglyLinkedListable and
// flb::HashTable.

#ifndef ZIRCON_SYSTEM_ULIB_TRACE_ENGINE_HASH_TABLE_H_
#define ZIRCON_SYSTEM_ULIB_TRACE_ENGINE_HASH_TABLE_H_

#include <stddef.h>
#include <zircon/assert.h>

#include <utility>

namespace trace {
namespace internal {

// Minimal version of |fbl::SinglyLinkedListable|.
// |_NodeType| is the object we are contained in.
// This only works with raw pointers, so we make it explicit.
template <typename _NodeType>
struct SinglyLinkedListable {
 public:
  bool InContainer() const { return next_ != nullptr; }

  using NodeType = _NodeType;
  NodeType* next_ = nullptr;
};

// Minimal version of |fbl::SinglyLinkedList| specifically for our needs.
// This only works with raw pointers, so we make it explicit.
template <typename _NodeType>
class SinglyLinkedList {
 public:
  using NodeType = _NodeType;
  using PtrType = NodeType*;

  // Default construction gives an empty list.
  constexpr SinglyLinkedList() {}

  ~SinglyLinkedList() {
    // It is considered an error to allow a list of unmanaged pointers to
    // destruct of there are still elements in it.
    ZX_DEBUG_ASSERT(is_empty());
    clear();
  }

  SinglyLinkedList(const SinglyLinkedList&) = delete;
  SinglyLinkedList(SinglyLinkedList&&) = delete;
  SinglyLinkedList& operator=(const SinglyLinkedList&) = delete;
  SinglyLinkedList& operator=(SinglyLinkedList&&) = delete;

  PtrType head() const { return head_; }

  bool is_empty() const {
    ZX_DEBUG_ASSERT(head_ != nullptr);
    return is_sentinel_ptr(head_);
  }

  void clear() {
    while (!is_empty()) {
      PtrType tmp = head_;
      head_ = head_->next_;
      tmp->next_ = nullptr;
    }
  }

  void push_front(PtrType ptr) {
    ZX_DEBUG_ASSERT(ptr != nullptr);
    ZX_DEBUG_ASSERT(!ptr->InContainer());
    ptr->next_ = head_;
    head_ = ptr;
  }

  static constexpr bool is_sentinel_ptr(PtrType ptr) {
    return (reinterpret_cast<uintptr_t>(ptr) & kContainerSentinelBit) != 0;
  }

 private:
  // Use a different value to mark the end of the list so that we can
  // distinguish last-entry-in-list vs entry-not-in-list.
  static constexpr uintptr_t kContainerSentinelBit = 1U;
  static constexpr PtrType sentinel() { return reinterpret_cast<PtrType>(kContainerSentinelBit); }

  // State consists of just a head pointer.
  PtrType head_ = sentinel();
};

// A note on choosing the value here: There are two hash tables for each thread
// in the process, one for |StringEntry| and one for |ThreadEntry|.
static constexpr size_t kDefaultNumBuckets = 37U;

// Minimal version of |fbl::HashTable| specifically for our needs.
// |_KeyType| is compared with ==.
// |_NodeType| is an object of type |SinglyLinkedListable|.
template <typename _KeyType, typename _NodeType, size_t _NumBuckets = kDefaultNumBuckets>
class HashTable {
 public:
  using KeyType = _KeyType;
  using NodeType = _NodeType;
  using PtrType = NodeType*;
  using HashType = size_t;
  using BucketType = SinglyLinkedList<NodeType>;
  static constexpr HashType kNumBuckets = _NumBuckets;

  constexpr HashTable() {}
  ~HashTable() { ZX_DEBUG_ASSERT(is_empty()); }

  HashTable(const HashTable&) = delete;
  HashTable(HashTable&&) = delete;
  HashTable& operator=(const HashTable&) = delete;
  HashTable& operator=(HashTable&&) = delete;

  size_t size() const { return count_; }
  bool is_empty() const { return count_ == 0; }

  void clear() {
    for (auto& e : buckets_)
      e.clear();
    count_ = 0;
  }

  void insert(PtrType ptr) {
    ZX_DEBUG_ASSERT(ptr != nullptr);
    KeyType key = ptr->GetKey();
    BucketType& bucket = GetBucket(key);

    // Duplicate keys are disallowed.  Debug assert if someone tries to to
    // insert an element with a duplicate key.
    ZX_DEBUG_ASSERT(FindInBucket(bucket, key) == nullptr);

    bucket.push_front(ptr);
    ++count_;
  }

  // This is not called |find()| because it behaves differently than std
  // container |find()|, it returns a pointer to the element or nullptr if
  // not found.
  PtrType lookup(const KeyType& key) { return FindInBucket(GetBucket(key), key); }

 private:
  static HashType GetHash(const KeyType& key) { return NodeType::GetHash(key); }

  BucketType& GetBucket(const KeyType& key) { return buckets_[GetHash(key) % kNumBuckets]; }

  static PtrType FindInBucket(const BucketType& bucket, const KeyType& key) {
    for (PtrType p = bucket.head(); !BucketType::is_sentinel_ptr(p); p = p->next_) {
      if (p->GetKey() == key) {
        return p;
      }
    }
    return nullptr;
  }

  size_t count_ = 0UL;
  BucketType buckets_[kNumBuckets];
};

}  // namespace internal
}  // namespace trace

#endif  // ZIRCON_SYSTEM_ULIB_TRACE_ENGINE_HASH_TABLE_H_
