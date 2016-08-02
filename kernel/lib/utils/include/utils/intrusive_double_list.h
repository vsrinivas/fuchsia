// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <utils/intrusive_pointer_traits.h>

// Usage and Implementation Notes:
//
// utils::DoublyLinkedList<> is a templated intrusive container class which
// allows users to manage doubly linked lists of objects.
//
// utils::DoublyLinkedList<> follows the same patterns as
// utils::SinglyLinkedList<> and implements a superset of the functionality
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
// is empty.  Each object on the list has a DoubleLinkedListNodeState<> which
// contains one raw pointer (prev) and one managed pointer (next) which are
// arranged in a ring.  The tail of a non-empty list can be found in O(k) time
// by following the prev pointer of the head node of the list.
namespace utils {

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
    friend class DefaultDoublyLinkedListTraits<T>;
    DoublyLinkedListNodeState<T> dll_node_state_;
};

template <typename T, typename _NodeTraits = DefaultDoublyLinkedListTraits<T>>
class DoublyLinkedList {
private:
    // Private fwd decls of the iterator implementation.
    template <typename IterTraits> class iterator_impl;
    class iterator_traits;
    class const_iterator_traits;

public:
    // Aliases used to reduce verbosity and expose types/traits to tests
    using PtrTraits  = internal::ContainerPtrTraits<T>;
    using NodeTraits = _NodeTraits;
    using NodeState  = DoublyLinkedListNodeState<T>;
    using PtrType    = typename PtrTraits::PtrType;
    using RawPtrType = typename PtrTraits::RawPtrType;
    using ValueType  = typename PtrTraits::ValueType;

    // Declarations of the standard iterator types.
    using iterator       = iterator_impl<iterator_traits>;
    using const_iterator = iterator_impl<const_iterator_traits>;

    // Default construction gives an empty list.
    constexpr DoublyLinkedList() { }

    // Rvalue construction is permitted, but will result in the move of the list
    // contents from one instance of the list to the other (even for unmanaged
    // pointers)
    explicit DoublyLinkedList(DoublyLinkedList<T, NodeTraits>&& other_list) {
        swap(other_list);
    }

    // Rvalue assignment is permitted for managed lists, and when the target is
    // an empty list of unmanaged pointers.  Like Rvalue construction, it will
    // result in the move of the source contents to the destination.
    DoublyLinkedList& operator=(DoublyLinkedList&& other_list) {
        DEBUG_ASSERT(PtrTraits::IsManaged || is_empty());

        clear();
        swap(other_list);

        return *this;
    }

    ~DoublyLinkedList() {
        // It is considered an error to allow a list of unmanaged pointers to
        // destruct of there are still elements in it.  Managed pointer lists
        // will automatically release their references to their elements.
        DEBUG_ASSERT(PtrTraits::IsManaged || is_empty());
        clear();
    }

    // Standard begin/end, cbegin/cend iterator accessors.
    iterator        begin()       { return iterator(this, head_); }
    const_iterator  begin() const { return const_iterator(this, head_); }
    const_iterator cbegin() const { return const_iterator(this, head_); }

    iterator          end()       { return iterator(this, nullptr); }
    const_iterator    end() const { return const_iterator(this, nullptr); }
    const_iterator   cend() const { return const_iterator(this, nullptr); }

    // make_iterator : construct an iterator out of a pointer to an object
    iterator make_iterator(ValueType& obj) { return iterator(this, &obj); }

    // is_empty : True if the list has at least one element in it, false otherwise.
    bool is_empty() const { return head_ == nullptr; }

    // front
    //
    // Return a reference to the element at the front of the list without
    // removing it.  It is an error to call front on an empty list.
    typename PtrTraits::RefType      front()       { DEBUG_ASSERT(!is_empty()); return *head_; }
    typename PtrTraits::ConstRefType front() const { DEBUG_ASSERT(!is_empty()); return *head_; }

    // back
    //
    // Return a reference to the element at the back of the list without
    // removing it.  It is an error to call back on an empty list.
    typename PtrTraits::RefType back() {
        DEBUG_ASSERT(!is_empty());
        return *(NodeTraits::node_state(*head_).prev_);
    }

    typename PtrTraits::ConstRefType back() const {
        DEBUG_ASSERT(!is_empty());
        return *(NodeTraits::node_state(*head_).prev_);
    }

    // push_front : Push an element onto the front of the list.
    void push_front(const PtrType& ptr) { push_front(PtrType(ptr)); }
    void push_front(PtrType&& ptr)      { internal_insert(head_, utils::move(ptr)); }

    // push_back : Push an element onto the end of the list.
    void push_back(const PtrType& ptr) { push_back(PtrType(ptr)); }
    void push_back(PtrType&& ptr)      { internal_insert(nullptr, utils::move(ptr)); }

    // insert : Insert an element before iter in the list.
    void insert(const iterator& iter, const PtrType&  ptr) { insert(iter, PtrType(ptr)); }
    void insert(const iterator& iter, PtrType&& ptr) {
        DEBUG_ASSERT(iter.list_ == this);
        internal_insert(iter.node_, utils::move(ptr));
    }

    void insert(ValueType& before, const PtrType& ptr) { insert(before, PtrType(ptr)); }
    void insert(ValueType& before, PtrType&& ptr) {
        internal_insert(&before, utils::move(ptr));
    }

    // insert_after : Insert an element after iter in the list.
    //
    // Note: It is an error to attempt to push a nullptr instance of PtrType, or
    // to attempt to push with iter == end().
    void insert_after(const iterator& iter, const PtrType& ptr) {
        insert_after(iter, PtrType(ptr));
    }
    void insert_after(const iterator& iter, PtrType&& ptr) {
        DEBUG_ASSERT(iter.list_ == this);
        DEBUG_ASSERT(iter.node_);

        auto& ns = NodeTraits::node_state(*iter.node_);
        auto  next = PtrTraits::GetRaw(ns.next_);
        internal_insert(next == head_ ? nullptr : next, utils::move(ptr));
    }

    // pop_front and pop_back
    //
    // Removes either the head or the tail of the list and transfers the pointer
    // to the caller.  If the list is empty, return a nullptr instance of
    // PtrType.
    PtrType pop_front() { return internal_erase(head_); }
    PtrType pop_back()  { return internal_erase(tail()); }

    // erase and erase_next
    //
    // Erase the element either at the provided iterator, or immediately after
    // the provided iterator.  Remove the element in the list either at iter, or
    // which follows iter.  If there is no element in the list at this position
    // (iter is end()), return a nullptr instance of PtrType.  It is an error to
    // attempt to use an iterator from a different instance of this list type to
    // attempt to erase a node.
    PtrType erase(ValueType& obj) { return internal_erase(&obj); }
    PtrType erase(const iterator& iter) {
        DEBUG_ASSERT(this == iter.list_);
        return internal_erase(iter.node_);
    }


    PtrType erase_next(const iterator& iter) {
        DEBUG_ASSERT(this == iter.list_);

        if (!iter.node_)
            return PtrType(nullptr);

        auto& ns = NodeTraits::node_state(*iter.node_);
        auto  next = PtrTraits::GetRaw(ns.next_);

        if (!next)
            return PtrType(nullptr);

        return internal_erase(next);
    }

    void clear() {
        if (!is_empty()) {
            auto& head_ns = NodeTraits::node_state(*head_);
            auto& tail_ns = NodeTraits::node_state(*head_ns.prev_);
            DEBUG_ASSERT(tail_ns.next_);

            PtrType tmp = PtrTraits::Take(tail_ns.next_);
            do {
                auto& tmp_ns = NodeTraits::node_state(*tmp);
                tmp_ns.prev_ = nullptr;
                tmp = PtrTraits::Take(tmp_ns.next_);
            } while (tmp);

            head_ = nullptr;
        }
    }

    // swap : swaps the contest of two lists.
    void swap(DoublyLinkedList<T, NodeTraits>& other) {
        RawPtrType tmp = head_;
        head_       = other.head_;
        other.head_ = tmp;
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
    // 'fn' and return a const& to the PtrType in the list which refers to it.
    // Return nullptr if no member satisfies the predicate.
    template <typename UnaryFn>
    const PtrType& find_if(UnaryFn fn) {
        using ConstRefType = typename PtrTraits::ConstRefType;
        using RefType      = typename PtrTraits::RefType;

        for (RefType obj : *this) {
            if (fn(const_cast<ConstRefType>(obj))) {
                auto& obj_ns  = NodeTraits::node_state(obj);
                auto& prev_ns = NodeTraits::node_state(*obj_ns.prev_);
                return prev_ns.next_;
            }
        }

        static PtrType null_ptr(nullptr);
        return null_ptr;
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
        iterator_impl(const iterator_impl& other) { list_ = other.list_; node_ = other.node_; }

        iterator_impl& operator=(const iterator_impl& other) {
            list_ = other.list_;
            node_ = other.node_;
            return *this;
        }

        bool IsValid() const { return node_ != nullptr; }
        bool operator==(const iterator_impl& other) const { return node_ == other.node_; }
        bool operator!=(const iterator_impl& other) const { return node_ != other.node_; }

        // Prefix
        iterator_impl& operator++() {
            if (!node_)
                return *this;

            DEBUG_ASSERT(list_);

            auto& ns = NodeTraits::node_state(*node_);
            auto tmp = PtrTraits::GetRaw(ns.next_);
            node_    = (tmp == list_->head_) ? nullptr : tmp;

            return *this;
        }

        iterator_impl& operator--() {
            DEBUG_ASSERT(!node_ || list_);
            if (!list_ || (node_ == list_->head_)) {
                node_ = nullptr;
                return *this;
            }

            auto& ns = NodeTraits::node_state(*(node_ ? node_ : list_->head_));
            node_ = ns.prev_;

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

        typename PtrTraits::PtrType CopyPointer()          { return PtrTraits::Copy(node_); }
        typename IterTraits::RefType operator*()     const { DEBUG_ASSERT(node_); return *node_; }
        typename IterTraits::RawPtrType operator->() const { DEBUG_ASSERT(node_); return node_; }

    private:
        friend class DoublyLinkedList<T, NodeTraits>;
        using ListPtrType = const DoublyLinkedList<T, NodeTraits>*;

        iterator_impl(ListPtrType list, typename PtrTraits::RawPtrType node)
            : list_(list),
              node_(node) { }

        ListPtrType list_ = nullptr;
        typename PtrTraits::RawPtrType node_ = nullptr;
    };

    // Copy construction and Lvalue assignment are disallowed
    DoublyLinkedList(const DoublyLinkedList&) = delete;
    DoublyLinkedList& operator=(const DoublyLinkedList&) = delete;

    void take_first_object(PtrType&& ptr) {
        DEBUG_ASSERT(!head_);

        auto& ptr_ns = NodeTraits::node_state(*ptr);
        DEBUG_ASSERT(ptr_ns.prev_ == nullptr);
        DEBUG_ASSERT(ptr_ns.next_ == nullptr);

        head_        = PtrTraits::GetRaw(ptr);
        ptr_ns.prev_ = PtrTraits::GetRaw(ptr);
        ptr_ns.next_ = utils::move(ptr);
    }

    void internal_insert(RawPtrType before, PtrType&& ptr) {
        DEBUG_ASSERT(ptr != nullptr);

        if (!head_) {
            DEBUG_ASSERT(!before);
            take_first_object(utils::move(ptr));
            return;
        }

        auto& ptr_ns    = NodeTraits::node_state(*ptr);
        auto& before_ns = before ? NodeTraits::node_state(*before)
                                 : NodeTraits::node_state(*head_);
        auto& after_ns  = NodeTraits::node_state(*before_ns.prev_);

        DEBUG_ASSERT((   ptr_ns.prev_ == nullptr) && (   ptr_ns.next_ == nullptr));
        DEBUG_ASSERT((before_ns.prev_ != nullptr) && (before_ns.next_ != nullptr));
        DEBUG_ASSERT(( after_ns.prev_ != nullptr) && ( after_ns.next_ != nullptr));

        if (before == head_)
            head_ = PtrTraits::GetRaw(ptr);

        ptr_ns.prev_    = before_ns.prev_;
        before_ns.prev_ = PtrTraits::GetRaw(ptr);
        ptr_ns.next_    = utils::move(after_ns.next_);
        after_ns.next_  = utils::move(ptr);
    }

    PtrType internal_erase(RawPtrType node) {
        if (!node)
            return PtrType(nullptr);

        auto& node_ns = NodeTraits::node_state(*node);
        DEBUG_ASSERT((node_ns.prev_ != nullptr) && (node_ns.next_ != nullptr));

        if (node == node_ns.prev_) {
            DEBUG_ASSERT(node == head_);
            head_         = nullptr;
            node_ns.prev_ = nullptr;
            return PtrTraits::Take(node_ns.next_);
        }

        if (node == head_)
            head_ = PtrTraits::GetRaw(node_ns.next_);

        auto& next_ns = NodeTraits::node_state(*node_ns.next_);
        auto& prev_ns = NodeTraits::node_state(*node_ns.prev_);

        next_ns.prev_ = node_ns.prev_;
        node_ns.prev_ = nullptr;
        PtrTraits::Swap(prev_ns.next_, node_ns.next_);
        return PtrTraits::Take(node_ns.next_);
    }

    RawPtrType tail() {
        if (!head_)
            return nullptr;
        auto& head_ns = NodeTraits::node_state(*head_);
        return head_ns.prev_;
    }

    // State consists of just a raw head pointer.
    RawPtrType head_ = nullptr;
};

}  // namespace utils
