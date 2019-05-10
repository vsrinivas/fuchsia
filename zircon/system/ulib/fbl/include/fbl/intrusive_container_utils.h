// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_INTRUSIVE_CONTAINER_UTILS_H_
#define FBL_INTRUSIVE_CONTAINER_UTILS_H_

#include <fbl/intrusive_pointer_traits.h>
#include <fbl/macros.h>

#include <type_traits>

namespace fbl {

// DefaultKeyedObjectTraits defines a default implementation of traits used to
// manage objects stored in associative containers such as hash-tables and
// trees.
//
// At a minimum, a class or a struct which is to be used to define the
// traits of a keyed object must define the following public members.
//
// GetKey   : A static method which takes a constant reference to an object (the
//            type of which is infered from PtrType) and returns a KeyType
//            instance corresponding to the key for an object.
// LessThan : A static method which takes two keys (key1 and key2) and returns
//            true if-and-only-if key1 is considered to be less than key2 for
//            sorting purposes.
// EqualTo  : A static method which takes two keys (key1 and key2) and returns
//            true if-and-only-if key1 is considered to be equal to key2.
//
// Rules for keys:
// ++ The type of key returned by GetKey must be compatible with the key which
//    was specified for the container.
// ++ The key for an object must remain constant for as long as the object is
//    contained within a container.
// ++ When comparing keys, comparisons must obey basic transative and
//    commutative properties.  That is to say...
//    LessThan(A, B) and LessThan(B, C) implies LessThan(A, C)
//    EqualTo(A, B) and EqualTo(B, C) implies EqualTo(A, C)
//    EqualTo(A, B) if-and-only-if EqualTo(B, A)
//    LessThan(A, B) if-and-only-if EqualTo(B, A) or (not LessThan(B, A))
//
// DefaultKeyedObjectTraits is a helper class which allows an object to be
// treated as a keyed-object by implementing a const GetKey method which returns
// a key of the appropriate type.  The key type must be compatible with the
// container key type, and must have definitions of the < and == operators for
// the purpose of generating implementation of LessThan and EqualTo.
template <typename KeyType, typename ObjType>
struct DefaultKeyedObjectTraits {
    static KeyType GetKey(const ObjType& obj)                       { return obj.GetKey(); }
    static bool LessThan(const KeyType& key1, const KeyType& key2)  { return key1 <  key2; }
    static bool EqualTo (const KeyType& key1, const KeyType& key2)  { return key1 == key2; }
};

struct DefaultObjectTag {};

// ContainableBaseClasses<> makes it easy to define types that live in multiple
// intrusive containers at once.
//
// If you didn't use this helper template, you would have to define multiple
// traits classes, each with their own node_state function, and then have
// multiple NodeState members in your class. This is noisy boilerplate, so
// instead you can just do something like the following:
//
// struct MyTag1 {};
// struct MyTag2 {};
// struct MyTag3 {};
//
// class MyClass
//     : public fbl::RefCounted<MyClass>,
//       public fbl::ContainableBaseClasses<
//           fbl::WAVLTreeContainable<fbl::RefPtr<MyClass>, MyTag1>,
//           fbl::WAVLTreeContainable<fbl::RefPtr<MyClass>, MyTag2>,
//           fbl::SinglyLinkedListable<MyClass*, MyTag3>,
//           [...]> { <your class definition> };
//
// Then when you create your container, you use the same tag type:
//
// fbl::TaggedWAVLTree<uint32_t, fbl::RefPtr<MyClass>, MyTag1> my_tree;
//
// The tag types themselves can be basically anything but I recommend you define
// your own empty structs to keep it simple and make it a type that you own.
//
// (Note for the curious: the tag types are necessary to solve the diamond
// problem, since your class ends up with multiple node_state_ members from the
// non-virtual multiple inheritance and the compiler needs to know which one you
// want.)
//
// When you inherit from this template, your class will also end up with a
// TagTypes member, which is just a std::tuple of your tag types, so that
// you can query these for metaprogramming purposes.
//
// You should also get relatively readable error messages for common error cases
// because of a few static_asserts; notably, you cannot:
// ++ Use any variation of unique_ptr as the PtrType here since that would defeat
//    its purpose.
// ++ Explicitly use the DefaultObjectTag that is used as the tag type when
//    the user does not specify one.
// ++ Pass the same tag type twice.
//
template <typename... BaseClasses>
struct ContainableBaseClasses;

template <>
struct ContainableBaseClasses<> {
    using TagTypes = std::tuple<>;
};

template <template <typename, typename>
             class    Container,
          typename    PtrType,
          typename    TagType,
          typename... Rest>
struct ContainableBaseClasses<Container<PtrType, TagType>, Rest...>
       : public Container<PtrType, TagType>,
         public ContainableBaseClasses<Rest...> {
    static_assert(internal::ContainerPtrTraits<PtrType>::CanCopy,
                  "You can't have a unique_ptr in multiple containers at once.");
    static_assert(!std::is_same_v<TagType, DefaultObjectTag>,
                  "Do not use fbl::DefaultObjectTag when inheriting from "
                  "fbl::ContainableBaseClasses; define your own instead.");
    static_assert((!std::is_same_v<TagType, typename Rest::TagType> && ...),
                  "All tag types used with fbl::ContainableBaseClasses must be unique.");

    using TagTypes =
        decltype(std::tuple_cat(
                     std::declval<std::tuple<TagType>>(),
                     std::declval<typename ContainableBaseClasses<Rest...>::TagTypes>()));
};

}  // namespace fbl

namespace fbl::internal {

// DirectEraseUtils
//
// A utility class used by HashTable to implement an O(n) or O(k) direct erase
// operation depending on whether or not the HashTable's bucket type supports
// O(k) erase.
template <typename ContainerType, typename Enable = void>
struct DirectEraseUtils;

template <typename ContainerType>
struct DirectEraseUtils<
        ContainerType,
        std::enable_if_t<ContainerType::SupportsConstantOrderErase == false, void>> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;
    using ValueType = typename PtrTraits::ValueType;

    static PtrType erase(ContainerType& container, ValueType& obj) {
        return container.erase_if(
            [&obj](const ValueType& other) -> bool {
                return &obj == &other;
            });
    }
};

template <typename ContainerType>
struct DirectEraseUtils<
        ContainerType,
        std::enable_if_t<ContainerType::SupportsConstantOrderErase == true, void>> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;
    using ValueType = typename PtrTraits::ValueType;

    static PtrType erase(ContainerType& container, ValueType& obj) {
        return container.erase(obj);
    }
};

// KeyEraseUtils
//
// A utility class used by HashTable to implement an O(n) or O(k) erase-by-key
// operation depending on whether or not the HashTable's bucket type is
// associative or not.
template <typename ContainerType, typename KeyTraits, typename Enable = void>
struct KeyEraseUtils;

template <typename ContainerType, typename KeyTraits>
struct KeyEraseUtils<
        ContainerType,
        KeyTraits,
        std::enable_if_t<ContainerType::IsAssociative == false, void>> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;
    using ValueType = typename PtrTraits::ValueType;

    template <typename KeyType>
    static PtrType erase(ContainerType& container, const KeyType& key) {
        return container.erase_if(
            [key](const ValueType& other) -> bool {
                return KeyTraits::EqualTo(key, KeyTraits::GetKey(other));
            });
    }
};

template <typename ContainerType, typename KeyTraits>
struct KeyEraseUtils<
        ContainerType,
        KeyTraits,
        std::enable_if_t<ContainerType::IsAssociative == true, void>> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;

    template <typename KeyType>
    static PtrType erase(ContainerType& container, const KeyType& key) {
        return container.erase(key);
    }
};

// Swaps two plain old data types with size no greater than 64 bits.
template <typename T, typename = std::enable_if_t<std::is_pod_v<T> && (sizeof(T) <= 8)>>
inline void Swap(T& a, T& b) noexcept {
    T tmp = a;
    a = b;
    b = tmp;
}

// Notes on container sentinels:
//
// Intrusive container implementations employ a slightly tricky pattern where
// sentinel values are used in place of nullptr in various places in the
// internal data structure in order to make some operations a bit easier.
// Generally speaking, a sentinel pointer is a pointer to a container with the
// sentinel bit set.  It is cast and stored in the container's data structure as
// a pointer to an element which the container contains, even though it is
// actually a slightly damaged pointer to the container itself.
//
// An example of where this is used is in the doubly linked list implementation.
// The final element in the list holds the container's sentinel value instead of
// nullptr or a pointer to the head of the list.  When an iterator hits the end
// of the list, it knows that it is at the end (because the sentinel bit is set)
// but can still get back to the list itself (by clearing the sentinel bit in
// the pointer) without needing to store an explicit pointer to the list itself.
//
// Care must be taken when using sentinel values.  They are *not* valid pointers
// and must never be dereferenced, recovered into an managed representation, or
// returned to a user.  In addition, it is essential that a legitimate pointer
// to a container never need to set the sentinel bit.  Currently, bit 0 is being
// used because it should never be possible to have a proper container instance
// which is odd-aligned.
constexpr uintptr_t kContainerSentinelBit = 1U;

// Create a sentinel pointer from a raw pointer, converting it to the specified
// type in the process.
template <typename T, typename U, typename = std::enable_if_t<std::is_pointer_v<T>>>
constexpr T make_sentinel(U* ptr) {
    return reinterpret_cast<T>(reinterpret_cast<uintptr_t>(ptr) |
                               kContainerSentinelBit);
}

template <typename T, typename = std::enable_if_t<std::is_pointer_v<T>>>
constexpr T make_sentinel(decltype(nullptr)) {
    return reinterpret_cast<T>(kContainerSentinelBit);
}

// Turn a sentinel pointer back into a normal pointer, automatically
// re-interpreting its type in the process.
template <typename T, typename U, typename = std::enable_if_t<std::is_pointer_v<T>>>
constexpr T unmake_sentinel(U* sentinel) {
    return reinterpret_cast<T>(reinterpret_cast<uintptr_t>(sentinel) &
                               ~kContainerSentinelBit);
}

// Test to see if a pointer is a sentinel pointer.
template <typename T>
constexpr bool is_sentinel_ptr(const T* ptr) {
    return (reinterpret_cast<uintptr_t>(ptr) & kContainerSentinelBit) != 0;
}

// Test to see if a pointer (which may be a sentinel) is valid.  Valid in this
// context means that the pointer is not null, and is not a sentinel.
template <typename T>
constexpr bool valid_sentinel_ptr(const T* ptr) {
    return ptr && !is_sentinel_ptr(ptr);
}

DECLARE_HAS_MEMBER_FN(has_node_state, node_state);
DECLARE_HAS_MEMBER_TYPE(has_tag_types, TagTypes);

}  // namespace fbl::internal

#endif  // FBL_INTRUSIVE_CONTAINER_UTILS_H_
