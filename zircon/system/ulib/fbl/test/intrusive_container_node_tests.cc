// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <type_traits>

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <zxtest/zxtest.h>

namespace {

// Utilities we use for the various tests below.

// Define a number of special values for NodeOptions we can use to make sure
// that nodes fetched have the proper type.  The macro will produce a constexpr
// symbol named "NodeOptTagN" where N is the number given. It will also make
// sure that the values produced use only the reserved-for-tests bits in the
// NodeOptions, and that they don't accidentally shift the bits off of the end
// of the word to produce a 0 value.  In addition, it will produce an empty
// structure named "TagTypeN" which can be used for Node tagging.  We know that
// all of our tags must be unique, because otherwise the TagTypeN structure
// definitions would collide.

constexpr bool ValidTestNodeOption(fbl::NodeOptions opt) {
  using UT = std::underlying_type_t<fbl::NodeOptions>;
  return (((static_cast<UT>(opt) & ~(static_cast<UT>(fbl::NodeOptions::ReservedBits))) == 0) &&
          (static_cast<UT>(opt) != 0));
}

#define DEFINE_NODE_TAGS(N)                                                              \
  struct TagType##N {};                                                                  \
  constexpr fbl::NodeOptions NodeOptTag##N = static_cast<fbl::NodeOptions>(N##ul << 60); \
  static_assert(ValidTestNodeOption(NodeOptTag##N), "Tag1 is declared to use non-test bits!")

DEFINE_NODE_TAGS(1);
DEFINE_NODE_TAGS(2);
DEFINE_NODE_TAGS(3);
DEFINE_NODE_TAGS(4);
DEFINE_NODE_TAGS(5);
DEFINE_NODE_TAGS(6);
DEFINE_NODE_TAGS(7);
DEFINE_NODE_TAGS(8);
DEFINE_NODE_TAGS(9);

// Define some helpers which look up a node based on tag and object type using
// the default traits.  These are mostly about reducing the amount of terrible
// metaprogramming typing we need to do.
template <typename TypeTag = fbl::DefaultObjectTag, typename T>
const auto& FindSLLNode(T& obj) {
  return fbl::DefaultSinglyLinkedListTraits<T*>::template node_state<TypeTag>(obj);
}

template <typename TypeTag = fbl::DefaultObjectTag, typename T>
const auto& FindDLLNode(T& obj) {
  return fbl::DefaultDoublyLinkedListTraits<T*>::template node_state<TypeTag>(obj);
}

template <typename TypeTag = fbl::DefaultObjectTag, typename T>
const auto& FindWAVLNode(T& obj) {
  return fbl::DefaultWAVLTreeTraits<T*>::template node_state<TypeTag>(obj);
}

// Define a macro which will give us the base return type of an expression,
// stripping the pointers, references, and CV qualifiers.
#define TYPE(expr) std::decay_t<decltype(expr)>

// Define a simple helper class we can use to check to see if various objects
// intersect each other in memory, or are completely contained by each other in
// memory.  This lets us make sure that node storage is always contained within
// an object, but different nodes in storage in the object never overlap.
struct Range {
  template <typename T>
  static Range Of(const T& obj) {
    return Range{reinterpret_cast<uintptr_t>(&obj), sizeof(T)};
  }

  Range(uintptr_t start_, size_t len_) : start(start_), len(len_) {}

  bool IntersectsWith(const Range& other) const {
    // We do not intersect the other object if our end is completely before the
    // other's start, or if our start is completely after the other's end.
    return !(((start + len) <= other.start) || (start >= (other.start + other.len)));
  }

  bool ContainedBy(const Range& other) const {
    // We are completely contained by other if our start is equal to or after
    // their start, and our end is equal to or before their end.
    return (start >= other.start) && ((start + len) <= (other.start + other.len));
  }

  const uintptr_t start;
  const size_t len;
};

// A helper which tests to see if a set of ranges are all non-overlapping.
template <size_t N>
bool RangesAreNonOverlapping(const std::array<Range, N>& ranges) {
  for (size_t i = 0; (i + 1) < ranges.size(); ++i) {
    for (size_t j = i + 1; j < ranges.size(); ++j) {
      if (ranges[i].IntersectsWith(ranges[j])) {
        return false;
      }
    }
  }
  return true;
}

TEST(IntrusiveContainerNodeTest, EmbeddedSingleNode) {
  // Check to make sure that we can embed a single container node directly into
  // a struct and have the default traits classes find it.
  struct SLL {
    uint32_t a, b, c;
    fbl::SinglyLinkedListNodeState<SLL*, NodeOptTag1> sll_node_state_;
    uint32_t d, e, f;
  } test_sll_obj;

  // Selecting our default node should give us a type with the proper option
  // tag, and should be completely contained somewhere within the test object.
  static_assert(TYPE(FindSLLNode(test_sll_obj))::kNodeOptions == NodeOptTag1,
                "Default traits found the wrong node!");
  ASSERT_TRUE(Range::Of(FindSLLNode(test_sll_obj)).ContainedBy(Range::Of(test_sll_obj)));

  struct DLL {
    uint32_t a, b, c;
    fbl::SinglyLinkedListNodeState<DLL*, NodeOptTag2> dll_node_state_;
    uint32_t d, e, f;
  } test_dll_obj;

  // Same checks, this time for doubly linked list nodes.
  static_assert(TYPE(FindDLLNode(test_dll_obj))::kNodeOptions == NodeOptTag2,
                "Default traits found the wrong node!");
  ASSERT_TRUE(Range::Of(FindDLLNode(test_dll_obj)).ContainedBy(Range::Of(test_dll_obj)));

  struct WAVL {
    uint32_t a, b, c;
    fbl::SinglyLinkedListNodeState<WAVL*, NodeOptTag3> wavl_node_state_;
    uint32_t d, e, f;
  } test_wavl_obj;

  // Same checks, this time for WAVL trees nodes.
  static_assert(TYPE(FindWAVLNode(test_wavl_obj))::kNodeOptions == NodeOptTag3,
                "Default traits found the wrong node!");
  ASSERT_TRUE(Range::Of(FindWAVLNode(test_wavl_obj)).ContainedBy(Range::Of(test_wavl_obj)));
}

TEST(IntrusiveContainerNodeTest, DefaultSingleNode) {
  // Check to make sure that we can find a node in our object using the default mix-ins.
  struct SLL : public fbl::SinglyLinkedListable<SLL*, fbl::DefaultObjectTag, NodeOptTag1> {
    uint32_t a, b, c;
  } test_sll_obj;

  // Selecting our default node should give us a type with the proper option
  // tag, and should be completely contained somewhere within the test object.
  static_assert(TYPE(FindSLLNode(test_sll_obj))::kNodeOptions == NodeOptTag1,
                "Default traits found the wrong node!");
  ASSERT_TRUE(Range::Of(FindSLLNode(test_sll_obj)).ContainedBy(Range::Of(test_sll_obj)));

  struct DLL : public fbl::DoublyLinkedListable<DLL*, fbl::DefaultObjectTag, NodeOptTag2> {
    uint32_t a, b, c;
  } test_dll_obj;

  // Same checks, this time for doubly linked list nodes.
  static_assert(TYPE(FindDLLNode(test_dll_obj))::kNodeOptions == NodeOptTag2,
                "Default traits found the wrong node!");
  ASSERT_TRUE(Range::Of(FindDLLNode(test_dll_obj)).ContainedBy(Range::Of(test_dll_obj)));

  struct WAVL : public fbl::WAVLTreeContainable<WAVL*, fbl::DefaultObjectTag, NodeOptTag3> {
    uint32_t a, b, c;
  } test_wavl_obj;

  // Same checks, this time for WAVL trees nodes.
  static_assert(TYPE(FindWAVLNode(test_wavl_obj))::kNodeOptions == NodeOptTag3,
                "Default traits found the wrong node!");
  ASSERT_TRUE(Range::Of(FindWAVLNode(test_wavl_obj)).ContainedBy(Range::Of(test_wavl_obj)));
}

TEST(IntrusiveContainerNodeTest, MultipleSLLTaggedNodes) {
  // Check to make sure that we can locate each of our different node types in structures which use
  // the ContainableBaseClasses helper.
  struct SLL
      : public fbl::ContainableBaseClasses<fbl::SinglyLinkedListable<SLL*, TagType1, NodeOptTag1>,
                                           fbl::SinglyLinkedListable<SLL*, TagType2, NodeOptTag2>,
                                           fbl::SinglyLinkedListable<SLL*, TagType3, NodeOptTag3>> {
    uint32_t a, b, c;
  } test_sll_obj;

  // Make sure the types have the options we expect when selected via tag
  static_assert(TYPE(FindSLLNode<TagType1>(test_sll_obj))::kNodeOptions == NodeOptTag1,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindSLLNode<TagType2>(test_sll_obj))::kNodeOptions == NodeOptTag2,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindSLLNode<TagType3>(test_sll_obj))::kNodeOptions == NodeOptTag3,
                "Default traits found the wrong node!");

  // Make sure that all of the nodes are completely contained with the object.
  ASSERT_TRUE(Range::Of(FindSLLNode<TagType1>(test_sll_obj)).ContainedBy(Range::Of(test_sll_obj)));
  ASSERT_TRUE(Range::Of(FindSLLNode<TagType2>(test_sll_obj)).ContainedBy(Range::Of(test_sll_obj)));
  ASSERT_TRUE(Range::Of(FindSLLNode<TagType3>(test_sll_obj)).ContainedBy(Range::Of(test_sll_obj)));

  // Make sure that none of the nodes overlap each other.
  ASSERT_TRUE(RangesAreNonOverlapping(std::array{
      Range::Of(FindSLLNode<TagType1>(test_sll_obj)),
      Range::Of(FindSLLNode<TagType2>(test_sll_obj)),
      Range::Of(FindSLLNode<TagType3>(test_sll_obj)),
  }));
}

TEST(IntrusiveContainerNodeTest, MultipleDLLTaggedNodes) {
  // Check to make sure that we can locate each of our different node types in structures which use
  // the ContainableBaseClasses helper.
  struct DLL
      : public fbl::ContainableBaseClasses<fbl::DoublyLinkedListable<DLL*, TagType1, NodeOptTag1>,
                                           fbl::DoublyLinkedListable<DLL*, TagType2, NodeOptTag2>,
                                           fbl::DoublyLinkedListable<DLL*, TagType3, NodeOptTag3>> {
    uint32_t a, b, c;
  } test_dll_obj;

  // Make sure the types have the options we expect when selected via tag
  static_assert(TYPE(FindDLLNode<TagType1>(test_dll_obj))::kNodeOptions == NodeOptTag1,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindDLLNode<TagType2>(test_dll_obj))::kNodeOptions == NodeOptTag2,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindDLLNode<TagType3>(test_dll_obj))::kNodeOptions == NodeOptTag3,
                "Default traits found the wrong node!");

  // Make sure that all of the nodes are completely contained with the object.
  ASSERT_TRUE(Range::Of(FindDLLNode<TagType1>(test_dll_obj)).ContainedBy(Range::Of(test_dll_obj)));
  ASSERT_TRUE(Range::Of(FindDLLNode<TagType2>(test_dll_obj)).ContainedBy(Range::Of(test_dll_obj)));
  ASSERT_TRUE(Range::Of(FindDLLNode<TagType3>(test_dll_obj)).ContainedBy(Range::Of(test_dll_obj)));

  // Make sure that none of the nodes overlap each other.
  ASSERT_TRUE(RangesAreNonOverlapping(std::array{
      Range::Of(FindDLLNode<TagType1>(test_dll_obj)),
      Range::Of(FindDLLNode<TagType2>(test_dll_obj)),
      Range::Of(FindDLLNode<TagType3>(test_dll_obj)),
  }));
}

TEST(IntrusiveContainerNodeTest, MultipleWAVLTaggedNodes) {
  // Check to make sure that we can locate each of our different node types in structures which use
  // the ContainableBaseClasses helper.
  struct WAVL
      : public fbl::ContainableBaseClasses<fbl::WAVLTreeContainable<WAVL*, TagType1, NodeOptTag1>,
                                           fbl::WAVLTreeContainable<WAVL*, TagType2, NodeOptTag2>,
                                           fbl::WAVLTreeContainable<WAVL*, TagType3, NodeOptTag3>> {
    uint32_t a, b, c;
  } test_wavl_obj;

  // Make sure the types have the options we expect when selected via tag
  static_assert(TYPE(FindWAVLNode<TagType1>(test_wavl_obj))::kNodeOptions == NodeOptTag1,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindWAVLNode<TagType2>(test_wavl_obj))::kNodeOptions == NodeOptTag2,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindWAVLNode<TagType3>(test_wavl_obj))::kNodeOptions == NodeOptTag3,
                "Default traits found the wrong node!");

  // Make sure that all of the nodes are completely contained with the object.
  Range obj_range = Range::Of(test_wavl_obj);
  ASSERT_TRUE(Range::Of(FindWAVLNode<TagType1>(test_wavl_obj)).ContainedBy(obj_range));
  ASSERT_TRUE(Range::Of(FindWAVLNode<TagType2>(test_wavl_obj)).ContainedBy(obj_range));
  ASSERT_TRUE(Range::Of(FindWAVLNode<TagType3>(test_wavl_obj)).ContainedBy(obj_range));

  // Make sure that none of the nodes overlap each other.
  ASSERT_TRUE(RangesAreNonOverlapping(std::array{
      Range::Of(FindWAVLNode<TagType1>(test_wavl_obj)),
      Range::Of(FindWAVLNode<TagType2>(test_wavl_obj)),
      Range::Of(FindWAVLNode<TagType3>(test_wavl_obj)),
  }));
}

TEST(IntrusiveContainerNodeTest, MultipleDifferentTaggedNodes) {
  // Check to make sure that we can locate each of our different node types in structures which use
  // the ContainableBaseClasses helper.
  struct Obj
      : public fbl::ContainableBaseClasses<fbl::SinglyLinkedListable<Obj*, TagType1, NodeOptTag1>,
                                           fbl::DoublyLinkedListable<Obj*, TagType2, NodeOptTag2>,
                                           fbl::WAVLTreeContainable<Obj*, TagType3, NodeOptTag3>> {
    uint32_t a, b, c;
  } test_obj;

  // Make sure the types have the options we expect when selected via tag
  static_assert(TYPE(FindSLLNode<TagType1>(test_obj))::kNodeOptions == NodeOptTag1,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindDLLNode<TagType2>(test_obj))::kNodeOptions == NodeOptTag2,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindWAVLNode<TagType3>(test_obj))::kNodeOptions == NodeOptTag3,
                "Default traits found the wrong node!");

  // Make sure that all of the nodes are completely contained with the object.
  Range obj_range = Range::Of(test_obj);
  ASSERT_TRUE(Range::Of(FindSLLNode<TagType1>(test_obj)).ContainedBy(obj_range));
  ASSERT_TRUE(Range::Of(FindDLLNode<TagType2>(test_obj)).ContainedBy(obj_range));
  ASSERT_TRUE(Range::Of(FindWAVLNode<TagType3>(test_obj)).ContainedBy(obj_range));

  // Make sure that none of the nodes overlap each other.
  ASSERT_TRUE(RangesAreNonOverlapping(std::array{
      Range::Of(FindSLLNode<TagType1>(test_obj)),
      Range::Of(FindDLLNode<TagType2>(test_obj)),
      Range::Of(FindWAVLNode<TagType3>(test_obj)),
  }));

  // Mismatching the type and the tag should not work.  Any of these statements should fail to
  // compile.
#if TEST_WILL_NOT_COMPILE || 0
  { auto& [[maybe_unused]] node = FindSLLNode<TagType2>(test_obj); }
  { auto& [[maybe_unused]] node = FindSLLNode<TagType3>(test_obj); };
  { auto& [[maybe_unused]] node = FindDLLNode<TagType1>(test_obj); };
  { auto& [[maybe_unused]] node = FindDLLNode<TagType3>(test_obj); };
  { auto& [[maybe_unused]] node = FindWAVLNode<TagType1>(test_obj); };
  { auto& [[maybe_unused]] node = FindWAVLNode<TagType2>(test_obj); };
#endif
}

TEST(IntrusiveContainerNodeTest, MultipleDifferentDefaultNodes) {
  // Nodes are still permitted to have multiple default Containable mix-ins, as
  // long as the mix-ins are for different types of containers.
  struct Obj : public fbl::SinglyLinkedListable<Obj*, fbl::DefaultObjectTag, NodeOptTag1>,
               public fbl::DoublyLinkedListable<Obj*, fbl::DefaultObjectTag, NodeOptTag2>,
               public fbl::WAVLTreeContainable<Obj*, fbl::DefaultObjectTag, NodeOptTag3> {
    uint32_t a, b, c;
  } test_obj;

  // Make sure the types have the options we expect when selected via tag
  static_assert(TYPE(FindSLLNode(test_obj))::kNodeOptions == NodeOptTag1,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindDLLNode(test_obj))::kNodeOptions == NodeOptTag2,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindWAVLNode(test_obj))::kNodeOptions == NodeOptTag3,
                "Default traits found the wrong node!");

  // Make sure that all of the nodes are completely contained with the object.
  Range obj_range = Range::Of(test_obj);
  ASSERT_TRUE(Range::Of(FindSLLNode(test_obj)).ContainedBy(obj_range));
  ASSERT_TRUE(Range::Of(FindDLLNode(test_obj)).ContainedBy(obj_range));
  ASSERT_TRUE(Range::Of(FindWAVLNode(test_obj)).ContainedBy(obj_range));

  // Make sure that none of the nodes overlap each other.
  ASSERT_TRUE(RangesAreNonOverlapping(std::array{
      Range::Of(FindSLLNode(test_obj)),
      Range::Of(FindDLLNode(test_obj)),
      Range::Of(FindWAVLNode(test_obj)),
  }));
}

TEST(IntrusiveContainerNodeTest, ComplicatedContainables) {
  // OK, now let's make a really complicated example.  A structure which uses
  // all three of the default base mix-ins, as well as multiple instances of
  // each of the tagged node types in a ContainedBaseClasses expression.
  struct Obj
      : public fbl::SinglyLinkedListable<Obj*, fbl::DefaultObjectTag, NodeOptTag1>,
        public fbl::DoublyLinkedListable<Obj*, fbl::DefaultObjectTag, NodeOptTag2>,
        public fbl::WAVLTreeContainable<Obj*, fbl::DefaultObjectTag, NodeOptTag3>,
        public fbl::ContainableBaseClasses<fbl::SinglyLinkedListable<Obj*, TagType4, NodeOptTag4>,
                                           fbl::DoublyLinkedListable<Obj*, TagType5, NodeOptTag5>,
                                           fbl::WAVLTreeContainable<Obj*, TagType6, NodeOptTag6>,
                                           fbl::SinglyLinkedListable<Obj*, TagType7, NodeOptTag7>,
                                           fbl::DoublyLinkedListable<Obj*, TagType8, NodeOptTag8>,
                                           fbl::WAVLTreeContainable<Obj*, TagType9, NodeOptTag9>> {
    uint32_t a, b, c;
  } test_obj;

  // Make sure the types have the options we expect when selected via tag
  static_assert(TYPE(FindSLLNode(test_obj))::kNodeOptions == NodeOptTag1,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindDLLNode(test_obj))::kNodeOptions == NodeOptTag2,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindWAVLNode(test_obj))::kNodeOptions == NodeOptTag3,
                "Default traits found the wrong node!");

  static_assert(TYPE(FindSLLNode<TagType4>(test_obj))::kNodeOptions == NodeOptTag4,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindDLLNode<TagType5>(test_obj))::kNodeOptions == NodeOptTag5,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindWAVLNode<TagType6>(test_obj))::kNodeOptions == NodeOptTag6,
                "Default traits found the wrong node!");

  static_assert(TYPE(FindSLLNode<TagType7>(test_obj))::kNodeOptions == NodeOptTag7,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindDLLNode<TagType8>(test_obj))::kNodeOptions == NodeOptTag8,
                "Default traits found the wrong node!");
  static_assert(TYPE(FindWAVLNode<TagType9>(test_obj))::kNodeOptions == NodeOptTag9,
                "Default traits found the wrong node!");

  // Make sure that all of the nodes are completely contained with the object.
  Range obj_range = Range::Of(test_obj);
  ASSERT_TRUE(Range::Of(FindSLLNode(test_obj)).ContainedBy(obj_range));
  ASSERT_TRUE(Range::Of(FindDLLNode(test_obj)).ContainedBy(obj_range));
  ASSERT_TRUE(Range::Of(FindWAVLNode(test_obj)).ContainedBy(obj_range));

  ASSERT_TRUE(Range::Of(FindSLLNode<TagType4>(test_obj)).ContainedBy(obj_range));
  ASSERT_TRUE(Range::Of(FindDLLNode<TagType5>(test_obj)).ContainedBy(obj_range));
  ASSERT_TRUE(Range::Of(FindWAVLNode<TagType6>(test_obj)).ContainedBy(obj_range));

  ASSERT_TRUE(Range::Of(FindSLLNode<TagType7>(test_obj)).ContainedBy(obj_range));
  ASSERT_TRUE(Range::Of(FindDLLNode<TagType8>(test_obj)).ContainedBy(obj_range));
  ASSERT_TRUE(Range::Of(FindWAVLNode<TagType9>(test_obj)).ContainedBy(obj_range));

  // Finally, make sure that none of the nodes overlap each other.
  ASSERT_TRUE(RangesAreNonOverlapping(std::array{
      Range::Of(FindSLLNode(test_obj)),
      Range::Of(FindDLLNode(test_obj)),
      Range::Of(FindWAVLNode(test_obj)),
      Range::Of(FindSLLNode<TagType4>(test_obj)),
      Range::Of(FindDLLNode<TagType5>(test_obj)),
      Range::Of(FindWAVLNode<TagType6>(test_obj)),
      Range::Of(FindSLLNode<TagType7>(test_obj)),
      Range::Of(FindDLLNode<TagType8>(test_obj)),
      Range::Of(FindWAVLNode<TagType9>(test_obj)),
  }));
}

TEST(IntrusiveContainerNodeTest, ContainerNodeTypeMatches) {
  // Make sure that the NodeType as understood by the container matches the
  // NodeType as defined by the mix-ins.  Start with the same complicated struct
  // we used for "ComplexContainables".
  struct Obj
      : public fbl::SinglyLinkedListable<Obj*, fbl::DefaultObjectTag, NodeOptTag1>,
        public fbl::DoublyLinkedListable<Obj*, fbl::DefaultObjectTag, NodeOptTag2>,
        public fbl::WAVLTreeContainable<Obj*, fbl::DefaultObjectTag, NodeOptTag3>,
        public fbl::ContainableBaseClasses<fbl::SinglyLinkedListable<Obj*, TagType4, NodeOptTag4>,
                                           fbl::DoublyLinkedListable<Obj*, TagType5, NodeOptTag5>,
                                           fbl::WAVLTreeContainable<Obj*, TagType6, NodeOptTag6>,
                                           fbl::SinglyLinkedListable<Obj*, TagType7, NodeOptTag7>,
                                           fbl::DoublyLinkedListable<Obj*, TagType8, NodeOptTag8>,
                                           fbl::WAVLTreeContainable<Obj*, TagType9, NodeOptTag9>> {
    uint32_t GetKey() const { return a; }
    uint32_t a, b, c;
  } test_obj;

  // Singly linked lists
  using DefaultSLL = fbl::SinglyLinkedList<Obj*>;
  static_assert(std::is_same_v<TYPE(FindSLLNode(test_obj)), DefaultSLL::NodeTraits::NodeState>,
                "Container disagrees about NodeState type");

  using Tag4SLL = fbl::TaggedSinglyLinkedList<Obj*, TagType4>;
  static_assert(
      std::is_same_v<TYPE(FindSLLNode<TagType4>(test_obj)), Tag4SLL::NodeTraits::NodeState>,
      "Container disagrees about NodeState type");

  using Tag7SLL = fbl::TaggedSinglyLinkedList<Obj*, TagType7>;
  static_assert(
      std::is_same_v<TYPE(FindSLLNode<TagType7>(test_obj)), Tag7SLL::NodeTraits::NodeState>,
      "Container disagrees about NodeState type");

  // Doubly linked lists
  using DefaultDLL = fbl::DoublyLinkedList<Obj*>;
  static_assert(std::is_same_v<TYPE(FindDLLNode(test_obj)), DefaultDLL::NodeTraits::NodeState>,
                "Container disagrees about NodeState type");

  using Tag5DLL = fbl::TaggedDoublyLinkedList<Obj*, TagType5>;
  static_assert(
      std::is_same_v<TYPE(FindDLLNode<TagType5>(test_obj)), Tag5DLL::NodeTraits::NodeState>,
      "Container disagrees about NodeState type");

  using Tag8DLL = fbl::TaggedDoublyLinkedList<Obj*, TagType8>;
  static_assert(
      std::is_same_v<TYPE(FindDLLNode<TagType8>(test_obj)), Tag8DLL::NodeTraits::NodeState>,
      "Container disagrees about NodeState type");

  // WAVL Trees
  using DefaultWAVL = fbl::WAVLTree<uint32_t, Obj*>;
  static_assert(std::is_same_v<TYPE(FindWAVLNode(test_obj)), DefaultWAVL::NodeTraits::NodeState>,
                "Container disagrees about NodeState type");

  using Tag6WAVL = fbl::TaggedWAVLTree<uint32_t, Obj*, TagType6>;
  static_assert(
      std::is_same_v<TYPE(FindWAVLNode<TagType6>(test_obj)), Tag6WAVL::NodeTraits::NodeState>,
      "Container disagrees about NodeState type");

  using Tag9WAVL = fbl::TaggedWAVLTree<uint32_t, Obj*, TagType9>;
  static_assert(
      std::is_same_v<TYPE(FindWAVLNode<TagType9>(test_obj)), Tag9WAVL::NodeTraits::NodeState>,
      "Container disagrees about NodeState type");
}

TEST(IntrusiveContainerNodeTest, SingleNodeInContainer) {
  // Make sure that all of the various InContainer helpers work when we happen
  // to be using custom node types.  The main check here is just be sure that
  // the templates expand properly when asked to do so with custom NodeOptions.
  // The actual functionality of InConainer is tested with the
  // container-specific tests.
  struct SLL : public fbl::SinglyLinkedListable<SLL*, fbl::DefaultObjectTag, NodeOptTag1> {
    uint32_t a, b, c;
  } test_sll_obj;
  ASSERT_FALSE(test_sll_obj.InContainer());
  ASSERT_FALSE(fbl::InContainer(test_sll_obj));  // Check the standalone version too

  struct DLL : public fbl::DoublyLinkedListable<DLL*, fbl::DefaultObjectTag, NodeOptTag2> {
    uint32_t a, b, c;
  } test_dll_obj;
  ASSERT_FALSE(test_dll_obj.InContainer());
  ASSERT_FALSE(fbl::InContainer(test_dll_obj));  // Check the standalone version too

  struct WAVL : public fbl::WAVLTreeContainable<WAVL*, fbl::DefaultObjectTag, NodeOptTag3> {
    uint32_t a, b, c;
  } test_wavl_obj;
  ASSERT_FALSE(test_wavl_obj.InContainer());
  ASSERT_FALSE(fbl::InContainer(test_wavl_obj));  // Check the standalone version too
}

TEST(IntrusiveContainerNodeTest, MultiNodeInContainer) {
  // Check to be sure that the standalone version of InContainer works with
  // tagged types, both with and without custom node options.
  struct Obj
      : public fbl::ContainableBaseClasses<fbl::SinglyLinkedListable<Obj*, TagType1>,
                                           fbl::DoublyLinkedListable<Obj*, TagType2>,
                                           fbl::WAVLTreeContainable<Obj*, TagType3>,
                                           fbl::SinglyLinkedListable<Obj*, TagType4, NodeOptTag4>,
                                           fbl::DoublyLinkedListable<Obj*, TagType5, NodeOptTag5>,
                                           fbl::WAVLTreeContainable<Obj*, TagType6, NodeOptTag6>> {
    uint32_t a, b, c;
  } test_obj;

  ASSERT_FALSE(fbl::InContainer<TagType1>(test_obj));
  ASSERT_FALSE(fbl::InContainer<TagType2>(test_obj));
  ASSERT_FALSE(fbl::InContainer<TagType3>(test_obj));
  ASSERT_FALSE(fbl::InContainer<TagType4>(test_obj));
  ASSERT_FALSE(fbl::InContainer<TagType5>(test_obj));
  ASSERT_FALSE(fbl::InContainer<TagType6>(test_obj));
}

}  // namespace
