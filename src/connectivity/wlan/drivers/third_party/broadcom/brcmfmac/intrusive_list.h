// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_INTRUSIVE_LIST_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_INTRUSIVE_LIST_H_

// intrusive_listable<> and intrusive_list<> implement a doubly-linked list with intrusive link
// pointers; that is, the link pointers are part of the object allocation and so list operations do
// not require an additional allocation for a node type (compare with std::list<>).  This
// implementation also ensures the useful invariant that the link pointers are always valid:
//
// * An empty list has link pointers that point to itself.  This is defined as the empty state.
// * If a listable is not on any list, then it is in the empty state.
// * When a listable is destroyed, it is automatically removed from any list it is on (and any list
//   it was on remains afterwards in a valid state).
// * When a list is destroyed, all its member listables are reset to the empty state.
//
// This invariant does come with a few notable trade-offs:
//
// * The list is not thread-safe to mutation (though iterators to unmutated elements remain valid).
//   Note that mutation of the list may occur independently of the list API; in particular when a
//   listable that is on the list is destroyed, the listable self-removes from the list.
// * Operations such as size() and clear() are O(n) instead of O(1), as:
//   * Listables may be independently removed, so the list keeps no size count.
//   * Clearing the list requires each listable therein to be individually reset to the empty state.

#include <iterator>
#include <type_traits>
#include <utility>

namespace wlan {
namespace brcmfmac {

template <typename T, typename TagType>
class intrusive_list;

// Listable type for an intrusive_list<>.  Subclasses of intrusive_listable<> can be added to an
// intrusive_list<>; TagType (which defaults void) enables tag dispatching when an object should be
// listable in multiple types of lists.
template <typename TagType = void>
class intrusive_listable {
 public:
  intrusive_listable() = default;
  intrusive_listable(const intrusive_listable& other) = delete;
  intrusive_listable(intrusive_listable&& other) { *this = std::move(other); }
  intrusive_listable& operator=(intrusive_listable&& other) noexcept {
    erase();
    if (!other.empty()) {
      prev_ = other.prev_;
      next_ = other.next_;
      prev_->next_ = this;
      next_->prev_ = this;
      other.prev_ = &other;
      other.next_ = &other;
    }
    return *this;
  }
  ~intrusive_listable() {
    prev_->next_ = next_;
    next_->prev_ = prev_;
  }

  friend void swap(intrusive_listable& lhs, intrusive_listable& rhs) noexcept {
    intrusive_listable* const lhs_prev = lhs.prev_;
    intrusive_listable* const lhs_next = lhs.next_;
    if (rhs.empty()) {
      // rhs is empty.
      lhs.prev_ = &lhs;
      lhs.next_ = &lhs;
    } else {
      // rhs is not empty.
      lhs.prev_ = rhs.prev_;
      lhs.next_ = rhs.next_;
      lhs.prev_->next_ = &lhs;
      lhs.next_->prev_ = &lhs;
    }
    if (lhs_prev == &lhs) {
      // lhs was empty.
      rhs.prev_ = &rhs;
      rhs.next_ = &rhs;
    } else {
      // lhs was not empty.
      rhs.prev_ = lhs_prev;
      rhs.next_ = lhs_next;
      rhs.prev_->next_ = &rhs;
      rhs.next_->prev_ = &rhs;
    }
  }

  // Returns true iff this instance is not on any list.
  bool empty() const noexcept { return prev_ == this; }

  // Erase this instance from any list it may be currently on.  Safe to call even if the instance is
  // not on a list.
  void erase() noexcept {
    prev_->next_ = next_;
    next_->prev_ = prev_;
    prev_ = this;
    next_ = this;
  }

 private:
  template <typename T, typename U>
  friend class intrusive_list;

  intrusive_listable* prev_ = this;
  intrusive_listable* next_ = this;
};

// A list of intrusive_listable<> instances of the same tag type.
template <typename T, typename TagType = void>
class intrusive_list {
 private:
  // Base iterator type definition.
  template <typename U, bool kIsForwardIterator = true>
  class iterator_t : public std::iterator<std::bidirectional_iterator_tag, U> {
   public:
    iterator_t() = default;
    iterator_t(const iterator_t& other) : link_(other.link_) {}
    iterator_t(iterator_t&& other) { swap(*this, other); }
    iterator_t& operator=(iterator_t other) noexcept {
      swap(*this, other);
      return *this;
    }
    friend void swap(iterator_t& lhs, iterator_t& rhs) noexcept {
      using std::swap;
      swap(lhs.link_, rhs.link_);
    }
    ~iterator_t() = default;
    operator iterator_t<const U, kIsForwardIterator>() const noexcept {
      return iterator_t<const U, kIsForwardIterator>(link_);
    };

    bool operator==(const iterator_t& other) const noexcept { return link_ == other.link_; }
    bool operator!=(const iterator_t& other) const noexcept { return link_ != other.link_; }

    U& operator*() { return static_cast<U&>(*link_); }
    iterator_t& operator++() noexcept {
      link_ = kIsForwardIterator ? link_->next_ : link_->prev_;
      return *this;
    }
    iterator_t& operator--() noexcept {
      link_ = kIsForwardIterator ? link_->prev_ : link_->next_;
      return *this;
    }
    iterator_t operator++(int) noexcept {
      iterator_t ret(link_);
      link_ = kIsForwardIterator ? link_->next_ : link_->prev_;
      return ret;
    }
    iterator_t operator--(int) noexcept {
      iterator_t ret(link_);
      link_ = kIsForwardIterator ? link_->prev_ : link_->next_;
      return ret;
    }

   private:
    using link_type = intrusive_listable<TagType>;
    friend class intrusive_list;

    explicit iterator_t(link_type* link) : link_(link) {}

    link_type* link_ = nullptr;
  };

 public:
  using value_type = T;
  using iterator = iterator_t<T, true>;
  using const_iterator = iterator_t<const T, true>;
  using reverse_iterator = iterator_t<T, false>;
  using const_reverse_iterator = iterator_t<const T, false>;

  intrusive_list() {
    static_assert(std::is_base_of<link_type, T>::value,
                  "value type T must derive from intrusive_listable<TagType>");
  };
  intrusive_list(const intrusive_list& other) = delete;
  intrusive_list(intrusive_list&& other) { link_ = std::move(other.link_); }
  intrusive_list& operator=(intrusive_list&& other) noexcept {
    link_ = std::move(other.link_);
    return *this;
  }
  ~intrusive_list() = default;

  friend void swap(intrusive_list& lhs, intrusive_list& rhs) noexcept {
    using std::swap;
    swap(lhs.link_, rhs.link_);
  }

  bool empty() const noexcept { return link_.empty(); }

  // Note: this is O(n).
  size_t size() const noexcept {
    size_t size = 0;
    link_type* link = link_.next_;
    while (link != &link_) {
      ++size;
      link = link->next_;
    }
    return size;
  }

  iterator begin() noexcept { return iterator(link_.next_); }
  const_iterator begin() const noexcept { return cbegin(); }
  const_iterator cbegin() const noexcept {
    return const_iterator(const_cast<link_type*>(link_.next_));
  }
  iterator end() noexcept { return iterator(&link_); }
  const_iterator end() const noexcept { return cend(); }
  const_iterator cend() const noexcept { return const_iterator(const_cast<link_type*>(&link_)); }

  reverse_iterator rbegin() noexcept { return reverse_iterator(link_.prev_); }
  const_reverse_iterator rbegin() const noexcept { return crbegin(); }
  const_reverse_iterator crbegin() const noexcept {
    return const_reverse_iterator(const_cast<link_type*>(link_.prev_));
  }
  reverse_iterator rend() noexcept { return reverse_iterator(&link_); }
  const_reverse_iterator rend() const noexcept { return crend(); }
  const_reverse_iterator crend() const noexcept {
    return const_reverse_iterator(const_cast<link_type*>(&link_));
  }

  T& front() { return static_cast<T&>(*link_.next_); }
  const T& front() const { return static_cast<const T&>(*link_.next_); }
  T& back() { return static_cast<T&>(*link_.prev_); }
  const T& back() const { return static_cast<const T&>(*link_.prev_); }

  // Note that these insertion operations take the element by mutable reference, and there are no
  // emplace_* equivalents, precisely because this is an intrusive list.
  void push_front(T& value) noexcept { insert(const_iterator(link_.next_), value); }
  void push_back(T& value) noexcept { insert(const_iterator(&link_), value); }
  iterator insert(const_iterator pos, T& value) noexcept {
    auto& new_link = static_cast<link_type&>(value);
    new_link.erase();
    new_link.prev_ = pos.link_->prev_;
    new_link.next_ = pos.link_;
    pos.link_->prev_->next_ = &new_link;
    pos.link_->prev_ = &new_link;
    return iterator(&new_link);
  }

  template <typename InputIter>
  iterator insert(const_iterator pos, InputIter first, InputIter last) {
    if (first == last) {
      return iterator(pos.link_);
    }
    link_type* prev = pos.link_->prev_;
    InputIter iter = first;
    do {
      link_type* const iter_link = static_cast<link_type*>(&(*iter));
      iter_link->erase();
      iter_link->prev_ = prev;
      prev->next_ = iter_link;
      ++iter;
      prev = iter_link;
    } while (iter != last);

    prev->next_ = pos.link_;
    pos.link_->prev_ = prev;
    return iterator(static_cast<link_type*>(&(*first)));
  }
  void splice(const_iterator pos, intrusive_list&& other) noexcept {
    if (other.empty()) {
      return;
    }
    other.link_.prev_->next_ = pos.link_;
    other.link_.next_->prev_ = pos.link_->prev_;
    pos.link_->prev_->next_ = other.link_.next_;
    pos.link_->prev_ = other.link_.prev_;
    other.link_.prev_ = &other.link_;
    other.link_.next_ = &other.link_;
  }

  void pop_front() { erase(const_iterator(link_.next_)); }
  void pop_back() { erase(const_iterator(link_.prev_)); }
  iterator erase(const_iterator pos) noexcept {
    iterator ret(pos.link_->next_);
    pos.link_->erase();
    return ret;
  }
  iterator erase(const_iterator first, const_iterator last) noexcept {
    link_type* link = first.link_;
    link_type* const last_link = last.link_;
    if (link == last_link) {
      return iterator(last_link);
    }
    link->prev_->next_ = last_link;
    last_link->prev_ = link->prev_;
    do {
      link_type* const todel = link;
      link = link->next_;
      todel->prev_ = todel;
      todel->next_ = todel;
    } while (link != last_link);
    return iterator(last_link);
  }

  // Note: this is O(n).
  void clear() noexcept { erase(cbegin(), cend()); }

  // Used for unit testing, purposely declared but not defined here.
  void intrusive_list_validate() const noexcept;

 private:
  using link_type = intrusive_listable<TagType>;

  link_type link_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_INTRUSIVE_LIST_H_
