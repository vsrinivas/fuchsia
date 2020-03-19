// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_VMO_STORE_GROWABLE_SLAB_H_
#define SRC_LIB_VMO_STORE_GROWABLE_SLAB_H_

#include <lib/fit/optional.h>
#include <zircon/status.h>

#include <limits>

#include <fbl/alloc_checker.h>
#include <fbl/vector.h>

namespace vmo_store {

// A slab data structure to store items of type `T` keyed by `KeyType`.
//
// `KeyType` must be an integer type, it is used to index an underlying vector storage of `T`.
//
// `GrowableSlab` is always created with zero capacity and can grow in capacity up to the maximum
// namespace of `std::numeric_limits<KeyType>::max()-1`.
//
// `GrowableSlab` acts as a container for `T` with O(1) guarantee on `Push`, `Insert`, `Get`, and
// `Erase` operations, and O(capacity) on `Grow`.
//
// This structure is not thread-safe.
template <typename T, typename KeyType = size_t>
class GrowableSlab {
 public:
  static_assert(!std::numeric_limits<KeyType>::is_signed);
  static_assert(std::numeric_limits<KeyType>::is_integer);

  class Iterator;

  GrowableSlab() = default;

  // Returns the currently allocated capacity of the slab.
  KeyType capacity() const { return static_cast<KeyType>(slots_.size()); }
  // Returns the number of items held by the slab.
  KeyType count() const { return used_; }
  // Returns the number of free slots available on the slab.
  KeyType free() const { return capacity() - count(); }

  bool is_empty() const { return used_ == 0; }

  // Grows the slab by a fixed factor if there are no more free slots.
  //
  // Note that the worst-case complexity for `Grow` is O(new_capacity).
  //
  // Returns `ZX_ERR_NO_MEMORY` if the extra capacity could not be allocated.
  [[nodiscard]] zx_status_t Grow() {
    if (free() == 0) {
      return GrowTo(capacity() == 0 ? 1 : capacity() * 2);
    }
    return ZX_OK;
  }

  // Grows the slab to `capacity`.
  //
  // Note that the worst-case complexity for `GrowTo` is O(capacity).
  //
  // Returns `ZX_ERR_NO_MEMORY` if the extra capacity could not be allocated.
  [[nodiscard]] zx_status_t GrowTo(KeyType capacity) {
    fbl::AllocChecker ac;
    GrowTo(capacity, &ac);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    return ZX_OK;
  }

  // Inserts `value` on the slab, using a key from the available pool.
  // Returns a valid `KeyType` if there was an available slot to put `value` in.
  [[nodiscard]] fit::optional<KeyType> Push(T&& value) {
    KeyType key = free_list_.head;
    if (Insert(key, std::move(value)) != ZX_OK) {
      return fit::optional<KeyType>();
    }
    return key;
  }

  // Attempts to insert `value` at slot `key` in the slab.
  // Returns `ZX_ERR_OUT_OF_RANGE` if `key` is not in the valid namespace.
  // Returns `ZX_ERR_ALREADY_EXISTS` if `key` is already occupied by another value.
  [[nodiscard]] zx_status_t Insert(KeyType key, T&& value) {
    if (key >= static_cast<KeyType>(slots_.size())) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    auto* slot = &slots_[key];
    if (slot->value.has_value()) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    slot->value.emplace(std::move(value));
    ListRemove(&free_list_, key);
    ListInsert(&used_list_, key);

    used_++;
    return ZX_OK;
  }

  // Gets the value `T` stored at `key`.
  // Returns `nullptr` if `key` is invalid or the slot is not occupied.
  T* Get(KeyType key) {
    auto* slot = GetOccupiedSlot(key);
    if (!slot) {
      return nullptr;
    }
    return &slot->value.value();
  }

  // Erases the value at `key`, freeing the slot and returning the stored value.
  // `Erase` returns a valid `T` if `key` pointed to an occupied slot.
  fit::optional<T> Erase(KeyType key) {
    auto* slot = GetOccupiedSlot(key);
    if (!slot) {
      return fit::optional<T>();
    }
    fit::optional<T> ret(std::move(slot->value.value()));
    slot->value.reset();

    ListRemove(&used_list_, key);
    ListInsert(&free_list_, key);

    used_--;
    return ret;
  }

  // Removes all currently stored values from the slab, returning all slots to the free-list.
  void Clear() {
    while (used_ != 0) {
      Erase(used_list_.head);
    }
  }

  Iterator begin() const { return Iterator(this); }

  Iterator end() const { return Iterator(); }

  struct Slot {
    Slot() : next(kSentinel), prev(kSentinel) {}

    Slot(Slot&& other) noexcept = default;
    Slot(const Slot& other) = delete;
    KeyType next;
    KeyType prev;
    fit::optional<T> value;
  };

  class Iterator {
   public:
    Iterator() = default;

    explicit Iterator(const GrowableSlab* parent)
        : parent_(parent), index_(parent->used_list_.head) {}

    Iterator(const Iterator& other) : parent_(other.parent), index_(other.index) {}

    Iterator& operator=(const Iterator& other) {
      parent_ = other.parent_;
      index_ = other.index_;
      return *this;
    }

    bool IsValid() const { return parent_ != nullptr && index_ != kSentinel; }

    bool operator==(const Iterator& other) const { return index_ == other.index_; }

    bool operator!=(const Iterator& other) const { return index_ != other.index_; }

    // Prefix
    Iterator& operator++() {
      if (IsValid()) {
        index_ = parent_->slots_[index_].next;
      }

      return *this;
    }

    // Postfix
    Iterator operator++(int) {
      Iterator ret(*this);
      ++(*this);
      return ret;
    }

    T& operator*() const {
      ZX_DEBUG_ASSERT(IsValid());
      return parent_->slots_[index_].value.value();
    }

    T* operator->() const {
      ZX_DEBUG_ASSERT(IsValid());
      return &parent_->slots_[index_].value.value();
    }

   private:
    Iterator(GrowableSlab* parent, size_t index) : parent_(parent), index_(index) {}

    // Pointer to parent slab, not owned.
    const GrowableSlab<T, KeyType>* parent_ = nullptr;
    KeyType index_ = kSentinel;
  };

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(GrowableSlab);

 private:
  static constexpr KeyType kSentinel = std::numeric_limits<KeyType>::max();
  // We keep two `List`s in `GrowableSlab`: a list of free slots in the vector, and a list of used
  // slots which contains valid stored `T`s.
  // The list itself is defined by head and tail indices, and each node contains the next and
  // previous node indices (see `Slot`). This index-based implementation is used instead of the fbl
  // DLL implementation so we can `Grow` the underlying fbl::Vector without having to update any
  // pointers - the fbl DLL is based on pointers and growing the fbl::Vector may cause things to
  // move around in memory.
  struct List {
    KeyType head;
    KeyType tail;
  };

  inline void ListRemove(List* list, KeyType key) {
    auto* slot = &slots_[key];
    if (slot->prev != kSentinel) {
      slots_[slot->prev].next = slot->next;
    }
    if (slot->next != kSentinel) {
      slots_[slot->next].prev = slot->prev;
    }
    if (list->head == key) {
      list->head = slot->next;
    }
    if (list->tail == key) {
      list->tail = slot->prev;
    }
    slot->next = kSentinel;
    slot->prev = kSentinel;
  }

  inline void ListInsert(List* list, KeyType key) {
    auto* slot = &slots_[key];
    ZX_DEBUG_ASSERT(slot->prev == kSentinel);
    ZX_DEBUG_ASSERT(slot->next == kSentinel);
    if (list->tail != kSentinel) {
      slots_[list->tail].next = key;
    }
    slot->prev = list->tail;
    list->tail = key;
    if (slot->prev == kSentinel) {
      list->head = key;
    }
  }

  void GrowTo(KeyType capacity, fbl::AllocChecker* ac) {
    auto before = static_cast<KeyType>(slots_.size());
    if (capacity <= before) {
      ac->arm(0, true);
      return;
    }
    slots_.reserve(capacity, ac);
    while (slots_.size() != slots_.capacity()) {
      Slot slot;
      auto key = static_cast<KeyType>(slots_.size());
      slots_.push_back(std::move(slot));
      ListInsert(&free_list_, key);
    }
  }

  Slot* GetOccupiedSlot(KeyType key) {
    if (key >= slots_.size()) {
      return nullptr;
    }
    auto* slot = &slots_[key];
    if (!slot->value.has_value()) {
      return nullptr;
    }
    return slot;
  }

  List used_list_ = {kSentinel, kSentinel};
  List free_list_ = {kSentinel, kSentinel};
  KeyType used_ = 0;
  fbl::Vector<Slot> slots_;
};

}  // namespace vmo_store

#endif  // SRC_LIB_VMO_STORE_GROWABLE_SLAB_H_
