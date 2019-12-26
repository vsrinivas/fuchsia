// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_INTRUSIVE_DOUBLE_LIST_H_
#define FBL_INTRUSIVE_DOUBLE_LIST_H_

#include <zircon/assert.h>

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/intrusive_container_utils.h>
#include <fbl/intrusive_pointer_traits.h>

// Usage and Implementation Notes:
//
// fbl::DoublyLinkedList<> is a templated intrusive container class which
// allows users to manage doubly linked lists of objects.
//
// fbl::DoublyLinkedList<> follows the same patterns as
// fbl::SinglyLinkedList<> and implements a superset of the functionality
// (including support for managed pointer types).  Please refer to the "Usage
// Notes" section of fbl/intrusive_single_list.h for details.
//
// Additional functionality provided by a DoublyLinkedList<> includes...
// ++ O(k) push_back/pop_back/back (in addition to push_front/pop_front/front)
// ++ The ability to "insert" in addition to "insert_after"
// ++ The ability to "erase" in addition to "erase_next"
// ++ Support for bidirectional iteration.
//
// Under the hood, the state of a DoublyLinkedList<> contains a single raw
// pointer to the object which is the head of the list, or nullptr if the list
// is empty.  Each object on the list has a DoublyLinkedListNodeState<> which
// contains one raw pointer (prev) and one managed pointer (next) which are
// arranged in a ring.  The tail of a non-empty list can be found in O(k) time
// by following the prev pointer of the head node of the list.
namespace fbl {

// Fwd decl of sanity checker class used by tests.
namespace tests {
namespace intrusive_containers {
class DoublyLinkedListChecker;
template <typename>
class SequenceContainerTestEnvironment;
}  // namespace intrusive_containers
}  // namespace tests

template <typename T>
struct DoublyLinkedListNodeState {
  using PtrTraits = internal::ContainerPtrTraits<T>;
  constexpr DoublyLinkedListNodeState() {}
  ~DoublyLinkedListNodeState() {
    // Note; this ASSERT can only be enforced for lists made of managed
    // pointer types.  Lists formed from unmanaged pointers can potentially
    // leave next_ in a non-null state during destruction if the list is
    // cleared using "clear_unsafe".
    ZX_DEBUG_ASSERT(IsValid() && (!PtrTraits::IsManaged || !InContainer()));
  }

  bool IsValid() const { return ((next_ == nullptr) == (prev_ == nullptr)); }
  bool InContainer() const { return (next_ != nullptr); }

 private:
  template <typename, typename, typename, SizeOrder>
  friend class DoublyLinkedList;
  template <typename>
  friend class tests::intrusive_containers::SequenceContainerTestEnvironment;
  friend class tests::intrusive_containers::DoublyLinkedListChecker;

  typename PtrTraits::RawPtrType next_ = nullptr;
  typename PtrTraits::RawPtrType prev_ = nullptr;
};

template <typename T, typename TagType>
struct DoublyLinkedListable;

template <typename T>
struct DefaultDoublyLinkedListTraits {
 private:
  using ValueType = typename internal::ContainerPtrTraits<T>::ValueType;

  template <typename TagType>
  using NodeType = std::conditional_t<internal::has_tag_types_v<ValueType>,
                                      DoublyLinkedListable<T, TagType>, ValueType>;

 public:
  using PtrTraits = internal::ContainerPtrTraits<T>;
  template <typename TagType = DefaultObjectTag>
  static DoublyLinkedListNodeState<T>& node_state(typename PtrTraits::RefType obj) {
    using Node [[maybe_unused]] = NodeType<TagType>;
    return obj.Node::dll_node_state_;
  }
};

template <typename T, typename TagType_ = DefaultObjectTag>
struct DoublyLinkedListable {
 public:
  using TagType = TagType_;
  bool InContainer() const {
    using Node = DoublyLinkedListable<T, TagType>;
    return Node::dll_node_state_.InContainer();
  }

 private:
  friend struct DefaultDoublyLinkedListTraits<T>;
  DoublyLinkedListNodeState<T> dll_node_state_;
};

template <typename T, typename NodeTraits_ = DefaultDoublyLinkedListTraits<T>,
          typename TagType_ = DefaultObjectTag, SizeOrder ListSizeOrder_ = SizeOrder::N>
class DoublyLinkedList : private internal::SizeTracker<ListSizeOrder_> {
 private:
  // Private fwd decls of the iterator implementation.
  template <typename IterTraits>
  class iterator_impl;
  struct iterator_traits;
  struct const_iterator_traits;

  template <typename NodeTraits, typename = void>
  struct AddGenericNodeState;

 public:
  // Aliases used to reduce verbosity and expose types/traits to tests
  static constexpr SizeOrder ListSizeOrder = ListSizeOrder_;
  using PtrTraits = internal::ContainerPtrTraits<T>;
  using NodeTraits = AddGenericNodeState<NodeTraits_>;
  using NodeState = DoublyLinkedListNodeState<T>;
  using PtrType = typename PtrTraits::PtrType;
  using RawPtrType = typename PtrTraits::RawPtrType;
  using RawPtrTraits = internal::ContainerPtrTraits<RawPtrType>;
  using ValueType = typename PtrTraits::ValueType;
  using TagType = TagType_;
  using CheckerType = ::fbl::tests::intrusive_containers::DoublyLinkedListChecker;
  using ContainerType = DoublyLinkedList<T, NodeTraits_, TagType, ListSizeOrder>;

  // Declarations of the standard iterator types.
  using iterator = iterator_impl<iterator_traits>;
  using const_iterator = iterator_impl<const_iterator_traits>;

  // Doubly linked lists support constant order erase (erase using an iterator
  // or direct object reference).
  static constexpr bool SupportsConstantOrderErase = true;
  static constexpr bool SupportsConstantOrderSize = (ListSizeOrder == SizeOrder::Constant);
  static constexpr bool IsAssociative = false;
  static constexpr bool IsSequenced = true;

  // Default construction gives an empty list.
  constexpr DoublyLinkedList() {}

  // Rvalue construction is permitted, but will result in the move of the list
  // contents from one instance of the list to the other (even for unmanaged
  // pointers)
  DoublyLinkedList(DoublyLinkedList&& other_list) noexcept { swap(other_list); }

  // Rvalue assignment is permitted for managed lists, and when the target is
  // an empty list of unmanaged pointers.  Like Rvalue construction, it will
  // result in the move of the source contents to the destination.
  DoublyLinkedList& operator=(DoublyLinkedList&& other_list) {
    ZX_DEBUG_ASSERT(PtrTraits::IsManaged || is_empty());

    clear();
    swap(other_list);

    return *this;
  }

  ~DoublyLinkedList() {
    // It is considered an error to allow a list of unmanaged pointers to
    // destruct if there are still elements in it.  Managed pointer lists
    // will automatically release their references to their elements.
    if (PtrTraits::IsManaged == false) {
      ZX_DEBUG_ASSERT(is_empty());
      if constexpr (SupportsConstantOrderSize) {
        ZX_DEBUG_ASSERT(this->SizeTrackerCount() == 0);
      }
    } else {
      clear();
    }
  }

  // Standard begin/end, cbegin/cend iterator accessors.
  iterator begin() { return iterator(head_); }
  const_iterator begin() const { return const_iterator(head_); }
  const_iterator cbegin() const { return const_iterator(head_); }

  iterator end() { return iterator(sentinel()); }
  const_iterator end() const { return const_iterator(sentinel()); }
  const_iterator cend() const { return const_iterator(sentinel()); }

  // make_iterator : construct an iterator out of a pointer to an object
  iterator make_iterator(ValueType& obj) { return iterator(&obj); }
  const_iterator make_iterator(const ValueType& obj) const {
    return const_iterator(&const_cast<ValueType&>(obj));
  }

  // is_empty : False if the list has at least one element in it, true otherwise.
  bool is_empty() const {
    ZX_DEBUG_ASSERT(head_ != nullptr);
    return internal::is_sentinel_ptr(head_);
  }

  // front
  //
  // Return a reference to the element at the front of the list without
  // removing it.  It is an error to call front on an empty list.
  typename PtrTraits::RefType front() {
    ZX_DEBUG_ASSERT(!is_empty());
    return *head_;
  }
  typename PtrTraits::ConstRefType front() const {
    ZX_DEBUG_ASSERT(!is_empty());
    return *head_;
  }

  // back
  //
  // Return a reference to the element at the back of the list without
  // removing it.  It is an error to call back on an empty list.
  typename PtrTraits::RefType back() {
    ZX_DEBUG_ASSERT(!is_empty());
    return *(NodeTraits::node_state(*head_).prev_);
  }

  typename PtrTraits::ConstRefType back() const {
    ZX_DEBUG_ASSERT(!is_empty());
    return *(NodeTraits::node_state(*head_).prev_);
  }

  // push_front : Push an element onto the front of the list.
  void push_front(const PtrType& ptr) { push_front(PtrType(ptr)); }
  void push_front(PtrType&& ptr) { internal_insert(head_, std::move(ptr)); }

  // push_back : Push an element onto the end of the list.
  void push_back(const PtrType& ptr) { push_back(PtrType(ptr)); }
  void push_back(PtrType&& ptr) { internal_insert(sentinel(), std::move(ptr)); }

  // insert : Insert an element before iter in the list.
  void insert(const iterator& iter, const PtrType& ptr) { insert(iter, PtrType(ptr)); }
  void insert(const iterator& iter, PtrType&& ptr) { internal_insert(iter.node_, std::move(ptr)); }

  void insert(ValueType& before, const PtrType& ptr) { insert(before, PtrType(ptr)); }
  void insert(ValueType& before, PtrType&& ptr) { internal_insert(&before, std::move(ptr)); }

  // splice : Splice another list before iter in this list.
  void splice(const iterator& iter, DoublyLinkedList& other_list) {
    auto before = iter.node_;
    ZX_DEBUG_ASSERT(before != nullptr);
    ZX_DEBUG_ASSERT(head_ != nullptr);

    if (other_list.is_empty()) {
      return;
    }
    if (is_empty()) {
      ZX_DEBUG_ASSERT(before == sentinel());
      ZX_DEBUG_ASSERT(before == head_);
      swap(other_list);
      return;
    }

    // If we are being inserted before the sentinel, the we are the new
    // tail, and the node_state which contains the prev pointer we need to
    // update is head's.  Otherwise, it is the node_state of the node we are
    // being inserted before.
    auto& prev_ns = NodeTraits::node_state(internal::is_sentinel_ptr(before) ? *head_ : *before);
    auto& tgt_prev = prev_ns.prev_;

    // If we are being inserted before the head, then we need to update the
    // head pointer.  Otherwise, we need to update the next pointer of the
    // node which is about to come before us.
    auto& tgt_next = (head_ == before) ? head_
                                       : internal::is_sentinel_ptr(before)
                                             ? NodeTraits::node_state(*tail()).next_
                                             : NodeTraits::node_state(*tgt_prev).next_;

    auto& other_head_ns = NodeTraits::node_state(*other_list.head_);
    auto other_tail = other_list.tail();
    auto& other_tail_ns = NodeTraits::node_state(*other_tail);

    // Update the prev pointers.
    other_head_ns.prev_ = tgt_prev;
    tgt_prev = other_tail;

    // Update the next pointers.
    other_tail_ns.next_ = tgt_next;
    tgt_next = other_list.head_;

    // Mark the other list as being empty now by replacing its head pointer
    // with its sentinel value.
    other_list.head_ = other_list.sentinel();

    // Update our count bookkeeping.  Note: don't attempt to access
    // SizeTrackerCount() unless we are a list which supports constant order
    // size.  The method will not exist when we have O(N) access to our size.
    if constexpr (ListSizeOrder == SizeOrder::Constant) {
      this->IncSizeTracker(other_list.SizeTrackerCount());
      other_list.ResetSizeTracker();
    }
  }

  // insert_after : Insert an element after iter in the list.
  //
  // Note: It is an error to attempt to push a nullptr instance of PtrType, or
  // to attempt to push with iter == end().
  void insert_after(const iterator& iter, const PtrType& ptr) { insert_after(iter, PtrType(ptr)); }
  void insert_after(const iterator& iter, PtrType&& ptr) {
    ZX_DEBUG_ASSERT(iter.IsValid());

    auto& ns = NodeTraits::node_state(*iter.node_);
    internal_insert(ns.next_, std::move(ptr));
  }

  // pop_front and pop_back
  //
  // Removes either the head or the tail of the list and transfers the pointer
  // to the caller.  If the list is empty, return a nullptr instance of
  // PtrType.
  PtrType pop_front() { return internal_erase(head_); }
  PtrType pop_back() { return internal_erase(tail()); }

  // erase
  //
  // Erase the element at the provided iterator.  If there is no element in
  // the list at this position (iter is end()), return a nullptr instance of
  // PtrType.  It is an error to attempt to use an iterator from a different
  // instance of this list type to attempt to erase a node.
  PtrType erase(ValueType& obj) { return internal_erase(&obj); }
  PtrType erase(const iterator& iter) { return internal_erase(iter.node_); }

  // erase_next
  //
  // Remove the element in the list which follows iter.  If there is no
  // element in the list which follows iter, return a nullptr instance of
  // PtrType.  It is an error to attempt to erase_next an invalid iterator
  // (either an uninitialized iterator, or an iterator which is equal to
  // end()) It is an error to attempt to use an iterator from a different
  // instance of this list type to attempt to erase a node.
  PtrType erase_next(const iterator& iter) {
    ZX_DEBUG_ASSERT(iter.IsValid());
    auto& ns = NodeTraits::node_state(*iter.node_);

    if (internal::is_sentinel_ptr(ns.next_)) {
      ZX_DEBUG_ASSERT(sentinel() == ns.next_);
      return PtrType(nullptr);
    }

    return internal_erase(ns.next_);
  }

  // clear
  //
  // Clear out the list, unlinking all of the elements in the process.  For
  // managed pointer types, this will release all references held by the list
  // to the objects which were in it.
  void clear() {
    while (!is_empty()) {
      auto& head_ns = NodeTraits::node_state(*head_);

      // Reclaim our pointer so that it will release its reference when it
      // goes out of scope a the end of the loop.  Note, this needs to be
      // flagged as UNUSED because for unmanaged pointer types, nothing
      // happens when the pointer goes out of scope.
      __UNUSED auto tmp = PtrTraits::Reclaim(head_);
      head_ = head_ns.next_;
      head_ns.next_ = nullptr;
      head_ns.prev_ = nullptr;
    }

    // Update our count bookkeeping.
    this->ResetSizeTracker();
  }

  // clear_unsafe
  //
  // See comments in fbl/intrusive_single_list.h
  // Think carefully before calling this!
  void clear_unsafe() {
    static_assert(PtrTraits::IsManaged == false,
                  "clear_unsafe is not allowed for containers of managed pointers");
    head_ = sentinel();

    // Update our count bookkeeping.
    this->ResetSizeTracker();
  }

  // swap : swaps the contest of two lists.
  void swap(DoublyLinkedList& other) {
    internal::Swap(head_, other.head_);

    RawPtrType& sentinel_ptr = is_empty() ? head_ : NodeTraits::node_state(*tail()).next_;
    RawPtrType& other_sentinel_ptr =
        other.is_empty() ? other.head_ : NodeTraits::node_state(*other.tail()).next_;

    sentinel_ptr = sentinel();
    other_sentinel_ptr = other.sentinel();
    this->SwapSizeTracker(other);
  }

  // size_slow : count the elements in the list in O(n) fashion.
  size_t size_slow() const {
    // It is illegal to call this if the user requested constant order size
    // operations.
    static_assert(
        ListSizeOrder == SizeOrder::N,
        "size_slow is only allowed when using a list which has O(N) size!  Use size() instead.");

    size_t size = 0;

    for (auto iter = cbegin(); iter != cend(); ++iter) {
      size++;
    }

    return size;
  }

  // size : Only allowed when the user has selected an SizeOrder::Constant for this list.
  size_t size() const {
    static_assert(
        ListSizeOrder == SizeOrder::Constant,
        "size is only allowed when using a list which has O(1) size!  Use size_slow() instead.");
    return this->SizeTrackerCount();
  }

  // erase_if
  //
  // Find the first member of the list which satisfies the predicate given by
  // 'fn' and erase it from the list, returning a referenced pointer to the
  // removed element.  Return nullptr if no element satisfies the predicate.
  template <typename UnaryFn>
  PtrType erase_if(UnaryFn fn) {
    for (auto iter = begin(); iter != end(); ++iter)
      if (fn(static_cast<typename PtrTraits::ConstRefType>(*iter)))
        return erase(iter);

    return PtrType(nullptr);
  }

  // find_if
  //
  // Find the first member of the list which satisfies the predicate given by
  // 'fn' and return an iterator in the list which refers to it.  Return end()
  // if no member satisfies the predicate.
  template <typename UnaryFn>
  const_iterator find_if(UnaryFn fn) const {
    for (auto iter = begin(); iter.IsValid(); ++iter) {
      if (fn(*iter))
        return iter;
    }

    return end();
  }

  template <typename UnaryFn>
  iterator find_if(UnaryFn fn) {
    const_iterator citer = const_cast<const ContainerType*>(this)->find_if(fn);
    return iterator(citer.node_);
  }

  // replace_if (copy)
  //
  // Find the first member of the list which satisfies the predicate given by
  // 'fn' and replace it in the list, returning a referenced pointer to the
  // replaced element.  If no member satisfies the predicate, simply return
  // nullptr instead.
  template <typename UnaryFn>
  PtrType replace_if(UnaryFn fn, const PtrType& ptr) {
    iterator iter = find_if(fn);

    if (!iter.IsValid())
      return nullptr;

    return internal_swap(*iter, PtrType(ptr));
  }

  // replace_if (move)
  //
  // Same as the copy version, except that if no member satisfies the
  // predicate, the original reference is returned instead of nullptr.
  template <typename UnaryFn>
  PtrType replace_if(UnaryFn fn, PtrType&& ptr) {
    iterator iter = find_if(fn);

    if (!iter.IsValid())
      return std::move(ptr);

    return internal_swap(*iter, std::move(ptr));
  }

  // replace (copy and move)
  //
  // Replaces the target member of the list with the given replacement.
  PtrType replace(typename PtrTraits::RefType target, PtrType replacement) {
    return internal_swap(target, std::move(replacement));
  }

 private:
  // The traits of a non-const iterator
  struct iterator_traits {
    using RefType = typename PtrTraits::RefType;
    using RawPtrType = typename PtrTraits::RawPtrType;
  };

  // The traits of a const iterator
  struct const_iterator_traits {
    using RefType = typename PtrTraits::ConstRefType;
    using RawPtrType = typename PtrTraits::ConstRawPtrType;
  };

  // The shared implementation of the iterator
  template <class IterTraits>
  class iterator_impl {
   public:
    iterator_impl() {}
    iterator_impl(const iterator_impl& other) { node_ = other.node_; }

    iterator_impl& operator=(const iterator_impl& other) {
      node_ = other.node_;
      return *this;
    }

    bool IsValid() const { return !internal::is_sentinel_ptr(node_) && (node_ != nullptr); }
    bool operator==(const iterator_impl& other) const { return node_ == other.node_; }
    bool operator!=(const iterator_impl& other) const { return node_ != other.node_; }

    // Prefix
    iterator_impl& operator++() {
      if (IsValid()) {
        auto& ns = NodeTraits::node_state(*node_);
        node_ = ns.next_;
        ZX_DEBUG_ASSERT(node_ != nullptr);
      }

      return *this;
    }

    iterator_impl& operator--() {
      if (node_) {
        if (internal::is_sentinel_ptr(node_)) {
          auto list = internal::unmake_sentinel<ListPtrType>(node_);
          node_ = list->tail();
        } else {
          auto& ns = NodeTraits::node_state(*node_);
          node_ = ns.prev_;
          ZX_DEBUG_ASSERT(node_ != nullptr);

          // If backing up would put us at a node whose next pointer
          // is a sentinel, the we have looped around the start of the
          // list and are currently pointed at the tail of the list.
          // Reset our internal pointer to point to the sentinel value
          // instead.
          auto& new_ns = NodeTraits::node_state(*node_);
          if (internal::is_sentinel_ptr(new_ns.next_))
            node_ = new_ns.next_;
        }
      }

      return *this;
    }

    // Postfix
    iterator_impl operator++(int) {
      iterator_impl ret(*this);
      ++(*this);
      return ret;
    }

    iterator_impl operator--(int) {
      iterator_impl ret(*this);
      --(*this);
      return ret;
    }

    typename PtrTraits::PtrType CopyPointer() const {
      return IsValid() ? PtrTraits::Copy(node_) : nullptr;
    }

    typename IterTraits::RefType operator*() const {
      ZX_DEBUG_ASSERT(IsValid());
      return *node_;
    }

    typename IterTraits::RawPtrType operator->() const {
      ZX_DEBUG_ASSERT(IsValid());
      return node_;
    }

   private:
    friend class DoublyLinkedList<T, NodeTraits_, TagType, ListSizeOrder>;
    using ListPtrType = const DoublyLinkedList<T, NodeTraits_, TagType, ListSizeOrder>*;

    iterator_impl(const typename PtrTraits::RawPtrType node)
        : node_(const_cast<typename PtrTraits::RawPtrType>(node)) {}

    typename PtrTraits::RawPtrType node_ = nullptr;
  };

  template <typename BaseNodeTraits>
  struct AddGenericNodeState<BaseNodeTraits,
                             std::enable_if_t<internal::has_node_state_v<BaseNodeTraits>>>
      : public BaseNodeTraits {};

  template <typename BaseNodeTraits>
  struct AddGenericNodeState<BaseNodeTraits,
                             std::enable_if_t<!internal::has_node_state_v<BaseNodeTraits>>>
      : public BaseNodeTraits {
    static DoublyLinkedListNodeState<T>& node_state(typename PtrTraits::RefType obj) {
      return DefaultDoublyLinkedListTraits<T>::template node_state<TagType>(obj);
    }
  };

  // The test framework's 'checker' class is our friend.
  friend CheckerType;

  // move semantics only
  DoublyLinkedList(const DoublyLinkedList&) = delete;
  DoublyLinkedList& operator=(const DoublyLinkedList&) = delete;

  constexpr RawPtrType sentinel() const { return internal::make_sentinel<RawPtrType>(this); }

  void internal_insert(RawPtrType before, PtrType&& ptr) {
    ZX_DEBUG_ASSERT(ptr != nullptr);
    ZX_DEBUG_ASSERT(before != nullptr);
    ZX_DEBUG_ASSERT(head_ != nullptr);

    auto& ptr_ns = NodeTraits::node_state(*ptr);
    ZX_DEBUG_ASSERT((ptr_ns.prev_ == nullptr) && (ptr_ns.next_ == nullptr));

    // No matter what happens, we are going to be 1 larger after this operation.
    this->IncSizeTracker(1);

    // Handle the (slightly) special case of an empty list.
    if (is_empty()) {
      ZX_DEBUG_ASSERT(before == sentinel());
      ZX_DEBUG_ASSERT(before == head_);

      ptr_ns.prev_ = PtrTraits::Leak(ptr);
      ptr_ns.next_ = head_;
      head_ = ptr_ns.prev_;

      return;
    }

    // If we are being inserted before the sentinel, the we are the new
    // tail, and the node_state which contains the prev pointer we need to
    // update is head's.  Otherwise, it is the node_state of the node we are
    // being inserted before.
    auto& prev_ns = NodeTraits::node_state(internal::is_sentinel_ptr(before) ? *head_ : *before);
    auto& tgt_prev = prev_ns.prev_;

    // If we are being inserted before the head, then we need to update the
    // managed head pointer.  Otherwise, we need to update the next pointer
    // of the node which is about to come before us.
    auto& tgt_next = (head_ == before) ? head_
                                       : internal::is_sentinel_ptr(before)
                                             ? NodeTraits::node_state(*tail()).next_
                                             : NodeTraits::node_state(*tgt_prev).next_;

    // Update the pointers in the inserted node.
    ptr_ns.next_ = tgt_next;
    ptr_ns.prev_ = tgt_prev;

    // Update the pointers which should now point to the inserted node.
    tgt_next = PtrTraits::GetRaw(ptr);
    tgt_prev = PtrTraits::Leak(ptr);
  }

  PtrType internal_erase(RawPtrType node) {
    if (!node || internal::is_sentinel_ptr(node))
      return PtrType(nullptr);

    // No matter what happens after this, we are going to be 1 smaller after this operation.
    this->DecSizeTracker(1);

    auto& node_ns = NodeTraits::node_state(*node);
    ZX_DEBUG_ASSERT((node_ns.prev_ != nullptr) && (node_ns.next_ != nullptr));

    // Find the prev pointer we need to update.  If we are removing the tail
    // of the list, the prev pointer is head_'s prev pointer.  Otherwise, it
    // is the prev pointer of the node which currently follows "ptr".
    auto& tgt_prev = internal::is_sentinel_ptr(node_ns.next_)
                         ? NodeTraits::node_state(*head_).prev_
                         : NodeTraits::node_state(*node_ns.next_).prev_;

    // Find the next pointer we need to update.  If we are removing the
    // head of the list, this is head_.  Otherwise it is the next pointer of
    // the node which comes before us in the list.
    auto& tgt_next = (head_ == node) ? head_ : NodeTraits::node_state(*node_ns.prev_).next_;

    tgt_prev = node_ns.prev_;
    tgt_next = node_ns.next_;
    node_ns.prev_ = nullptr;
    node_ns.next_ = nullptr;

    return PtrTraits::Reclaim(node);
  }

  PtrType internal_swap(typename PtrTraits::RefType node, PtrType&& ptr) {
    ZX_DEBUG_ASSERT(ptr != nullptr);
    auto& ptr_ns = NodeTraits::node_state(*ptr);
    ZX_DEBUG_ASSERT(!ptr_ns.InContainer());

    auto& node_ns = NodeTraits::node_state(node);
    ZX_DEBUG_ASSERT(node_ns.InContainer());

    // Handle the case of there being only a single node in the list.
    ZX_DEBUG_ASSERT(internal::valid_sentinel_ptr(head_));
    if (internal::is_sentinel_ptr(NodeTraits::node_state(*head_).next_)) {
      ZX_DEBUG_ASSERT(head_ == &node);
      ZX_DEBUG_ASSERT(internal::is_sentinel_ptr(node_ns.next_));
      ZX_DEBUG_ASSERT(&node == node_ns.prev_);

      ptr_ns.next_ = node_ns.next_;
      ptr_ns.prev_ = PtrTraits::GetRaw(ptr);
      node_ns.next_ = nullptr;
      node_ns.prev_ = nullptr;

      head_ = PtrTraits::Leak(ptr);
      return PtrTraits::Reclaim(&node);
    }

    // Find the prev pointer we need to update.  If we are swapping the tail
    // of the list, the prev pointer is head_'s prev pointer.  Otherwise, it
    // is the prev pointer of the node which currently follows "ptr".
    auto& tgt_prev = internal::is_sentinel_ptr(node_ns.next_)
                         ? NodeTraits::node_state(*head_).prev_
                         : NodeTraits::node_state(*node_ns.next_).prev_;

    // Find the next pointer we need to update.  If we are swapping the
    // head of the list, this is head_.  Otherwise it is the next pointer of
    // the node which comes before us in the list.
    auto& tgt_next = (head_ == &node) ? head_ : NodeTraits::node_state(*node_ns.prev_).next_;

    tgt_next = PtrTraits::Leak(ptr);
    tgt_prev = tgt_next;
    internal::Swap(ptr_ns.next_, node_ns.next_);
    internal::Swap(ptr_ns.prev_, node_ns.prev_);

    return PtrTraits::Reclaim(&node);
  }

  RawPtrType tail() const {
    ZX_DEBUG_ASSERT(head_ != nullptr);
    if (internal::is_sentinel_ptr(head_))
      return head_;
    return NodeTraits::node_state(*head_).prev_;
  }

  // State consists of a raw pointer to the head of the list.  Initially, this
  // is set to the special sentinel value, which allows iterators set to
  // this->end() to back up to the tail of the list.
  RawPtrType head_ = sentinel();
};

// SizedDoublyLinkedList<> is an alias for a DoublyLinkedList<> which keeps
// track of it's size internally so that it may be accessed in O(1) time.
//
template <typename T, typename NodeTraits = DefaultDoublyLinkedListTraits<T>,
          typename TagType = DefaultObjectTag>
using SizedDoublyLinkedList = DoublyLinkedList<T, NodeTraits, TagType, SizeOrder::Constant>;

// TaggedDoublyLinkedList<> is intended for use with ContainableBaseClasses<>.
//
// For an easy way to allow instances of your class to live in multiple
// intrusive containers at once, simply derive from
// ContainableBaseClasses<YourContainables<PtrType, TagType>...> and then use
// this template instead of DoublyLinkedList<> as the container, passing the same tag
// type you used earlier as the third parameter.
//
// See comments on ContainableBaseClasses<> in fbl/intrusive_container_utils.h
// for more details.
//
template <typename T, typename TagType, typename NodeTraits = DefaultDoublyLinkedListTraits<T>>
using TaggedDoublyLinkedList = DoublyLinkedList<T, NodeTraits, TagType, SizeOrder::N>;

// SizedTaggedDoublyLinkedList<> is a variant of TaggedDoublyLinkedList which
// also specifies O(1) access size().
//
template <typename T, typename TagType, typename NodeTraits = DefaultDoublyLinkedListTraits<T>>
using SizedTaggedDoublyLinkedList = DoublyLinkedList<T, NodeTraits, TagType, SizeOrder::Constant>;

}  // namespace fbl

#endif  // FBL_INTRUSIVE_DOUBLE_LIST_H_
