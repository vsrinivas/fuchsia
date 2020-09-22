// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_INTRUSIVE_LIST_H_
#define SRC_UI_LIB_ESCHER_UTIL_INTRUSIVE_LIST_H_

#ifndef NDEBUG
#include <lib/syslog/cpp/macros.h>
#endif

namespace escher {

// Base class for items that can be stored in an IntrusiveList.
//
// TODO(fxbug.dev/23915): A fancier implementation would make next/prev private.  Until
// then, we trust Escher clients to not frob these pointers.
template <typename T>
struct IntrusiveListItem {
  T* prev = nullptr;
  T* next = nullptr;

#ifndef NDEBUG
  void* list = nullptr;
#endif
};

template <typename T>
class IntrusiveList {
 public:
  ~IntrusiveList() { Clear(); }

  void Clear() {
    IntrusiveListItem<T>* current = head_;
    while (current) {
#ifndef NDEBUG
      FX_DCHECK(current->list == this);
      current->list = nullptr;
#endif
      IntrusiveListItem<T>* next = current->next;
      current->next = nullptr;
      current->prev = nullptr;
      current = next;
    }
    head_ = nullptr;
  }

  class Iterator {
   public:
    friend class IntrusiveList<T>;
    Iterator(T* item) : item_(item) {}

    Iterator() = default;

    explicit operator bool() const { return item_ != nullptr; }

    bool operator==(const Iterator& other) const { return item_ == other.item_; }
    bool operator!=(const Iterator& other) const { return item_ != other.item_; }

    T& operator*() { return *static_cast<T*>(item_); }
    const T& operator*() const { return *static_cast<T*>(item_); }

    T* get() { return static_cast<T*>(item_); }
    const T* get() const { return static_cast<T*>(item_); }

    T* operator->() { return get(); }
    const T* operator->() const { return get(); }

    // Pre-increment.
    Iterator operator++() {
      item_ = item_->next;
      return *this;
    }

    // Post-increment.
    Iterator operator++(int) {
      Iterator result = *this;
      ++*this;
      return result;
    }

   private:
    IntrusiveListItem<T>* item_ = nullptr;
  };

  Iterator begin() { return Iterator(head_); }

  Iterator end() { return Iterator(); }

  bool IsEmpty() const { return head_ == nullptr; }

  // Return iterator following the last removed item.
  Iterator Erase(Iterator it) {
    auto item = it.get();
    return Iterator(item ? Erase(item) : nullptr);
  }

  // Return the list item that follows the erased item.
  T* Erase(T* item) {
#ifndef NDEBUG
    FX_DCHECK(item);
    FX_DCHECK(item->list == this);
    item->list = nullptr;
#endif

    auto* next = item->next;
    auto* prev = item->prev;

    if (prev) {
      prev->next = next;
    } else {
      head_ = next;
    }

    if (next) {
      next->prev = prev;
    }

    item->next = item->prev = nullptr;

    return next;
  }

  // If the list is not empty, erase and return the first item.  Otherwise,
  // return nullptr.
  T* PopFront() {
    if (head_) {
      auto result = head_;
      Erase(head_);
      return result;
    } else {
      return nullptr;
    }
  }

  void InsertFront(Iterator it) {
    auto* item = it.get();
    if (head_)
      head_->prev = item;

    item->next = head_;
    item->prev = nullptr;
    head_ = item;

#ifndef NDEBUG
    FX_DCHECK(item->list == nullptr);
    item->list = this;
#endif
  }

  void MoveToFront(IntrusiveList<T>& other, Iterator it) {
    other.Erase(it);
    InsertFront(it);
  }

 private:
  T* head_ = nullptr;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_INTRUSIVE_LIST_H_
