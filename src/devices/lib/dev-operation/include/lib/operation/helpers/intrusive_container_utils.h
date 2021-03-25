// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_HELPERS_INTRUSIVE_CONTAINER_UTILS_H_
#define SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_HELPERS_INTRUSIVE_CONTAINER_UTILS_H_

#include <lib/operation/helpers/intrusive_pointer_traits.h>
#include <lib/operation/helpers/macros.h>

#include <type_traits>
#include <utility>

namespace operation {

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
  static KeyType GetKey(const ObjType& obj) { return obj.GetKey(); }
  static bool LessThan(const KeyType& key1, const KeyType& key2) { return key1 < key2; }
  static bool EqualTo(const KeyType& key1, const KeyType& key2) { return key1 == key2; }
};

// A set of flag-style options which can be applied to container nodes in order
// to control and sanity check their behavior and compatibility at compile time.
//
// To control node options, users pass a set of options to either the
// containable mix-in class (SinglyLinkedLisable, DoublyLinkedListable, or
// WAVLTreeContainable), or directly to the node instance in the case that the
// user is specifying custom container traits.
enum class NodeOptions : uint64_t {
  None = 0,

  // By default, nodes will not allow either copy construction or copy
  // assignment. Directly copying the contents of node storage for a structure
  // which is currently in a container cannot be allowed.  The result would be
  // to have two objects, one of which is actually in a container, and the other
  // of which is only sort-of in the container.
  //
  // Because of this, if code attempts to expand either the copy constructor or
  // assignment operator, by default it will trigger a static assert.
  //
  // It may be the case, however, that a user wishes to allow copying of their
  // structure while the source and destination structure are _not_ in any
  // containers.  In this case, pass the NodeOptions::AllowCopy flag.  Nodes
  // will permit copying, but will runtime ZX_DEBUG_ASSERT if either the source
  // or destination exist within a container at the time of the copy.  In builds
  // with ZX_DEBUG_ASSERTs disabled, neither the source nor the destination's
  // internal node contents will be modified.  So, in the case of the following
  // code:
  //
  //   MyStructure& A = *(container.begin());
  //   MyStructure  B = A;
  //   MyStructure& C = get_a_structure_reference_from_somewhere();
  //   C = A;
  //
  // ++ A will remain in the container unmodified.
  // ++ B will be in no container (as it was just constructed).
  // ++ C either will either remain in its container if it had been in one, or
  //      will remain in no container if not.
  //
  // Finally, it is possible that a user actually _does_ wish to copy the
  // contents of a structure out of a container using either copy construction
  // or using copy assignment.  If this is the case, pass the
  // NodeOptions::AllowCopyFromContainer flag.  In addition to allowing the copy
  // constructor and assignment operators, the debug asserts will be disabled in
  // this situation.  Users may copy the contents of their node, but not mutate
  // any container membership state as part of the process.
  //
  AllowCopy = (1 << 0),
  AllowCopyFromContainer = (1 << 1),

  // See the AllowCopy and AllowCopyFromConainer flags for details.  AllowMove
  // and AllowMoveFromContainer are the same, simply applied to the rvalue
  // constructor and assignment operators of the node.
  AllowMove = (1 << 2),
  AllowMoveFromContainer = (1 << 3),

  // Convenience definitions
  AllowCopyMove = static_cast<uint64_t>(AllowCopy) | static_cast<uint64_t>(AllowMove),
  AllowCopyMoveFromContainer =
      static_cast<uint64_t>(AllowCopyFromContainer) | static_cast<uint64_t>(AllowMoveFromContainer),

  // Allow an object to exist in multiple containers at once, even if one or
  // more of those containers tracks the object using a unique_ptr (or any other
  // non-copyable pointer type).
  //
  // Generally, it would be a mistake to define an object which can exist in
  // multiple containers concurrently, and track those objects in their
  // containers using unique_ptr semantics.  In theory, it should be impossible
  // for two different containers to track the same object at the same time,
  // each using something like a unique_ptr.  This would violate the uniqueness of
  // the pointer.
  //
  // Because of this, the ContainableBaseClasses helper (see below) will, by
  // default, complain and refuse to build if someone attempts to use it in
  // conjunction with a Containable mix-in which tracks objects using
  // unique_ptr-style pointers if there are any other containers in the
  // Containable list.
  //
  // There are special cases, however, where a user might want this behavior to
  // be permitted.  Consider an object whose lifecycle is managed by a central
  // list of unique_ptrs, but which can also exist on a temporary list which
  // tracks the objects using raw pointers for strictly algorithmic purposes.
  // Provided that the user carefully ensures that the object does not disappear
  // from the central list while it exists on the temporary list, this should be
  // completely fine.  More concretely, the following should be allowed provided
  // that the user opts in.
  //
  // using MainList = operation::TaggedDoublyLinkedList<std::unique_ptr<Obj>, MainListTag>;
  // using TmpList = operation::TaggedSinglyLinkedList<Obj*, TmpListTag>;
  //
  // operation::Mutex all_objects_lock;
  // MainList all_objects TA_GUARDED(all_objects_lock);
  //
  // void do_interesting_things() TA_EXCL(all_objects_lock) {
  //   operation::AutoLock lock(&all_objects_lock);
  //   TmpList interesting_objects;
  //
  //   for (auto& obj : all_objects) {
  //     if (object_is_interesting(obj)) {
  //       interesting_objects.push(obj);
  //     }
  //   }
  //
  //   do_interesting_things_to_interesting_objects(std::move(interesting_objects));
  // }
  //
  // Users who have carefully considered the lifecycle management of their
  // objects and wish to allow this behavior should pass the
  // AllowMultiContainerUptr option to their Containable mix-in.
  AllowMultiContainerUptr = (1 << 4),

  // Nodes with this flag permitted to be directly removed from their container,
  // without needing to go through the container's erase method.
  AllowRemoveFromContainer = (1 << 5),

  // Enables the |clear_unsafe| operation on containers of unmanaged pointers.
  AllowClearUnsafe = (1 << 6),

  // Reserved bits reserved for testing purposes and should always be ignored by
  // node implementations.
  ReservedBits = 0xF000000000000000,
};

// Helper functions which make it a bit easier to use the enum class NodeOptions
// in a flag style fashion.
//
// The | operator will take two options and or them together to produce their
// composition without needing to do all sorts of nasty casting.  In other
// words:
//
//   operation::NodeOptions::AllowX | operation::NodeOptions::AllowY
//
// is legal.
//
// The & operator is overloaded to perform the bitwise and of the
// underlying flags and test against zero returning a bool.  This allows us to
// say things like:
//
//   if constexpr (SomeOptions | operation::NodeOptions::AllowX) { ... }
//
constexpr operation::NodeOptions operator|(operation::NodeOptions A, operation::NodeOptions B) {
  return static_cast<operation::NodeOptions>(
      static_cast<std::underlying_type<operation::NodeOptions>::type>(A) |
      static_cast<std::underlying_type<operation::NodeOptions>::type>(B));
}

constexpr bool operator&(operation::NodeOptions A, operation::NodeOptions B) {
  return (static_cast<std::underlying_type<operation::NodeOptions>::type>(A) &
          static_cast<std::underlying_type<operation::NodeOptions>::type>(B)) != 0;
}

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
//     : public operation::RefCounted<MyClass>,
//       public operation::ContainableBaseClasses<
//           operation::WAVLTreeContainable<operation::RefPtr<MyClass>, MyTag1>,
//           operation::WAVLTreeContainable<operation::RefPtr<MyClass>, MyTag2>,
//           operation::SinglyLinkedListable<MyClass*, MyTag3>,
//           [...]> { <your class definition> };
//
// Then when you create your container, you use the same tag type:
//
// operation::TaggedWAVLTree<uint32_t, operation::RefPtr<MyClass>, MyTag1> my_tree;
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
namespace internal {

template <typename... BaseClasses>
struct ContainableBaseClassEnumerator;

template <>
struct ContainableBaseClassEnumerator<> {
  using ContainableTypes = std::tuple<>;
  using TagTypes = std::tuple<>;
  static constexpr size_t BaseClassCount = 0;
  static constexpr size_t UniquePtrCount = 0;
};

template <template <typename, NodeOptions, typename> class Containable, typename PtrType,
          NodeOptions Options, typename TagType, typename... Rest>
struct ContainableBaseClassEnumerator<Containable<PtrType, Options, TagType>, Rest...>
    : public Containable<PtrType, Options, TagType>,
      public ContainableBaseClassEnumerator<Rest...> {
  static_assert(!std::is_same_v<TagType, DefaultObjectTag>,
                "Do not use operation::DefaultObjectTag when inheriting from "
                "operation::ContainableBaseClasses. Define your own instead.");
  static_assert(
      (!std::is_same_v<TagType, typename Rest::TagType> && ...),
      "All tag types used with operation::ContainableBaseClassEnumerator must be unique.");

  using ContainableTypes = decltype(std::tuple_cat(
      std::declval<std::tuple<Containable<PtrType, Options, TagType>>>(),
      std::declval<typename ContainableBaseClassEnumerator<Rest...>::ContainableTypes>()));
  using TagTypes = decltype(std::tuple_cat(
      std::declval<std::tuple<TagType>>(),
      std::declval<typename ContainableBaseClassEnumerator<Rest...>::TagTypes>()));

  static constexpr size_t BaseClassCount =
      1 + ContainableBaseClassEnumerator<Rest...>::BaseClassCount;
  static constexpr size_t UniquePtrCount = !internal::ContainerPtrTraits<PtrType>::CanCopy +
                                           ContainableBaseClassEnumerator<Rest...>::UniquePtrCount;
  static_assert((UniquePtrCount == 0) || ((UniquePtrCount == 1) && (BaseClassCount == 1)) ||
                    (Options & NodeOptions::AllowMultiContainerUptr),
                "Containers of pointers with unique pointer semantics cannot be combined with any "
                "other containers when using ContainableBaseClasses unless you specify the "
                "AllowMultiContainerUptr flag in your node options.");
};

}  // namespace internal

template <typename... BaseClasses>
struct ContainableBaseClasses {
  using Enumerator = internal::ContainableBaseClassEnumerator<BaseClasses...>;
  using ContainableTypes = typename Enumerator::ContainableTypes;
  using TagTypes = typename Enumerator::TagTypes;

  template <typename Tag, size_t N = 0>
  static constexpr size_t TagIndex() {
    static_assert(N < std::tuple_size<ContainableTypes>(), "Tag not found!");
    using ContainableType = typename std::tuple_element<N, ContainableTypes>::type;
    if constexpr (std::is_same_v<Tag, typename ContainableType::TagType>) {
      return N;
    } else {
      return TagIndex<Tag, N + 1>();
    }
  }

  template <typename Tag>
  auto& GetContainableByTag() {
    constexpr size_t Index = TagIndex<Tag>();
    return std::get<Index>(contained_nodes_);
  }

  template <typename Tag>
  const auto& GetContainableByTag() const {
    constexpr size_t Index = TagIndex<Tag>();
    return std::get<Index>(contained_nodes_);
  }

  ContainableTypes contained_nodes_;
};

namespace internal {
DECLARE_HAS_MEMBER_TYPE(has_tag_types, TagTypes);
}

// These are free function because making it a member function presents
// complicated lookup issues since the specific Containable classes exist as
// members of the ContainableBaseClasses<...>, and you'd need to say
// obj.template GetContainableByTag<TagType>().InContainer (or
// RemoveFromContainer), which is super ugly.
template <typename TagType = DefaultObjectTag, typename Containable>
bool InContainer(const Containable& c) {
  if constexpr (std::is_same_v<TagType, DefaultObjectTag>) {
    return c.InContainer();
  } else {
    return c.template GetContainableByTag<TagType>().InContainer();
  }
}

template <typename TagType = DefaultObjectTag, typename Containable>
auto RemoveFromContainer(Containable& c) {
  if constexpr (std::is_same_v<TagType, DefaultObjectTag>) {
    return c.RemoveFromContainer();
  } else {
    return c.template GetContainableByTag<TagType>().RemoveFromContainer();
  }
}

// An enumeration which can be used as a template argument on list types to
// control the order of operation needed to compute the size of the list.  When
// set to SizeOrder::N, the list's size will not be maintained and there will be
// no valid size() method to call.  The only way to fetch the size of a list
// would be via |size_slow()|.  Alternatively, a user may specify
// SizeOrder::Constant.  In this case, the storage size of the list itself will
// grow by a size_t, and the size of the list will be maintained as elements are
// added and removed.
enum class SizeOrder { N, Constant };

}  // namespace operation

namespace operation::internal {

// DirectEraseUtils
//
// A utility class used by HashTable to implement an O(n) or O(k) direct erase
// operation depending on whether or not the HashTable's bucket type supports
// O(k) erase.
template <typename ContainerType, typename Enable = void>
struct DirectEraseUtils;

template <typename ContainerType>
struct DirectEraseUtils<
    ContainerType, std::enable_if_t<ContainerType::SupportsConstantOrderErase == false, void>> {
  using PtrTraits = typename ContainerType::PtrTraits;
  using PtrType = typename PtrTraits::PtrType;
  using ValueType = typename PtrTraits::ValueType;

  static PtrType erase(ContainerType& container, ValueType& obj) {
    return container.erase_if([&obj](const ValueType& other) -> bool { return &obj == &other; });
  }
};

template <typename ContainerType>
struct DirectEraseUtils<ContainerType,
                        std::enable_if_t<ContainerType::SupportsConstantOrderErase == true, void>> {
  using PtrTraits = typename ContainerType::PtrTraits;
  using PtrType = typename PtrTraits::PtrType;
  using ValueType = typename PtrTraits::ValueType;

  static PtrType erase(ContainerType& container, ValueType& obj) { return container.erase(obj); }
};

// KeyEraseUtils
//
// A utility class used by HashTable to implement an O(n) or O(k) erase-by-key
// operation depending on whether or not the HashTable's bucket type is
// associative or not.
template <typename ContainerType, typename KeyTraits, typename Enable = void>
struct KeyEraseUtils;

template <typename ContainerType, typename KeyTraits>
struct KeyEraseUtils<ContainerType, KeyTraits,
                     std::enable_if_t<ContainerType::IsAssociative == false, void>> {
  using PtrTraits = typename ContainerType::PtrTraits;
  using PtrType = typename PtrTraits::PtrType;
  using ValueType = typename PtrTraits::ValueType;

  template <typename KeyType>
  static PtrType erase(ContainerType& container, const KeyType& key) {
    return container.erase_if([key](const ValueType& other) -> bool {
      return KeyTraits::EqualTo(key, KeyTraits::GetKey(other));
    });
  }
};

template <typename ContainerType, typename KeyTraits>
struct KeyEraseUtils<ContainerType, KeyTraits,
                     std::enable_if_t<ContainerType::IsAssociative == true, void>> {
  using PtrTraits = typename ContainerType::PtrTraits;
  using PtrType = typename PtrTraits::PtrType;

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
  return reinterpret_cast<T>(reinterpret_cast<uintptr_t>(ptr) | kContainerSentinelBit);
}

template <typename T, typename = std::enable_if_t<std::is_pointer_v<T>>>
constexpr T make_sentinel(decltype(nullptr)) {
  return reinterpret_cast<T>(kContainerSentinelBit);
}

// Turn a sentinel pointer back into a normal pointer, automatically
// re-interpreting its type in the process.
template <typename T, typename U, typename = std::enable_if_t<std::is_pointer_v<T>>>
constexpr T unmake_sentinel(U* sentinel) {
  return reinterpret_cast<T>(reinterpret_cast<uintptr_t>(sentinel) & ~kContainerSentinelBit);
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

// Helpers which can be used to determine the NodeState type and
// NodeState::PtrType types returned by the node_state method of a TraitClass
// |RefType|.  These are used primarily in tests and in static_asserts in the
// code as sanity checks.
template <typename TraitClass, typename RefType>
using node_state_t = std::decay_t<std::invoke_result_t<decltype(TraitClass::node_state), RefType>>;

template <typename TraitClass, typename RefType>
using node_ptr_t = typename node_state_t<TraitClass, RefType>::PtrType;

// SizeTracker is a partially specialized internal class used to track (or
// explicitly to not track) the size of Lists in the operation:: containers.  Its
// behavior and size depends on the SizeOrder template parameter passed to it.
//
// Please note that to use this class, containers must (sadly) derive from it, they
// cannot simply encapsulate it.  The SizeOrder::N version of the tracker is
// nominally of 0 size, however 0 sized members of a struct/class are not allowed
// in C++.  Attempting to put a 0 sized member into a class results in at least
// 1 byte of size impact, which changes the size of the entire object.
//
// 0 sized base classes, however, are totally fine.  So, if we encapsulate a
// SizeTracker<SizeOrder::N>, then our container gets bigger for no reason, but
// if we derive from one, then our container stays the size that we expect it
// to.
//
// static_assert tests for this exist in the non-sized doubly and singly linked
// list tests.
template <SizeOrder>
class SizeTracker;

template <>
class SizeTracker<SizeOrder::N> {
 protected:
  constexpr SizeTracker() = default;
  ~SizeTracker() = default;

  // No copy, no move.
  SizeTracker(const SizeTracker&) = delete;
  SizeTracker& operator=(const SizeTracker&) = delete;
  SizeTracker(SizeTracker&& other) = delete;
  SizeTracker& operator=(SizeTracker&& other) = delete;

  // Inc, Dec, Reset, and swap operations are no-ops.  There is no count
  // accessor.  Anyone who attempts to access count has made a mistake.
  void IncSizeTracker(size_t) {}
  void DecSizeTracker(size_t) {}
  void ResetSizeTracker() {}
  void SwapSizeTracker(SizeTracker&) {}
};

template <>
class SizeTracker<SizeOrder::Constant> {
 protected:
  constexpr SizeTracker() = default;
  ~SizeTracker() = default;

  // No copy, no move.
  SizeTracker(const SizeTracker&) = delete;
  SizeTracker& operator=(const SizeTracker&) = delete;
  SizeTracker(SizeTracker&& other) = delete;
  SizeTracker& operator=(SizeTracker&& other) = delete;

  // Basic operations for manipulating the count storage.
  void IncSizeTracker(size_t amt) { size_tracker_count_ += amt; }
  void DecSizeTracker(size_t amt) { size_tracker_count_ -= amt; }
  void ResetSizeTracker() { size_tracker_count_ = 0; }
  void SwapSizeTracker(SizeTracker& other) {
    std::swap(size_tracker_count_, other.size_tracker_count_);
  }
  size_t SizeTrackerCount() const { return size_tracker_count_; }

 private:
  size_t size_tracker_count_ = 0;
};

}  // namespace operation::internal

#endif  // SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_HELPERS_INTRUSIVE_CONTAINER_UTILS_H_
