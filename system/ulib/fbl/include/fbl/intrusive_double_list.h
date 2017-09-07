// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>
#include <fbl/intrusive_pointer_traits.h>
#include <fbl/macros.h>

// Usage and Implementation Notes:
//
// fbl::DoublyLinkedList<> is a templated intrusive container class which
// allows users to manage doubly linked lists of objects.
//
// fbl::DoublyLinkedList<> follows the same patterns as
// fbl::SinglyLinkedList<> and implements a superset of the functionality
// (including support for managed pointer types).  Please refer to the "Usage
// Notes" section of utils/intrusive_single_list.h for details.
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
}  // namespace tests
}  // namespace intrusive_containers

template <typename T>
struct DoublyLinkedListNodeState {
    using PtrTraits = internal::ContainerPtrTraits<T>;
    typename PtrTraits::PtrType    next_ = nullptr;
    typename PtrTraits::RawPtrType prev_ = nullptr;

    bool IsValid() const     { return ((next_ == nullptr) == (prev_ == nullptr)); }
    bool InContainer() const { return ((next_ != nullptr) && (prev_ != nullptr)); }
};

template <typename T>
struct DefaultDoublyLinkedListTraits {
    using PtrTraits = internal::ContainerPtrTraits<T>;
    static DoublyLinkedListNodeState<T>& node_state(typename PtrTraits::RefType obj) {
        return obj.dll_node_state_;
    }
};

template <typename T>
struct DoublyLinkedListable {
public:
    bool InContainer() const { return dll_node_state_.InContainer(); }

private:
    friend struct DefaultDoublyLinkedListTraits<T>;
    DoublyLinkedListNodeState<T> dll_node_state_;
};

template <typename T, typename _NodeTraits = DefaultDoublyLinkedListTraits<T>>
class DoublyLinkedList {
private:
    // Private fwd decls of the iterator implementation.
    template <typename IterTraits> class iterator_impl;
    struct iterator_traits;
    struct const_iterator_traits;

public:
    // Aliases used to reduce verbosity and expose types/traits to tests
    using PtrTraits     = internal::ContainerPtrTraits<T>;
    using NodeTraits    = _NodeTraits;
    using NodeState     = DoublyLinkedListNodeState<T>;
    using PtrType       = typename PtrTraits::PtrType;
    using RawPtrType    = typename PtrTraits::RawPtrType;
    using RawPtrTraits  = internal::ContainerPtrTraits<RawPtrType>;
    using ValueType     = typename PtrTraits::ValueType;
    using CheckerType   = ::fbl::tests::intrusive_containers::DoublyLinkedListChecker;
    using ContainerType = DoublyLinkedList<T, NodeTraits>;

    // Declarations of the standard iterator types.
    using iterator       = iterator_impl<iterator_traits>;
    using const_iterator = iterator_impl<const_iterator_traits>;

    // Doubly linked lists support constant order erase (erase using an iterator
    // or direct object reference).
    static constexpr bool SupportsConstantOrderErase = true;
    static constexpr bool SupportsConstantOrderSize = false;
    static constexpr bool IsAssociative = false;
    static constexpr bool IsSequenced = true;

    // Default construction gives an empty list.
    constexpr DoublyLinkedList() { }

    // Rvalue construction is permitted, but will result in the move of the list
    // contents from one instance of the list to the other (even for unmanaged
    // pointers)
    DoublyLinkedList(DoublyLinkedList<T, NodeTraits>&& other_list) {
        swap(other_list);
    }

    // Rvalue assignment is permitted for managed lists, and when the target is
    // an empty list of unmanaged pointers.  Like Rvalue construction, it will
    // result in the move of the source contents to the destination.
    DoublyLinkedList& operator=(DoublyLinkedList&& other_list) {
        MX_DEBUG_ASSERT(PtrTraits::IsManaged || is_empty());

        clear();
        swap(other_list);

        return *this;
    }

    ~DoublyLinkedList() {
        // It is considered an error to allow a list of unmanaged pointers to
        // destruct if there are still elements in it.  Managed pointer lists
        // will automatically release their references to their elements.
        MX_DEBUG_ASSERT(PtrTraits::IsManaged || is_empty());
        clear();
        PtrTraits::DetachSentinel(head_);
    }

    // Standard begin/end, cbegin/cend iterator accessors.
    iterator        begin()       { return iterator(PtrTraits::GetRaw(head_)); }
    const_iterator  begin() const { return const_iterator(PtrTraits::GetRaw(head_)); }
    const_iterator cbegin() const { return const_iterator(PtrTraits::GetRaw(head_)); }

    iterator          end()       { return iterator(sentinel()); }
    const_iterator    end() const { return const_iterator(sentinel()); }
    const_iterator   cend() const { return const_iterator(sentinel()); }

    // make_iterator : construct an iterator out of a pointer to an object
    iterator make_iterator(ValueType& obj) { return iterator(&obj); }

    // is_empty : True if the list has at least one element in it, false otherwise.
    bool is_empty() const { return PtrTraits::IsSentinel(head_); }

    // front
    //
    // Return a reference to the element at the front of the list without
    // removing it.  It is an error to call front on an empty list.
    typename PtrTraits::RefType      front()       { MX_DEBUG_ASSERT(!is_empty()); return *head_; }
    typename PtrTraits::ConstRefType front() const { MX_DEBUG_ASSERT(!is_empty()); return *head_; }

    // back
    //
    // Return a reference to the element at the back of the list without
    // removing it.  It is an error to call back on an empty list.
    typename PtrTraits::RefType back() {
        MX_DEBUG_ASSERT(!is_empty());
        return *(NodeTraits::node_state(*head_).prev_);
    }

    typename PtrTraits::ConstRefType back() const {
        MX_DEBUG_ASSERT(!is_empty());
        return *(NodeTraits::node_state(*head_).prev_);
    }

    // push_front : Push an element onto the front of the list.
    void push_front(const PtrType& ptr) { push_front(PtrType(ptr)); }
    void push_front(PtrType&& ptr) { internal_insert(PtrTraits::GetRaw(head_), fbl::move(ptr)); }

    // push_back : Push an element onto the end of the list.
    void push_back(const PtrType& ptr) { push_back(PtrType(ptr)); }
    void push_back(PtrType&& ptr) { internal_insert(sentinel(), fbl::move(ptr)); }

    // insert : Insert an element before iter in the list.
    void insert(const iterator& iter, const PtrType&  ptr) { insert(iter, PtrType(ptr)); }
    void insert(const iterator& iter, PtrType&& ptr) {
        internal_insert(iter.node_, fbl::move(ptr));
    }

    void insert(ValueType& before, const PtrType& ptr) { insert(before, PtrType(ptr)); }
    void insert(ValueType& before, PtrType&& ptr) {
        internal_insert(&before, fbl::move(ptr));
    }

    // splice : Splice another list before iter in this list.
    void splice(const iterator& iter, DoublyLinkedList& other_list)  {
        auto before = iter.node_;
        MX_DEBUG_ASSERT(before != nullptr);
        MX_DEBUG_ASSERT(head_  != nullptr);

        if (other_list.is_empty()) {
            return;
        }
        if (is_empty()) {
            MX_DEBUG_ASSERT(before == sentinel());
            MX_DEBUG_ASSERT(before == PtrTraits::GetRaw(head_));
            swap(other_list);
            return;
        }

        // If we are being inserted before the sentinel, the we are the new
        // tail, and the node_state which contains the prev pointer we need to
        // update is head's.  Otherwise, it is the node_state of the node we are
        // being inserted before.
        auto& prev_ns  = NodeTraits::node_state(PtrTraits::IsSentinel(before) ? *head_ : *before);
        auto& tgt_prev = prev_ns.prev_;

        // If we are being inserted before the head, then we need to update the
        // managed head pointer.  Otherwise, we need to update the next pointer
        // of the node which is about to come before us.
        auto& tgt_next = (PtrTraits::GetRaw(head_) == before)
                       ? head_
                       : PtrTraits::IsSentinel(before) ? NodeTraits::node_state(*tail()).next_
                                                       : NodeTraits::node_state(*tgt_prev).next_;

        auto& other_head_ns  = NodeTraits::node_state(*other_list.head_);
        auto other_tail = other_list.tail();
        auto& other_tail_ns = NodeTraits::node_state(*other_tail);

        // Update the prev pointers.
        other_head_ns.prev_ = tgt_prev;
        tgt_prev            = other_tail;

        // Update the next pointers.
        PtrTraits::DetachSentinel(other_tail_ns.next_);
        other_tail_ns.next_ = PtrTraits::Take(tgt_next);
        tgt_next            = PtrTraits::Take(other_list.head_);

        other_list.head_ = PtrTraits::MakeSentinel(&other_list);
    }

    // insert_after : Insert an element after iter in the list.
    //
    // Note: It is an error to attempt to push a nullptr instance of PtrType, or
    // to attempt to push with iter == end().
    void insert_after(const iterator& iter, const PtrType& ptr) {
        insert_after(iter, PtrType(ptr));
    }
    void insert_after(const iterator& iter, PtrType&& ptr) {
        MX_DEBUG_ASSERT(iter.IsValid());

        auto& ns = NodeTraits::node_state(*iter.node_);
        internal_insert(PtrTraits::GetRaw(ns.next_), fbl::move(ptr));
    }

    // pop_front and pop_back
    //
    // Removes either the head or the tail of the list and transfers the pointer
    // to the caller.  If the list is empty, return a nullptr instance of
    // PtrType.
    PtrType pop_front() { return internal_erase(PtrTraits::GetRaw(head_)); }
    PtrType pop_back()  { return internal_erase(tail()); }

    // erase and erase_next
    //
    // Erase the element either at the provided iterator, or immediately after
    // the provided iterator.  Remove the element in the list either at iter, or
    // which follows iter.  If there is no element in the list at this position
    // (iter is end()), return a nullptr instance of PtrType.  It is an error to
    // attempt to use an iterator from a different instance of this list type to
    // attempt to erase a node.
    PtrType erase(ValueType& obj)       { return internal_erase(&obj); }
    PtrType erase(const iterator& iter) { return internal_erase(iter.node_); }

    PtrType erase_next(const iterator& iter) {
        if (!iter.IsValid())
            return PtrType(nullptr);

        auto& ns = NodeTraits::node_state(*iter.node_);
        if (PtrTraits::IsSentinel(ns.next_)) {
            MX_DEBUG_ASSERT(sentinel() == PtrTraits::GetRaw(ns.next_));
            return PtrType(nullptr);
        }

        return internal_erase(PtrTraits::GetRaw(ns.next_));
    }

    // clear
    //
    // Clear out the list, unlinking all of the elements in the process.  For
    // managed pointer types, this will release all references held by the list
    // to the objects which were in it.
    void clear() {
        while (!is_empty()) {
            auto& head_ns = NodeTraits::node_state(*head_);

            PtrTraits::Swap(head_, head_ns.next_);
            head_ns.prev_ = nullptr;
            PtrTraits::Take(head_ns.next_);
        }
    }

    // clear_unsafe
    //
    // See comments in fbl/intrusive_single_list.h
    // Think carefully before calling this!
    void clear_unsafe() {
        static_assert(PtrTraits::IsManaged == false,
                     "clear_unsafe is not allowed for containers of managed pointers");
        head_ = PtrTraits::MakeSentinel(sentinel());
    }

    // swap : swaps the contest of two lists.
    void swap(DoublyLinkedList<T, NodeTraits>& other) {
        PtrTraits::Swap(head_, other.head_);

        PtrType& sentinel_ptr = is_empty() ? head_ : NodeTraits::node_state(*tail()).next_;
        PtrType& other_sentinel_ptr = other.is_empty()
                                    ? other.head_
                                    : NodeTraits::node_state(*other.tail()).next_;
        PtrTraits::Swap(sentinel_ptr, other_sentinel_ptr);
    }

    // size_slow : count the elements in the list in O(n) fashion
    size_t size_slow() const {
        size_t size = 0;

        for (auto iter = cbegin(); iter != cend(); ++iter) {
            size++;
        }

        return size;
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
            return fbl::move(ptr);

        return internal_swap(*iter, fbl::move(ptr));
    }

private:
    // The traits of a non-const iterator
    struct iterator_traits {
        using RefType    = typename PtrTraits::RefType;
        using RawPtrType = typename PtrTraits::RawPtrType;
    };

    // The traits of a const iterator
    struct const_iterator_traits {
        using RefType    = typename PtrTraits::ConstRefType;
        using RawPtrType = typename PtrTraits::ConstRawPtrType;
    };

    // The shared implementation of the iterator
    template <class IterTraits>
    class iterator_impl {
    public:
        iterator_impl() { }
        iterator_impl(const iterator_impl& other) { node_ = other.node_; }

        iterator_impl& operator=(const iterator_impl& other) {
            node_ = other.node_;
            return *this;
        }

        bool IsValid() const { return !PtrTraits::IsSentinel(node_) && (node_ != nullptr); }
        bool operator==(const iterator_impl& other) const { return node_ == other.node_; }
        bool operator!=(const iterator_impl& other) const { return node_ != other.node_; }

        // Prefix
        iterator_impl& operator++() {
            if (IsValid()) {
                auto& ns = NodeTraits::node_state(*node_);
                node_ = PtrTraits::GetRaw(ns.next_);
                MX_DEBUG_ASSERT(node_ != nullptr);
            }

            return *this;
        }

        iterator_impl& operator--() {
            if (node_) {
                if (PtrTraits::IsSentinel(node_)) {
                    ListPtrType list = GetList();
                    node_ = list->tail();
                } else {
                    auto& ns = NodeTraits::node_state(*node_);
                    node_ = ns.prev_;
                    MX_DEBUG_ASSERT(node_ != nullptr);

                    // If backing up would put us at a node whose next pointer
                    // is a sentinel, the we have looped around the start of the
                    // list and are currently pointed at the tail of the list.
                    // Reset our internal pointer to point to the sentinel value
                    // instead.
                    auto& new_ns = NodeTraits::node_state(*node_);
                    if (PtrTraits::IsSentinel(new_ns.next_))
                        node_ = PtrTraits::GetRaw(new_ns.next_);
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

        typename PtrTraits::PtrType CopyPointer() {
            return IsValid() ? PtrTraits::Copy(node_) : nullptr;
        }

        typename IterTraits::RefType operator*() const {
            MX_DEBUG_ASSERT(IsValid());
            return *node_;
        }

        typename IterTraits::RawPtrType operator->() const {
            MX_DEBUG_ASSERT(IsValid());
            return node_;
        }

    private:
        friend class DoublyLinkedList<T, NodeTraits>;
        using ListPtrType = const DoublyLinkedList<T, NodeTraits>*;

        iterator_impl(const typename PtrTraits::RawPtrType node)
            : node_(const_cast<typename PtrTraits::RawPtrType>(node)) { }

        ListPtrType GetList() const {
            return reinterpret_cast<ListPtrType>(
                    reinterpret_cast<uintptr_t>(node_) & ~internal::kContainerSentinelBit);
        }

        typename PtrTraits::RawPtrType node_ = nullptr;
    };

    // The test framework's 'checker' class is our friend.
    friend CheckerType;

    // move semantics only
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(DoublyLinkedList);

    constexpr RawPtrType sentinel() const {
        return reinterpret_cast<RawPtrType>(
                reinterpret_cast<uintptr_t>(this) | internal::kContainerSentinelBit);
    }

    void internal_insert(RawPtrType before, PtrType&& ptr) {
        MX_DEBUG_ASSERT(ptr    != nullptr);
        MX_DEBUG_ASSERT(before != nullptr);
        MX_DEBUG_ASSERT(head_  != nullptr);

        auto& ptr_ns = NodeTraits::node_state(*ptr);
        MX_DEBUG_ASSERT((ptr_ns.prev_ == nullptr) && (ptr_ns.next_ == nullptr));

        // Handle the (slightly) special case of an empty list.
        if (is_empty()) {
            MX_DEBUG_ASSERT(before == sentinel());
            MX_DEBUG_ASSERT(before == PtrTraits::GetRaw(head_));

            ptr_ns.prev_ = PtrTraits::GetRaw(ptr);
            ptr_ns.next_ = fbl::move(head_);
            head_        = fbl::move(ptr);

            return;
        }

        // If we are being inserted before the sentinel, the we are the new
        // tail, and the node_state which contains the prev pointer we need to
        // update is head's.  Otherwise, it is the node_state of the node we are
        // being inserted before.
        auto& prev_ns  = NodeTraits::node_state(PtrTraits::IsSentinel(before) ? *head_ : *before);
        auto& tgt_prev = prev_ns.prev_;

        // If we are being inserted before the head, then we need to update the
        // managed head pointer.  Otherwise, we need to update the next pointer
        // of the node which is about to come before us.
        auto& tgt_next = (PtrTraits::GetRaw(head_) == before)
                       ? head_
                       : PtrTraits::IsSentinel(before) ? NodeTraits::node_state(*tail()).next_
                                                       : NodeTraits::node_state(*tgt_prev).next_;

        // Update the prev pointers.
        ptr_ns.prev_ = tgt_prev;
        tgt_prev     = PtrTraits::GetRaw(ptr);

        // Update the next pointers.
        ptr_ns.next_ = fbl::move(tgt_next);
        tgt_next     = fbl::move(ptr);
    }

    PtrType internal_erase(RawPtrType node) {
        if (!node || PtrTraits::IsSentinel(node))
            return PtrType(nullptr);

        auto& node_ns = NodeTraits::node_state(*node);
        MX_DEBUG_ASSERT((node_ns.prev_ != nullptr) && (node_ns.next_ != nullptr));

        // Find the prev pointer we need to update.  If we are removing the tail
        // of the list, the prev pointer is head_'s prev pointer.  Otherwise, it
        // is the prev pointer of the node which currently follows "ptr".
        auto& tgt_prev = PtrTraits::IsSentinel(node_ns.next_)
                       ? NodeTraits::node_state(*head_).prev_
                       : NodeTraits::node_state(*node_ns.next_).prev_;

        // Find the next pointer we need to update.  If we are removing the
        // head of the list, this is head_.  Otherwise it is the next pointer of
        // the node which comes before us in the list.
        auto& tgt_next = PtrTraits::GetRaw(head_) == node
                       ? head_
                       : NodeTraits::node_state(*node_ns.prev_).next_;

        tgt_prev = node_ns.prev_;
        node_ns.prev_ = nullptr;

        PtrTraits::Swap(tgt_next, node_ns.next_);
        return PtrTraits::Take(node_ns.next_);
    }

    PtrType internal_swap(typename PtrTraits::RefType node, PtrType&& ptr) {
        MX_DEBUG_ASSERT(ptr != nullptr);
        auto& ptr_ns = NodeTraits::node_state(*ptr);
        MX_DEBUG_ASSERT(!ptr_ns.InContainer());

        auto& node_ns = NodeTraits::node_state(node);
        MX_DEBUG_ASSERT(node_ns.InContainer());

        // Handle the case of there being only a single node in the list.
        MX_DEBUG_ASSERT(PtrTraits::IsValid(head_));
        if (PtrTraits::IsSentinel(NodeTraits::node_state(*head_).next_)) {
            MX_DEBUG_ASSERT(PtrTraits::GetRaw(head_) == &node);
            MX_DEBUG_ASSERT(PtrTraits::IsSentinel(node_ns.next_));
            MX_DEBUG_ASSERT(&node == node_ns.prev_);

            ptr_ns.prev_  = PtrTraits::GetRaw(ptr);
            node_ns.prev_ = nullptr;
            PtrTraits::Swap(ptr_ns.next_, node_ns.next_);
            PtrTraits::Swap(head_, ptr);

            return fbl::move(ptr);
        }

        // Find the prev pointer we need to update.  If we are swapping the tail
        // of the list, the prev pointer is head_'s prev pointer.  Otherwise, it
        // is the prev pointer of the node which currently follows "ptr".
        auto& tgt_prev = PtrTraits::IsSentinel(node_ns.next_)
                       ? NodeTraits::node_state(*head_).prev_
                       : NodeTraits::node_state(*node_ns.next_).prev_;

        // Find the next pointer we need to update.  If we are swapping the
        // head of the list, this is head_.  Otherwise it is the next pointer of
        // the node which comes before us in the list.
        auto& tgt_next = PtrTraits::GetRaw(head_) == &node
                       ? head_
                       : NodeTraits::node_state(*node_ns.prev_).next_;

        PtrTraits::Swap(ptr_ns.next_, node_ns.next_);
        RawPtrTraits::Swap(ptr_ns.prev_, node_ns.prev_);
        tgt_prev = PtrTraits::GetRaw(ptr);
        PtrTraits::Swap(tgt_next, ptr);

        return fbl::move(ptr);
    }

    RawPtrType tail() const {
        MX_DEBUG_ASSERT(head_ != nullptr);
        if (PtrTraits::IsSentinel(head_))
            return PtrTraits::GetRaw(head_);
        return NodeTraits::node_state(*head_).prev_;
    }

    // State consists of managed pointer to the head of the list.  Initially,
    // this is set to the special sentinel value, which allows iterators set to
    // this->end() to back up to the tail of the list.
    PtrType head_ = PtrTraits::MakeSentinel(this);
};

// Explicit declaration of constexpr storage.
template <typename T, typename NodeTraits>
constexpr bool DoublyLinkedList<T, NodeTraits>::SupportsConstantOrderErase;
template <typename T, typename NodeTraits>
constexpr bool DoublyLinkedList<T, NodeTraits>::SupportsConstantOrderSize;
template <typename T, typename NodeTraits>
constexpr bool DoublyLinkedList<T, NodeTraits>::IsAssociative;
template <typename T, typename NodeTraits>
constexpr bool DoublyLinkedList<T, NodeTraits>::IsSequenced;

}  // namespace fbl
