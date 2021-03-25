// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_HELPERS_INTRUSIVE_DOUBLE_LIST_H_
#define SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_HELPERS_INTRUSIVE_DOUBLE_LIST_H_

#include <lib/operation/helpers/intrusive_container_node_utils.h>
#include <lib/operation/helpers/intrusive_container_utils.h>
#include <lib/operation/helpers/intrusive_pointer_traits.h>
#include <zircon/assert.h>

#include <utility>

// Usage and Implementation Notes:
//
// operation::DoublyLinkedList<> is a templated intrusive container class which
// allows users to manage doubly linked lists of objects.
//
// operation::DoublyLinkedList<> follows the same patterns as
// operation::SinglyLinkedList<> and implements a superset of the functionality
// (including support for managed pointer types).  Please refer to the "Usage
// Notes" section of operation/intrusive_single_list.h for details.
//
// Additional functionality provided by a DoublyLinkedList<> includes...
// ++ O(1) push_back/pop_back/back (in addition to push_front/pop_front/front)
// ++ The ability to "insert" in addition to "insert_after"
// ++ The ability to "erase" in addition to "erase_next"
// ++ Support for bidirectional iteration.
//
// Under the hood, the state of a DoublyLinkedList<> contains a single raw
// pointer to the object which is the head of the list, or nullptr if the list
// is empty.  Each object on the list has a DoublyLinkedListNodeState<> which
// contains one raw pointer (prev) and one managed pointer (next) which are
// arranged in a ring.  The tail of a non-empty list can be found in O(1) time
// by following the prev pointer of the head node of the list.
namespace operation {

// Fwd decl of sanity checker class used by tests.
namespace tests {
namespace intrusive_containers {
class DoublyLinkedListChecker;
template <typename>
class SequenceContainerTestEnvironment;
}  // namespace intrusive_containers
}  // namespace tests

// fwd decl of the list base class.
namespace internal {
template <typename PtrType>
class DoublyLinkedListBase;
}

template <typename PtrType_, NodeOptions Options = NodeOptions::None>
struct DoublyLinkedListNodeState
    : public internal::CommonNodeStateBase<DoublyLinkedListNodeState<PtrType_, Options>> {
 private:
  using Base = internal::CommonNodeStateBase<DoublyLinkedListNodeState<PtrType_, Options>>;

 public:
  using PtrType = PtrType_;
  using PtrTraits = internal::ContainerPtrTraits<PtrType>;
  static constexpr NodeOptions kNodeOptions = Options;

  constexpr DoublyLinkedListNodeState() {}
  ~DoublyLinkedListNodeState() {
    ZX_DEBUG_ASSERT(IsValid());
    if constexpr (!(kNodeOptions & operation::NodeOptions::AllowClearUnsafe)) {
      ZX_DEBUG_ASSERT(!InContainer());
    }
  }

  bool IsValid() const { return ((next_ == nullptr) == (prev_ == nullptr)); }
  bool InContainer() const { return (next_ != nullptr); }

  template <typename NodeTraits>
  PtrType_ RemoveFromContainer() {
    static_assert(kNodeOptions & NodeOptions::AllowRemoveFromContainer,
                  "Node does not support direct RemoveFromContainer operations");

    return internal::DoublyLinkedListBase<PtrType_>::template internal_erase<NodeTraits>(*this);
  }

  // Defer to CommonNodeStateBase for enforcement of the various copy/move
  // rules.  Make sure, however, that we explicitly do not allow our own default
  // construction/assignment operators change anything about our state.
  DoublyLinkedListNodeState(const DoublyLinkedListNodeState& other) : Base(other) {}
  DoublyLinkedListNodeState& operator=(const DoublyLinkedListNodeState& other) {
    this->Base::operator=(other);
    return *this;
  }
  DoublyLinkedListNodeState(DoublyLinkedListNodeState&& other) : Base(std::move(other)) {}
  DoublyLinkedListNodeState& operator=(DoublyLinkedListNodeState&& other) {
    this->Base::operator=(std::move(other));
    return *this;
  }

 private:
  template <typename, typename, SizeOrder, typename>
  friend class DoublyLinkedList;
  template <typename>
  friend class internal::DoublyLinkedListBase;
  template <typename>
  friend class tests::intrusive_containers::SequenceContainerTestEnvironment;
  friend class tests::intrusive_containers::DoublyLinkedListChecker;

  typename PtrTraits::RawPtrType next_ = nullptr;
  typename PtrTraits::RawPtrType prev_ = nullptr;
};

template <typename PtrType, NodeOptions Options, typename TagType>
struct DoublyLinkedListable;

template <typename PtrType_, typename TagType_>
struct DefaultDoublyLinkedListTraits {
 private:
  using ValueType = typename internal::ContainerPtrTraits<PtrType_>::ValueType;

 public:
  using PtrType = PtrType_;
  using TagType = TagType_;
  using PtrTraits = internal::ContainerPtrTraits<PtrType>;

  static auto& node_state(typename PtrTraits::RefType obj) {
    if constexpr (std::is_same_v<TagType, DefaultObjectTag>) {
      return obj.ValueType::dll_node_state_;
    } else {
      return obj.template GetContainableByTag<TagType>().dll_node_state_;
    }
  }

  using NodeState =
      std::decay_t<std::invoke_result_t<decltype(node_state), typename PtrTraits::RefType>>;
};

template <typename PtrType_, NodeOptions Options = NodeOptions::None,
          typename TagType_ = DefaultObjectTag>
struct DoublyLinkedListable {
 public:
  using PtrType = PtrType_;
  using TagType = TagType_;
  static constexpr NodeOptions kNodeOptions = Options;

  bool InContainer() const {
    using Node = DoublyLinkedListable<PtrType, Options, TagType>;
    return Node::dll_node_state_.InContainer();
  }

  PtrType RemoveFromContainer() {
    using ListTraits = DefaultDoublyLinkedListTraits<PtrType, TagType_>;
    return dll_node_state_.template RemoveFromContainer<ListTraits>();
  }

 private:
  friend struct DefaultDoublyLinkedListTraits<PtrType, TagType>;
  DoublyLinkedListNodeState<PtrType, Options> dll_node_state_;
};

namespace internal {

template <typename PtrType_>
class DoublyLinkedListBase {
 public:
  constexpr DoublyLinkedListBase() = default;
  ~DoublyLinkedListBase() {}

  using PtrType = PtrType_;
  using PtrTraits = internal::ContainerPtrTraits<PtrType>;
  using RawPtrType = typename PtrTraits::RawPtrType;

 protected:
  template <typename, NodeOptions>
  friend struct ::operation::DoublyLinkedListNodeState;

  template <typename NodeTraits, typename NodeStateType>
  static PtrType internal_erase(NodeStateType& node_ns) {
    // No one should be calling internal erase with an invalid node state, or a
    // node state which is not in-container.
    ZX_DEBUG_ASSERT(node_ns.IsValid() && node_ns.InContainer());

    // Fetch the previous node's node state (this may be our own in the case
    // that we are the only element on the list)
    auto& prev_node_ns = NodeTraits::node_state(*node_ns.prev_);

    // Find the prev pointer we need to update.  If we are removing the tail
    // of the list, the prev pointer is head_'s prev pointer.  Otherwise, it
    // is the prev pointer of the node which currently follows "next_"
    auto& tgt_prev = internal::is_sentinel_ptr(node_ns.next_)
                         ? NodeTraits::node_state(*head_from_sentinel(node_ns.next_)).prev_
                         : NodeTraits::node_state(*node_ns.next_).prev_;

    // Find the next pointer we need to update.  If the next_ pointer of the
    // previous node state is a sentinel, removing the head of the list and the
    // next_ pointer we need to update is actually the head_ for the list.
    // Otherwise, it is simply the next_ pointer of the previous node state.
    auto& tgt_next = internal::is_sentinel_ptr(prev_node_ns.next_)
                         ? head_from_sentinel(prev_node_ns.next_)
                         : prev_node_ns.next_;

    RawPtrType erased = tgt_next;

    tgt_prev = node_ns.prev_;
    tgt_next = node_ns.next_;
    node_ns.prev_ = nullptr;
    node_ns.next_ = nullptr;

    return PtrTraits::Reclaim(erased);
  }

  constexpr RawPtrType sentinel() const { return internal::make_sentinel<RawPtrType>(this); }

  // State consists of a raw pointer to the head of the list.  Initially, this
  // is set to the special sentinel value, which allows iterators set to
  // this->end() to back up to the tail of the list.
  RawPtrType head_ = sentinel();

 private:
  // Fetch a reference to the head_ member the list referenced by a sentinel
  // value.
  static RawPtrType& head_from_sentinel(RawPtrType node) {
    return internal::unmake_sentinel<DoublyLinkedListBase<PtrType_>*>(node)->head_;
  }
};

}  // namespace internal

template <typename PtrType_, typename TagType_ = DefaultObjectTag,
          SizeOrder ListSizeOrder_ = SizeOrder::N,
          typename NodeTraits_ = DefaultDoublyLinkedListTraits<PtrType_, TagType_>>
class __POINTER(PtrType_) DoublyLinkedList : public internal::DoublyLinkedListBase<PtrType_>,
                                             private internal::SizeTracker<ListSizeOrder_> {
 private:
  using Base = internal::DoublyLinkedListBase<PtrType_>;

  // Private fwd decls of the iterator implementation.
  template <typename IterTraits>
  class iterator_impl;
  struct iterator_traits;
  struct const_iterator_traits;

 public:
  // Aliases used to reduce verbosity and expose types/traits to tests
  static constexpr SizeOrder ListSizeOrder = ListSizeOrder_;
  using typename Base::PtrType;
  using TagType = TagType_;
  using NodeTraits = NodeTraits_;

  using typename Base::PtrTraits;
  using typename Base::RawPtrType;
  using ValueType = typename PtrTraits::ValueType;
  using RefType = typename PtrTraits::RefType;
  using CheckerType = ::operation::tests::intrusive_containers::DoublyLinkedListChecker;
  using ContainerType = DoublyLinkedList<PtrType_, TagType_, ListSizeOrder_, NodeTraits_>;

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
  constexpr DoublyLinkedList() noexcept {
    using NodeState = internal::node_state_t<NodeTraits, RefType>;

    // Make certain that the type of pointer we are expected to manage matches
    // the type of pointer that our Node type expects to manage.
    static_assert(std::is_same_v<PtrType, typename NodeState::PtrType>,
                  "DoublyLinkedList's pointer type must match its Node's pointer type");

    // Direct remove-from-container is only allowed if this list does not keep
    // track of it's size.
    static_assert((ListSizeOrder == SizeOrder::N) ||
                      !(NodeState::kNodeOptions & NodeOptions::AllowRemoveFromContainer),
                  "Nodes which allow RemoveFromContainer may not be used with DoublyLinkedLists "
                  "that track size");
  }

  // Rvalue construction is permitted, but will result in the move of the list
  // contents from one instance of the list to the other (even for unmanaged
  // pointers)
  DoublyLinkedList(DoublyLinkedList&& other_list) noexcept : DoublyLinkedList() {
    swap(other_list);
  }

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

  // insert : Insert an element before iter in the list, and return an
  // element to the newly-inserted element.
  iterator insert(const iterator& iter, const PtrType& ptr) { return insert(iter, PtrType(ptr)); }
  iterator insert(const iterator& iter, PtrType&& ptr) {
    return internal_insert(iter.node_, std::move(ptr));
  }

  iterator insert(ValueType& before, const PtrType& ptr) { return insert(before, PtrType(ptr)); }
  iterator insert(ValueType& before, PtrType&& ptr) {
    return internal_insert(&before, std::move(ptr));
  }

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
    auto& tgt_next = (head_ == before)                   ? head_
                     : internal::is_sentinel_ptr(before) ? NodeTraits::node_state(*tail()).next_
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

  // Split the list immediately after |iter|, returning the remainder of the
  // list in a new list instance.
  //
  // |iter| *must* refer to a member of the list being split.  Attempt to split
  // list A with an iterator to an element which is a member of list B will
  // result in undefined behavior which may not be detectable at runtime.
  ContainerType split_after(const iterator& iter) {
    if (!iter.IsValid()) {
      // iter must refer to a member of this list, therefore it must be valid.
      // DEBUG_ASSERT if it is not, or return an empty list in a release build.
      ZX_DEBUG_ASSERT(false);
      return {};
    }
    return split_after(*iter.node_);
  }

  // Alternate form of split_after which uses an object reference instead of an
  // iterator to determine the split point.  Just like the iterator form of
  // split_after, |obj| *must* be a member of the list being split.
  ContainerType split_after(ValueType& obj) {
    static_assert(ListSizeOrder == SizeOrder::N,
                  "split_after is not allowed for SizedDoublyLinkedLists");
    auto& A_ns = NodeTraits::node_state(obj);

    // If this is element is already the tail of the list, or if it is an
    // illegal split in a release build, simply return an empty list.
    if (!A_ns.InContainer()) {
      ZX_DEBUG_ASSERT(false);
      return {};
    }

    if (internal::is_sentinel_ptr(A_ns.next_)) {
      // Since this node is at the end of the list, we can sanity check to make
      // sure that obj was actually a member of this list.
      ZX_DEBUG_ASSERT(A_ns.next_ == sentinel());
      return {};
    }

    // At this point in time, we know that we have at least 2 nodes in the list
    // we are splitting.  Let A be |obj|, and B be the node immediately after A.
    //
    // We have 5 pointers we need to update in total.
    //
    // ret.head       : needs to point to B, which is the new head of ret
    // A.next         : A is the new tail.  Next becomes this.sentinel();
    // B.prev         : B is the new head of ret.  Its prev points to ret.tail
    // this.head.prev : A is the new tail of this.  this.head.prev needs to point to it.
    // this.tail.next : this.tail is now ret.tail, so this.tail.next = ret.sentinel()
    ContainerType ret;
    auto& B_ns = NodeTraits::node_state(*A_ns.next_);
    auto& head_ns = NodeTraits::node_state(*head_);
    auto& tail_ns = NodeTraits::node_state(*head_ns.prev_);

    ret.head_ = A_ns.next_;
    A_ns.next_ = this->sentinel();
    B_ns.prev_ = head_ns.prev_;
    head_ns.prev_ = &obj;
    tail_ns.next_ = ret.sentinel();

    return ret;
  }

  // insert_after : Insert an element after iter in the list, and return
  // an element to the newly-inserted element.
  //
  // Note: It is an error to attempt to push a nullptr instance of PtrType, or
  // to attempt to push with iter == end().
  iterator insert_after(const iterator& iter, const PtrType& ptr) {
    return insert_after(iter, PtrType(ptr));
  }
  iterator insert_after(const iterator& iter, PtrType&& ptr) {
    ZX_DEBUG_ASSERT(iter.IsValid());

    auto& ns = NodeTraits::node_state(*iter.node_);
    return internal_insert(ns.next_, std::move(ptr));
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
  // Remove the element at the provided iterator and return a pointer to the
  // removed element.  If there is no element in the list at this position
  // (iter is end()), return a nullptr instance of PtrType.  It is an error to
  // attempt to use an iterator from a different instance of this list type to
  // attempt to erase a node.
  PtrType erase(ValueType& obj) { return internal_erase(&obj); }
  PtrType erase(const iterator& iter) { return internal_erase(iter.node_); }

  // erase_next
  //
  // Remove the element in the list which follows iter and return a pointer to
  // the removed element.  If there is no element in the list which follows
  // iter, return a nullptr instance of PtrType.  It is an error to attempt to
  // erase_next an invalid iterator (either an uninitialized iterator, or an
  // iterator which is equal to end()) It is an error to attempt to use an
  // iterator from a different instance of this list type to attempt to erase
  // a node.
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
  // See comments in operation/intrusive_single_list.h
  // Think carefully before calling this!
  void clear_unsafe() {
    static_assert(PtrTraits::IsManaged == false,
                  "clear_unsafe is not allowed for containers of managed pointers");
    static_assert(NodeTraits::NodeState::kNodeOptions & NodeOptions::AllowClearUnsafe,
                  "Container does not support clear_unsafe.  Consider adding "
                  "NodeOptions::AllowClearUnsafe to your node storage.");

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
  // 'fn' and remove it from the list, returning a referenced pointer to the
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
    iterator_impl() = default;
    iterator_impl(const iterator_impl& other) = default;
    iterator_impl& operator=(const iterator_impl& other) = default;

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
          auto list = unmake_sentinel(node_);
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
    friend class DoublyLinkedList<PtrType_, TagType_, ListSizeOrder_, NodeTraits_>;
    using ListType = const DoublyLinkedList<PtrType_, TagType_, ListSizeOrder_, NodeTraits_>;

    iterator_impl(const typename PtrTraits::RawPtrType node)
        : node_(const_cast<typename PtrTraits::RawPtrType>(node)) {}

    const ListType* unmake_sentinel(typename PtrTraits::RawPtrType p) {
      return static_cast<const ListType*>(internal::unmake_sentinel<typename ListType::Base*>(p));
    }

    typename PtrTraits::RawPtrType node_ = nullptr;
  };

  // The test framework's 'checker' class is our friend.
  friend CheckerType;

  // move semantics only
  DoublyLinkedList(const DoublyLinkedList&) = delete;
  DoublyLinkedList& operator=(const DoublyLinkedList&) = delete;

  constexpr RawPtrType sentinel() const { return internal::make_sentinel<RawPtrType>(this); }

  iterator internal_insert(RawPtrType before, PtrType&& ptr) {
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

      auto* new_item = PtrTraits::Leak(ptr);
      ptr_ns.prev_ = new_item;
      ptr_ns.next_ = head_;
      head_ = new_item;

      return make_iterator(*new_item);
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
    auto& tgt_next = (head_ == before)                   ? head_
                     : internal::is_sentinel_ptr(before) ? NodeTraits::node_state(*tail()).next_
                                                         : NodeTraits::node_state(*tgt_prev).next_;

    // Update the pointers in the inserted node.
    ptr_ns.next_ = tgt_next;
    ptr_ns.prev_ = tgt_prev;

    // Update the pointers which should now point to the inserted node.
    auto* new_item = PtrTraits::Leak(ptr);
    tgt_next = new_item;
    tgt_prev = new_item;

    // Return an iterator to the new element.
    return make_iterator(*new_item);
  }

  PtrType internal_erase(RawPtrType node) {
    if (!node || internal::is_sentinel_ptr(node)) {
      return PtrType(nullptr);
    }

    // No matter what happens after this, we are going to be 1 smaller after this operation.
    this->DecSizeTracker(1);

    // Defer to our base implementation in order to remove this node.
    return Base::template internal_erase<NodeTraits>(NodeTraits::node_state(*node));
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

  using Base::head_;
  using Base::sentinel;
};

// SizedDoublyLinkedList<> is an alias for a DoublyLinkedList<> which keeps
// track of it's size internally so that it may be accessed in O(1) time.
//
template <typename PtrType, typename TagType = DefaultObjectTag,
          typename NodeTraits = DefaultDoublyLinkedListTraits<PtrType, TagType>>
using SizedDoublyLinkedList = DoublyLinkedList<PtrType, TagType, SizeOrder::Constant, NodeTraits>;

// DoublyLinkedListCustomTraits<> is an alias for a DoublyLinkedList<> which makes is easier to
// define a DoublyLinkedList which uses custom node traits.  It defaults to O(n) size, and will not
// allow users to use a non-default object tag, since lists which use custom node traits are
// required to use the default tag.
//
template <typename PtrType, typename NodeTraits, SizeOrder ListSizeOrder = SizeOrder::N>
using DoublyLinkedListCustomTraits =
    DoublyLinkedList<PtrType, DefaultObjectTag, ListSizeOrder, NodeTraits>;

// TaggedDoublyLinkedList<> is intended for use with ContainableBaseClasses<>.
//
// For an easy way to allow instances of your class to live in multiple
// intrusive containers at once, simply derive from
// ContainableBaseClasses<YourContainables<PtrType, TagType>...> and then use
// this template instead of DoublyLinkedList<> as the container, passing the same tag
// type you used earlier as the third parameter.
//
// See comments on ContainableBaseClasses<> in operation/intrusive_container_utils.h
// for more details.
//
template <typename PtrType, typename TagType>
using TaggedDoublyLinkedList = DoublyLinkedList<PtrType, TagType, SizeOrder::N,
                                                DefaultDoublyLinkedListTraits<PtrType, TagType>>;

template <typename PtrType, typename TagType, NodeOptions Options = NodeOptions::None>
using TaggedDoublyLinkedListable = DoublyLinkedListable<PtrType, Options, TagType>;

}  // namespace operation

#endif  // SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_HELPERS_INTRUSIVE_DOUBLE_LIST_H_
