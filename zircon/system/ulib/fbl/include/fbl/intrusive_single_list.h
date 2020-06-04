// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_INTRUSIVE_SINGLE_LIST_H_
#define FBL_INTRUSIVE_SINGLE_LIST_H_

#include <zircon/assert.h>

#include <utility>

#include <fbl/intrusive_container_node_utils.h>
#include <fbl/intrusive_container_utils.h>
#include <fbl/intrusive_pointer_traits.h>

// Usage Notes:
//
// fbl::SinglyLinkedList<> is a templated intrusive container class which
// allows users to manage singly linked lists of objects.
//
// The bookkeeping storage required to exist on a list is a property of the
// objects stored on the list eliminating the need for runtime bookkeeping
// allocation/deallocation to add/remove members to/from the container.
//
// Lists store pointers to the objects, not the objects themselves, and are
// templated based on the specific type of pointer to be stored.  Supported
// pointer types are....
// 1) T*            : raw unmanaged pointers
// 2) unique_ptr<T> : unique managed pointers.
// 3) RefPtr<T>     : managed pointers to ref-counted objects.
//
// Lists of managed pointer types hold references to objects and follow the
// rules of the particular managed pointer patterns.  Destroying or clearing a
// list of managed pointers will release the references to the objects and may
// end the lifecycle of an object if the reference held by the list happened to
// be the last.
//
// Lists of unmanaged pointer types perform no lifecycle management.  It is up to
// the user of the list class to make sure that lifecycles are managed properly.
// As an added safety, a list of unmanaged pointers will ZX_ASSERT if it is
// destroyed with elements still in it.
//
// Objects may exist in multiple lists (or other containers) through the use of
// custom trait classes.  It should be noted that it is possible to make
// different types lists of unique_ptr<T> for a given T, but there is little
// point in doing so as it is impossible to exist on multiple lists at the same
// time without violating the fundamental rules of unique_ptr<T> patterns.
//
// Default traits and a helper base class are provided to make it easy to
// implement list-able objects intended to exist on only one type of list.
//
////////////////////////////////////////////////////////////////////////////////
// Example: A simple list of unmanaged pointers to Foo objects
//
// class Foo : public fbl::SinglyLinkedListable<Foo*> {
//      ...
// };
//
// void Test() {
//     fbl::SinglyLinkedList<Foo*> list;
//
//     for (size_t i = 0; SOME_NUMBER; ++i)
//         list.push_front(new Foo(...));
//
//     for (const auto& foo : list)
//         foo.print();
//
//     while (!list.is_empty())
//         delete list.pop_front();
// }
//
////////////////////////////////////////////////////////////////////////////////
// Example: A simple list of unique pointers to Foo objects
//
// class Foo : public fbl::SinglyLinkedListable<unique_ptr<Foo>> {
//      ...
// };
//
// void Test() {
//     fbl::SinglyLinkedList<unique_ptr<Foo>> list;
//
//     for (size_t i = 0; SOME_NUMBER; ++i) {
//         unique_ptr<Foo> new_foo(new Foo(...));
//         list.push_front(std::move(new_foo));
//     }
//
//     for (const auto& foo : list)
//         foo.print();
//
//     list.clear();    // Could also just let the list go out of scope.
// }
//
////////////////////////////////////////////////////////////////////////////////
// Example: A more complicated example of a list of ref-counted objects which
// can exist on 3 different types of lists at the same time.
//
// class Foo : public fbl::SinglyLinkedListable<fbl::RefPtr<Foo>>
//           , public fbl::RefCounted<Foo> {
// public:
//     using NodeState = SinglyLinkedListNodeState<fbl::RefPtr<Foo>>;
//     struct TypeATraits { static NodeState& node_state(Foo& foo) { return foo.type_a_state_; } }
//     struct TypeBTraits { static NodeState& node_state(Foo& foo) { return foo.type_b_state_; } }
//
//     /* Class implementation goes here */
//
// private:
//     friend struct TypeATraits;
//     friend struct TypeBTraits;
//     NodeState type_a_state_;
//     NodeState type_b_state_;
// };
//
// void Test() {
//     using DefaultList = fbl::SinglyLinkedList<fbl::RefPtr<Foo>>;
//     using TypeAList   = fbl::SinglyLinkedListCustomTraits<fbl::RefPtr<Foo>, Foo::TypeATraits>;
//     using TypeBList   = fbl::SinglyLinkedListCustomTraits<fbl::RefPtr<Foo>, Foo::TypeBTraits>;
//
//     DefaultList default_list;
//     TypeAList a_list;
//     TypeAList b_list;
//
//     for (size_t i = 0; i < SOME_NUMBER; ++i) {
//         fbl::RefPtr<Foo> new_foo = AdoptRef(new Foo(...));
//
//         switch (i & 0x3) {
//             case 0: break;
//             case 1: a_list.push_front(new_foo); break;
//             case 2: b_list.push_front(new_foo); break;
//             case 3:
//                 a_list.push_front(new_foo);
//                 b_list.push_front(new_foo);
//                 break;
//         }
//
//         default_list.push_front(std::move(new_foo));
//     }
//
//     // default list contains all the Foo instances we created
//     // a_list has case 1 and case 3 Foo instances
//     // b_list has case 2 and case 3 Foo instances
//     for (const auto& foo : default_list) foo.print();
//     for (const auto& foo : a_list) foo.print();
//     for (const auto& foo : b_list) foo.print();
//
//     default_list.clear();  // case 0 Foo's get cleaned up.
//     a_list.clear();        // case 1 Foo's get cleaned up.
//     b_list.clear();        // case 2 and 3 Foo's get cleaned up.
// }

namespace fbl {

// Fwd decl of classes used by tests.
namespace tests {
namespace intrusive_containers {
class SinglyLinkedListChecker;
template <typename>
class SequenceContainerTestEnvironment;
}  // namespace intrusive_containers
}  // namespace tests

// SinglyLinkedListNodeState<PtrType>
//
// PtrTypehe state needed to be a member of a SinglyLinkedList<PtrType>.  All members of a
// specific type SinglyLinkedList<PtrType> must expose a SinglyLinkedListNodeState<PtrType>
// to the list implementation via the supplied traits.  See
// DefaultSinglyLinkedListPtrTyperaits<PtrType>
template <typename PtrType_, NodeOptions Options = NodeOptions::None>
struct SinglyLinkedListNodeState
    : public internal::CommonNodeStateBase<SinglyLinkedListNodeState<PtrType_, Options>> {
 private:
  using Base = internal::CommonNodeStateBase<SinglyLinkedListNodeState<PtrType_, Options>>;

 public:
  using PtrType = PtrType_;
  using PtrTraits = internal::ContainerPtrTraits<PtrType_>;
  static constexpr NodeOptions kNodeOptions = Options;

  constexpr SinglyLinkedListNodeState() {}
  ~SinglyLinkedListNodeState() {
    ZX_DEBUG_ASSERT(IsValid());
    if constexpr (!(kNodeOptions & fbl::NodeOptions::AllowClearUnsafe)) {
      ZX_DEBUG_ASSERT(!InContainer());
    }
  }

  // Defer to CommonNodeStateBase for enforcement of the various copy/move
  // rules.  Make sure, however, that we explicitly do not allow our own default
  // construction/assignment operators change anything about our state.
  SinglyLinkedListNodeState(const SinglyLinkedListNodeState& other) : Base(other) {}
  SinglyLinkedListNodeState& operator=(const SinglyLinkedListNodeState& other) {
    this->Base::operator=(other);
    return *this;
  }
  SinglyLinkedListNodeState(SinglyLinkedListNodeState&& other) : Base(std::move(other)) {}
  SinglyLinkedListNodeState& operator=(SinglyLinkedListNodeState&& other) {
    this->Base::operator=(std::move(other));
    return *this;
  }

  bool IsValid() const { return true; }
  bool InContainer() const { return (next_ != nullptr); }

 private:
  template <typename, typename, SizeOrder, typename>
  friend class SinglyLinkedList;
  template <typename>
  friend class tests::intrusive_containers::SequenceContainerTestEnvironment;
  friend class tests::intrusive_containers::SinglyLinkedListChecker;

  typename PtrTraits::RawPtrType next_ = nullptr;
};

template <typename PtrType, NodeOptions Options, typename TagType>
struct SinglyLinkedListable;

// DefaultSinglyLinkedListNodeState<PtrType, TagType>
//
// The default implementation of traits needed to be a member of a singly linked
// list.  Any valid traits implementation must expose a static node_state method
// compatible with DefaultSinglyLinkedListTraits<PtrType, TagType>::node_state(...).
//
// To use the default traits, an object may...
//
// 1) Be friends with DefaultSinglyLinkedListTraits<PtrType, TagType> and have a
//    private sll_node_state_ member.
// 2) Have a public sll_node_state_ member (not recommended)
// 3) Derive from SinglyLinkedListable<PtrType> or
//    ContainableBaseClasses<SinglyLinkedListable<PtrType, TagType> [...]>
//    (easiest)
template <typename PtrType_, typename TagType_ = DefaultObjectTag>
struct DefaultSinglyLinkedListTraits {
 private:
  using ValueType = typename internal::ContainerPtrTraits<PtrType_>::ValueType;

 public:
  using PtrType = PtrType_;
  using TagType = TagType_;
  using PtrTraits = internal::ContainerPtrTraits<PtrType_>;

  static auto& node_state(typename PtrTraits::RefType obj) {
    if constexpr (std::is_same_v<TagType, DefaultObjectTag>) {
      return obj.ValueType::sll_node_state_;
    } else {
      return obj.template GetContainableByTag<TagType>().sll_node_state_;
    }
  }

  using NodeState =
      std::decay_t<std::invoke_result_t<decltype(node_state), typename PtrTraits::RefType>>;
};

// SinglyLinkedListable<PtrType>
//
// A helper class which makes it simple to exist on a singly linked list.
// Simply derive your object from SinglyLinkedListable and you are done.
template <typename PtrType_, NodeOptions Options = NodeOptions::None,
          typename TagType_ = DefaultObjectTag>
struct SinglyLinkedListable {
 public:
  using PtrType = PtrType_;
  using TagType = TagType_;
  static constexpr NodeOptions kNodeOptions = Options;

  bool InContainer() const {
    using Node = SinglyLinkedListable<PtrType, Options, TagType>;
    return Node::sll_node_state_.InContainer();
  }

 private:
  friend struct DefaultSinglyLinkedListTraits<PtrType, TagType>;
  SinglyLinkedListNodeState<PtrType, Options> sll_node_state_;
};

template <typename PtrType_, typename TagType_ = DefaultObjectTag,
          SizeOrder ListSizeOrder_ = SizeOrder::N,
          typename NodeTraits_ = DefaultSinglyLinkedListTraits<PtrType_, TagType_>>
class __POINTER(PtrType_) SinglyLinkedList : private internal::SizeTracker<ListSizeOrder_> {
 private:
  // Private fwd decls of the iterator implementation.
  template <typename IterTraits>
  class iterator_impl;
  struct iterator_traits;
  struct const_iterator_traits;

 public:
  // Aliases used to reduce verbosity and expose types/traits to tests
  static constexpr SizeOrder ListSizeOrder = ListSizeOrder_;
  using PtrType = PtrType_;
  using TagType = TagType_;
  using NodeTraits = NodeTraits_;

  using PtrTraits = internal::ContainerPtrTraits<PtrType_>;
  using RawPtrType = typename PtrTraits::RawPtrType;
  using ValueType = typename PtrTraits::ValueType;
  using RefType = typename PtrTraits::RefType;
  using CheckerType = ::fbl::tests::intrusive_containers::SinglyLinkedListChecker;
  using ContainerType = SinglyLinkedList<PtrType_, TagType_, ListSizeOrder_, NodeTraits_>;

  // Declarations of the standard iterator types.
  using iterator = iterator_impl<iterator_traits>;
  using const_iterator = iterator_impl<const_iterator_traits>;

  // Singly linked lists do not support constant order erase (erase using an
  // iterator or direct object reference).
  static constexpr bool SupportsConstantOrderErase = false;
  static constexpr bool SupportsConstantOrderSize = (ListSizeOrder == SizeOrder::Constant);
  static constexpr bool IsAssociative = false;
  static constexpr bool IsSequenced = true;

  // Default construction gives an empty list.
  constexpr SinglyLinkedList() noexcept {
    using NodeState = internal::node_state_t<NodeTraits, RefType>;

    // Make certain that the type of pointer we are expected to manage matches
    // the type of pointer that our Node type expects to manage.
    static_assert(std::is_same_v<PtrType, typename NodeState::PtrType>,
                  "SinglyLinkedList's pointer type must match its Node's pointerType");

    // SinglyLinkedList does not currently support direct remove-from-container.
    static_assert(!(NodeState::kNodeOptions & NodeOptions::AllowRemoveFromContainer),
                  "SinglyLinkedList does not support nodes which allow RemoveFromContainer.");
  }

  // Rvalue construction is permitted, but will result in the move of the list
  // contents from one instance of the list to the other (even for unmanaged
  // pointers).
  //
  // Make sure to expand our default constructor as well in order to pick up the
  // static asserts that we put there.
  SinglyLinkedList(SinglyLinkedList&& other_list) noexcept : SinglyLinkedList() {
    swap(other_list);
  }

  // Rvalue assignment is permitted for managed lists, and when the target is
  // an empty list of unmanaged pointers.  Like Rvalue construction, it will
  // result in the move of the source contents to the destination.
  SinglyLinkedList& operator=(SinglyLinkedList&& other_list) {
    ZX_DEBUG_ASSERT(PtrTraits::IsManaged || is_empty());

    clear();
    swap(other_list);

    return *this;
  }

  ~SinglyLinkedList() {
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

  // make_iterator : construct an iterator out of a reference to an object.
  iterator make_iterator(ValueType& obj) { return iterator(&obj); }
  const_iterator make_iterator(const ValueType& obj) const {
    return const_iterator(&const_cast<ValueType&>(obj));
  }

  // is_empty
  //
  // True if the list has at least one element in it, false otherwise.
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

  // push_front
  //
  // Push an element onto the front of the lists.  Lvalue and Rvalue
  // versions are supplied in order to support move semantics.  It
  // is an error to attempt to push a nullptr instance of PtrType.
  void push_front(const PtrType& ptr) { push_front(PtrType(ptr)); }
  void push_front(PtrType&& ptr) {
    ZX_DEBUG_ASSERT(ptr != nullptr);

    auto& ptr_ns = NodeTraits::node_state(*ptr);
    ZX_DEBUG_ASSERT(!ptr_ns.InContainer());

    ptr_ns.next_ = head_;
    head_ = PtrTraits::Leak(ptr);
    this->IncSizeTracker(1);
  }

  // insert_after
  //
  // Insert an element after iter in the list.  It is an error to attempt to
  // push a nullptr instance of PtrType, or to attempt to push with iter ==
  // end().
  void insert_after(const iterator& iter, const PtrType& ptr) { insert_after(iter, PtrType(ptr)); }
  void insert_after(const iterator& iter, PtrType&& ptr) {
    ZX_DEBUG_ASSERT(iter.IsValid());
    ZX_DEBUG_ASSERT(ptr != nullptr);

    auto& iter_ns = NodeTraits::node_state(*iter.node_);
    auto& ptr_ns = NodeTraits::node_state(*ptr);
    ZX_DEBUG_ASSERT(!ptr_ns.InContainer());

    ptr_ns.next_ = iter_ns.next_;
    iter_ns.next_ = PtrTraits::Leak(ptr);
    this->IncSizeTracker(1);
  }

  // pop_front
  //
  // Removes the head of the list and transfer the pointer to the
  // caller.  If the list is empty, return a nullptr instance of
  // PtrType.
  PtrType pop_front() {
    if (is_empty())
      return PtrType(nullptr);

    auto& head_ns = NodeTraits::node_state(*head_);
    PtrType ret = PtrTraits::Reclaim(head_);

    head_ = head_ns.next_;
    head_ns.next_ = nullptr;
    this->DecSizeTracker(1);

    return ret;
  }

  // clear
  //
  // Clear out the list, unlinking all of the elements in the process.  For
  // managed pointer types, this will release all references held by the list
  // to the objects which were in it.
  void clear() {
    while (!is_empty()) {
      auto& head_ns = NodeTraits::node_state(*head_);
      RawPtrType tmp = head_;
      head_ = head_ns.next_;
      head_ns.next_ = nullptr;
      PtrTraits::Reclaim(tmp);
    }
    this->ResetSizeTracker();
  }

  // clear_unsafe
  //
  // A special clear operation which just resets the internal container
  // structure, but leaves all of the node-state(s) of the current element(s)
  // alone.
  //
  // Only usable with containers of unmanaged pointers (Very Bad things can
  // happen if you try this with containers of managed pointers) whose nodes
  // have the NodeOptions::AllowClearUnsafe option set.
  //
  // Note: While this can be useful in special cases (such as resetting a free
  // list for a pool/slab allocator during destruction), you normally do not
  // want this behavior.  Think carefully before calling this!
  void clear_unsafe() {
    static_assert(PtrTraits::IsManaged == false,
                  "clear_unsafe is not allowed for containers of managed pointers");
    static_assert(NodeTraits::NodeState::kNodeOptions & NodeOptions::AllowClearUnsafe,
                  "Container does not support clear_unsafe.  Consider adding "
                  "NodeOptions::AllowClearUnsafe to your node storage.");

    head_ = sentinel();
    this->ResetSizeTracker();
  }

  // erase_next
  //
  // Remove the element in the list which follows iter and return a pointer to
  // the removed element.  If there is no element in the list which follows
  // iter, return a nullptr instance of PtrType.  It is an error to attempt to
  // erase_next an invalid iterator (either an uninitialized iterator, or an
  // iterator which is equal to end())
  PtrType erase_next(const iterator& iter) {
    ZX_DEBUG_ASSERT(iter.IsValid());
    auto& iter_ns = NodeTraits::node_state(*iter);

    if (internal::is_sentinel_ptr(iter_ns.next_))
      return PtrType(nullptr);

    auto& next_ns = NodeTraits::node_state(*iter_ns.next_);

    PtrType ret = PtrTraits::Reclaim(iter_ns.next_);
    iter_ns.next_ = next_ns.next_;
    next_ns.next_ = nullptr;
    this->DecSizeTracker(1);
    return ret;
  }

  // swap
  //
  // swaps the contest of two lists.
  void swap(SinglyLinkedList& other) {
    auto tmp = head_;
    head_ = other.head_;
    other.head_ = tmp;
    this->SwapSizeTracker(other);
  }

  // size_slow
  //
  // count the elements in the list in O(n) fashion
  size_t size_slow() const {
    // It is illegal to call this if the user requested constant order size
    // operations.
    static_assert(
        ListSizeOrder == SizeOrder::N,
        "size_slow is only allowed when using a list which has O(N) size!  Use size() instead.");

    size_t size = 0;

    for (auto iter = cbegin(); iter != cend(); ++iter)
      size++;

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
  // removed element.  Return nullptr if no member satisfies the predicate.
  template <typename UnaryFn>
  PtrType erase_if(UnaryFn fn) {
    using ConstRefType = typename PtrTraits::ConstRefType;

    if (is_empty())
      return PtrType(nullptr);

    auto iter = begin();
    if (fn(static_cast<ConstRefType>(*iter)))
      return pop_front();

    for (auto prev = iter++; iter != end(); prev = iter++) {
      if (fn(static_cast<ConstRefType>(*iter)))
        return erase_next(prev);
    }

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
    using ConstRefType = typename PtrTraits::ConstRefType;
    ZX_DEBUG_ASSERT(ptr != nullptr);

    auto& ptr_ns = NodeTraits::node_state(*ptr);
    ZX_DEBUG_ASSERT(!ptr_ns.InContainer());

    auto iter = begin();
    if (iter.IsValid()) {
      RawPtrType* prev_next_ptr = &head_;

      while (iter.IsValid()) {
        auto& iter_ns = NodeTraits::node_state(*iter);

        if (fn(static_cast<ConstRefType>(*iter))) {
          PtrType new_ref = ptr;
          RawPtrType replaced;

          replaced = *prev_next_ptr;
          *prev_next_ptr = PtrTraits::Leak(new_ref);
          ptr_ns.next_ = iter_ns.next_;
          iter_ns.next_ = nullptr;

          return PtrTraits::Reclaim(replaced);
        }

        prev_next_ptr = &iter_ns.next_;
        ++iter;
      }
    }

    return nullptr;
  }

  // replace_if (move)
  //
  // Same as the copy version, except that if no member satisfies the
  // predicate, the original reference is returned instead of nullptr.
  template <typename UnaryFn>
  PtrType replace_if(UnaryFn fn, PtrType&& ptr) {
    using ConstRefType = typename PtrTraits::ConstRefType;
    ZX_DEBUG_ASSERT(ptr != nullptr);

    auto& ptr_ns = NodeTraits::node_state(*ptr);
    ZX_DEBUG_ASSERT(!ptr_ns.InContainer());

    auto iter = begin();
    if (iter.IsValid()) {
      RawPtrType* prev_next_ptr = &head_;

      while (iter.IsValid()) {
        auto& iter_ns = NodeTraits::node_state(*iter);

        if (fn(static_cast<ConstRefType>(*iter))) {
          RawPtrType replaced;

          replaced = *prev_next_ptr;
          *prev_next_ptr = PtrTraits::Leak(ptr);
          ptr_ns.next_ = iter_ns.next_;
          iter_ns.next_ = nullptr;

          return PtrTraits::Reclaim(replaced);
        }

        prev_next_ptr = &iter_ns.next_;
        ++iter;
      }
    }

    return std::move(ptr);
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
                  "split_after is not allowed for SizedSinglyLinkedLists");
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
    // We have 2 pointers we need to update in total.
    //
    // ret.head : needs to point to B, which is the new head of ret
    // A.next   : A is the new tail.  Next becomes this.sentinel();
    //
    // Thankfully, singly linked lists do not support reverse iteration, which
    // means that their sentinels are all the same.  This means that we don't
    // actually have to go and fixup ret.tail.next (which would force an O(n)
    // operation to find the tail).
    ContainerType ret;

    ret.head_ = A_ns.next_;
    A_ns.next_ = this->sentinel();

    return ret;
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

    bool IsValid() const { return (node_ != nullptr) && !internal::is_sentinel_ptr(node_); }
    bool operator==(const iterator_impl& other) const { return node_ == other.node_; }
    bool operator!=(const iterator_impl& other) const { return node_ != other.node_; }

    // Prefix
    iterator_impl& operator++() {
      if (!IsValid())
        return *this;

      node_ = NodeTraits::node_state(*node_).next_;

      return *this;
    }

    // Postfix
    iterator_impl operator++(int) {
      iterator_impl ret(*this);
      ++(*this);
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
    friend class SinglyLinkedList<PtrType_, TagType_, ListSizeOrder_, NodeTraits_>;

    explicit iterator_impl(typename IterTraits::RawPtrType node)
        : node_(const_cast<typename PtrTraits::RawPtrType>(node)) {}

    typename PtrTraits::RawPtrType node_ = nullptr;
  };

  // The test framework's 'checker' class is our friend.
  friend CheckerType;

  // move semantics only
  SinglyLinkedList(const SinglyLinkedList&) = delete;
  SinglyLinkedList& operator=(const SinglyLinkedList&) = delete;

  // Note: the sentinel value we use for singly linked list is a bit different
  // from the sentinel value we use for everything else.  Instead of being the
  // this pointer of the container with the sentinel bit set, the sentinel is
  // just the bit with nothing else (aka; nullptr | kContainerSentinelBit).
  //
  // The reasons which drive this decision are as follows.
  // 1) When swapping lists, if the sentinel value was list specific, we would
  //    need to update the sentinel values at the end of each list.  This would
  //    be an O(n) operation for a SLL, whereas it is an O(1) operation for
  //    every other container.
  // 2) The sentinel value used by a list cannot simply be nullptr, or the
  //    node state for an element which is list-able would not be able to
  //    distinguish between an element which was not InContainer() and one
  //    which was InContainer, but located at the end of the list.
  constexpr RawPtrType sentinel() const { return internal::make_sentinel<RawPtrType>(nullptr); }

  // State consists of just a head pointer.
  RawPtrType head_ = sentinel();
};

// SizedSinglyLinkedList<> is an alias for a SinglyLinkedList<> which keeps
// track of it's size internally so that it may be accessed in O(1) time.
//
template <typename PtrType, typename TagType = DefaultObjectTag,
          typename NodeTraits = DefaultSinglyLinkedListTraits<PtrType, TagType>>
using SizedSinglyLinkedList = SinglyLinkedList<PtrType, TagType, SizeOrder::Constant, NodeTraits>;

// SinglyLinkedListCustomTraits<> is an alias for a SinglyLinkedList<> which makes is easier to
// define a SinglyLinkedList which uses custom node traits.  It defaults to O(n) size, and will not
// allow users to use a non-default object tag, since lists which use custom node traits are
// required to use the default tag.
//
template <typename PtrType, typename NodeTraits, SizeOrder ListSizeOrder = SizeOrder::N>
using SinglyLinkedListCustomTraits =
    SinglyLinkedList<PtrType, DefaultObjectTag, ListSizeOrder, NodeTraits>;

// TaggedSinglyLinkedList<> is intended for use with ContainableBaseClasses<>.
//
// For an easy way to allow instances of your class to live in multiple
// intrusive containers at once, simply derive from
// ContainableBaseClasses<YourContainables<PtrType, TagType>...> and then use
// this template instead of SinglyLinkedList<> as the container, passing the same tag
// type you used earlier as the third parameter.
//
// See comments on ContainableBaseClasses<> in fbl/intrusive_container_utils.h
// for more details.
//
template <typename PtrType, typename TagType>
using TaggedSinglyLinkedList = SinglyLinkedList<PtrType, TagType, SizeOrder::N,
                                                DefaultSinglyLinkedListTraits<PtrType, TagType>>;

template <typename PtrType, typename TagType, NodeOptions Options = NodeOptions::None>
using TaggedSinglyLinkedListable = SinglyLinkedListable<PtrType, Options, TagType>;

}  // namespace fbl

#endif  // FBL_INTRUSIVE_SINGLE_LIST_H_
