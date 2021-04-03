// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>

#include <array>
#include <type_traits>

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
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
  return fbl::DefaultSinglyLinkedListTraits<T*, TypeTag>::node_state(obj);
}

template <typename TypeTag = fbl::DefaultObjectTag, typename T>
const auto& FindDLLNode(T& obj) {
  return fbl::DefaultDoublyLinkedListTraits<T*, TypeTag>::node_state(obj);
}

template <typename TypeTag = fbl::DefaultObjectTag, typename T>
const auto& FindWAVLNode(T& obj) {
  return fbl::DefaultWAVLTreeTraits<T*, TypeTag>::node_state(obj);
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
    fbl::DoublyLinkedListNodeState<DLL*, NodeOptTag2> dll_node_state_;
    uint32_t d, e, f;
  } test_dll_obj;

  // Same checks, this time for doubly linked list nodes.
  static_assert(TYPE(FindDLLNode(test_dll_obj))::kNodeOptions == NodeOptTag2,
                "Default traits found the wrong node!");
  ASSERT_TRUE(Range::Of(FindDLLNode(test_dll_obj)).ContainedBy(Range::Of(test_dll_obj)));

  struct WAVL {
    uintptr_t GetKey() const { return reinterpret_cast<uintptr_t>(this); }
    uint32_t a, b, c;
    fbl::WAVLTreeNodeState<WAVL*, NodeOptTag3> wavl_node_state_;
    uint32_t d, e, f;
  } test_wavl_obj;

  // Same checks, this time for WAVL trees nodes.
  static_assert(TYPE(FindWAVLNode(test_wavl_obj))::kNodeOptions == NodeOptTag3,
                "Default traits found the wrong node!");
  ASSERT_TRUE(Range::Of(FindWAVLNode(test_wavl_obj)).ContainedBy(Range::Of(test_wavl_obj)));

  // Make sure that we can instantiate containers which use these nodes.
  [[maybe_unused]] fbl::SinglyLinkedList<SLL*> sll;
  [[maybe_unused]] fbl::DoublyLinkedList<DLL*> dll;
  [[maybe_unused]] fbl::WAVLTree<uintptr_t, WAVL*> tree;
}

TEST(IntrusiveContainerNodeTest, DefaultSingleNode) {
  // Check to make sure that we can find a node in our object using the default mix-ins.
  struct SLL : public fbl::SinglyLinkedListable<SLL*, NodeOptTag1> {
    uint32_t a, b, c;
  } test_sll_obj;

  // Selecting our default node should give us a type with the proper option
  // tag, and should be completely contained somewhere within the test object.
  static_assert(TYPE(FindSLLNode(test_sll_obj))::kNodeOptions == NodeOptTag1,
                "Default traits found the wrong node!");
  ASSERT_TRUE(Range::Of(FindSLLNode(test_sll_obj)).ContainedBy(Range::Of(test_sll_obj)));

  struct DLL : public fbl::DoublyLinkedListable<DLL*, NodeOptTag2> {
    uint32_t a, b, c;
  } test_dll_obj;

  // Same checks, this time for doubly linked list nodes.
  static_assert(TYPE(FindDLLNode(test_dll_obj))::kNodeOptions == NodeOptTag2,
                "Default traits found the wrong node!");
  ASSERT_TRUE(Range::Of(FindDLLNode(test_dll_obj)).ContainedBy(Range::Of(test_dll_obj)));

  struct WAVL : public fbl::WAVLTreeContainable<WAVL*, NodeOptTag3> {
    uintptr_t GetKey() const { return reinterpret_cast<uintptr_t>(this); }
    uint32_t a, b, c;
  } test_wavl_obj;

  // Same checks, this time for WAVL trees nodes.
  static_assert(TYPE(FindWAVLNode(test_wavl_obj))::kNodeOptions == NodeOptTag3,
                "Default traits found the wrong node!");
  ASSERT_TRUE(Range::Of(FindWAVLNode(test_wavl_obj)).ContainedBy(Range::Of(test_wavl_obj)));

  // Make sure that we can instantiate containers which use these nodes.
  [[maybe_unused]] fbl::SinglyLinkedList<SLL*> sll;
  [[maybe_unused]] fbl::DoublyLinkedList<DLL*> dll;
  [[maybe_unused]] fbl::WAVLTree<uintptr_t, WAVL*> tree;
}

TEST(IntrusiveContainerNodeTest, MultipleSLLTaggedNodes) {
  // Check to make sure that we can locate each of our different node types in structures which use
  // the ContainableBaseClasses helper.
  struct SLL
      : public fbl::ContainableBaseClasses<fbl::SinglyLinkedListable<SLL*, NodeOptTag1, TagType1>,
                                           fbl::SinglyLinkedListable<SLL*, NodeOptTag2, TagType2>,
                                           fbl::SinglyLinkedListable<SLL*, NodeOptTag3, TagType3>> {
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

  // Make sure that we can instantiate containers which use these nodes.
  [[maybe_unused]] fbl::TaggedSinglyLinkedList<SLL*, TagType1> list1;
  [[maybe_unused]] fbl::TaggedSinglyLinkedList<SLL*, TagType2> list2;
  [[maybe_unused]] fbl::TaggedSinglyLinkedList<SLL*, TagType3> list3;
}

TEST(IntrusiveContainerNodeTest, MultipleDLLTaggedNodes) {
  // Check to make sure that we can locate each of our different node types in structures which use
  // the ContainableBaseClasses helper.
  struct DLL
      : public fbl::ContainableBaseClasses<fbl::DoublyLinkedListable<DLL*, NodeOptTag1, TagType1>,
                                           fbl::DoublyLinkedListable<DLL*, NodeOptTag2, TagType2>,
                                           fbl::DoublyLinkedListable<DLL*, NodeOptTag3, TagType3>> {
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

  // Make sure that we can instantiate containers which use these nodes.
  [[maybe_unused]] fbl::TaggedDoublyLinkedList<DLL*, TagType1> list1;
  [[maybe_unused]] fbl::TaggedDoublyLinkedList<DLL*, TagType2> list2;
  [[maybe_unused]] fbl::TaggedDoublyLinkedList<DLL*, TagType3> list3;
}

TEST(IntrusiveContainerNodeTest, MultipleWAVLTaggedNodes) {
  // Check to make sure that we can locate each of our different node types in structures which use
  // the ContainableBaseClasses helper.
  struct WAVL
      : public fbl::ContainableBaseClasses<fbl::WAVLTreeContainable<WAVL*, NodeOptTag1, TagType1>,
                                           fbl::WAVLTreeContainable<WAVL*, NodeOptTag2, TagType2>,
                                           fbl::WAVLTreeContainable<WAVL*, NodeOptTag3, TagType3>> {
    uintptr_t GetKey() const { return reinterpret_cast<uintptr_t>(this); }
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

  // Make sure that we can instantiate containers which use these nodes.
  [[maybe_unused]] fbl::TaggedWAVLTree<uintptr_t, WAVL*, TagType1> tree1;
  [[maybe_unused]] fbl::TaggedWAVLTree<uintptr_t, WAVL*, TagType2> tree2;
  [[maybe_unused]] fbl::TaggedWAVLTree<uintptr_t, WAVL*, TagType3> tree3;
}

TEST(IntrusiveContainerNodeTest, MultipleDifferentTaggedNodes) {
  // Check to make sure that we can locate each of our different node types in structures which use
  // the ContainableBaseClasses helper.
  struct Obj
      : public fbl::ContainableBaseClasses<fbl::SinglyLinkedListable<Obj*, NodeOptTag1, TagType1>,
                                           fbl::DoublyLinkedListable<Obj*, NodeOptTag2, TagType2>,
                                           fbl::WAVLTreeContainable<Obj*, NodeOptTag3, TagType3>> {
    uintptr_t GetKey() const { return reinterpret_cast<uintptr_t>(this); }
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

  // Make sure that we can instantiate containers which use these nodes.
  [[maybe_unused]] fbl::TaggedSinglyLinkedList<Obj*, TagType1> sll;
  [[maybe_unused]] fbl::TaggedDoublyLinkedList<Obj*, TagType2> dll;
  [[maybe_unused]] fbl::TaggedWAVLTree<uintptr_t, Obj*, TagType3> tree;
}

TEST(IntrusiveContainerNodeTest, MultipleDifferentDefaultNodes) {
  // Nodes are still permitted to have multiple default Containable mix-ins, as
  // long as the mix-ins are for different types of containers.
  struct Obj : public fbl::SinglyLinkedListable<Obj*, NodeOptTag1>,
               public fbl::DoublyLinkedListable<Obj*, NodeOptTag2>,
               public fbl::WAVLTreeContainable<Obj*, NodeOptTag3> {
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

  // Make sure that we can instantiate containers which use these nodes.
  [[maybe_unused]] fbl::SinglyLinkedList<Obj*> sll;
  [[maybe_unused]] fbl::DoublyLinkedList<Obj*> dll;
  [[maybe_unused]] fbl::WAVLTree<uintptr_t, Obj*> tree;
}

TEST(IntrusiveContainerNodeTest, ComplicatedContainables) {
  // OK, now let's make a really complicated example.  A structure which uses
  // all three of the default base mix-ins, as well as multiple instances of
  // each of the tagged node types in a ContainedBaseClasses expression.
  struct Obj
      : public fbl::SinglyLinkedListable<Obj*, NodeOptTag1>,
        public fbl::DoublyLinkedListable<Obj*, NodeOptTag2>,
        public fbl::WAVLTreeContainable<Obj*, NodeOptTag3>,
        public fbl::ContainableBaseClasses<fbl::SinglyLinkedListable<Obj*, NodeOptTag4, TagType4>,
                                           fbl::DoublyLinkedListable<Obj*, NodeOptTag5, TagType5>,
                                           fbl::WAVLTreeContainable<Obj*, NodeOptTag6, TagType6>,
                                           fbl::SinglyLinkedListable<Obj*, NodeOptTag7, TagType7>,
                                           fbl::DoublyLinkedListable<Obj*, NodeOptTag8, TagType8>,
                                           fbl::WAVLTreeContainable<Obj*, NodeOptTag9, TagType9>> {
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

  // Make sure that we can instantiate containers which use these nodes.
  [[maybe_unused]] fbl::SinglyLinkedList<Obj*> default_sll;
  [[maybe_unused]] fbl::TaggedSinglyLinkedList<Obj*, TagType4> sll_tag4;
  [[maybe_unused]] fbl::TaggedSinglyLinkedList<Obj*, TagType7> sll_tag7;

  [[maybe_unused]] fbl::DoublyLinkedList<Obj*> default_dll;
  [[maybe_unused]] fbl::TaggedDoublyLinkedList<Obj*, TagType5> dll_tag5;
  [[maybe_unused]] fbl::TaggedDoublyLinkedList<Obj*, TagType8> dll_tag8;

  [[maybe_unused]] fbl::WAVLTree<uintptr_t, Obj*> default_tree;
  [[maybe_unused]] fbl::TaggedWAVLTree<uintptr_t, Obj*, TagType6> tree_tag6;
  [[maybe_unused]] fbl::TaggedWAVLTree<uintptr_t, Obj*, TagType9> tree_tag9;
}

TEST(IntrusiveContainerNodeTest, ContainerNodeTypeMatches) {
  // Make sure that the NodeType as understood by the container matches the
  // NodeType as defined by the mix-ins.  Start with the same complicated struct
  // we used for "ComplexContainables".
  struct Obj
      : public fbl::SinglyLinkedListable<Obj*, NodeOptTag1>,
        public fbl::DoublyLinkedListable<Obj*, NodeOptTag2>,
        public fbl::WAVLTreeContainable<Obj*, NodeOptTag3>,
        public fbl::ContainableBaseClasses<fbl::SinglyLinkedListable<Obj*, NodeOptTag4, TagType4>,
                                           fbl::DoublyLinkedListable<Obj*, NodeOptTag5, TagType5>,
                                           fbl::WAVLTreeContainable<Obj*, NodeOptTag6, TagType6>,
                                           fbl::SinglyLinkedListable<Obj*, NodeOptTag7, TagType7>,
                                           fbl::DoublyLinkedListable<Obj*, NodeOptTag8, TagType8>,
                                           fbl::WAVLTreeContainable<Obj*, NodeOptTag9, TagType9>> {
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
  struct SLL : public fbl::SinglyLinkedListable<SLL*, NodeOptTag1> {
    uint32_t a, b, c;
  } test_sll_obj;
  ASSERT_FALSE(test_sll_obj.InContainer());
  ASSERT_FALSE(fbl::InContainer(test_sll_obj));  // Check the standalone version too

  struct DLL : public fbl::DoublyLinkedListable<DLL*, NodeOptTag2> {
    uint32_t a, b, c;
  } test_dll_obj;
  ASSERT_FALSE(test_dll_obj.InContainer());
  ASSERT_FALSE(fbl::InContainer(test_dll_obj));  // Check the standalone version too

  struct WAVL : public fbl::WAVLTreeContainable<WAVL*, NodeOptTag3> {
    uint32_t a, b, c;
  } test_wavl_obj;
  ASSERT_FALSE(test_wavl_obj.InContainer());
  ASSERT_FALSE(fbl::InContainer(test_wavl_obj));  // Check the standalone version too
}

TEST(IntrusiveContainerNodeTest, MultiNodeInContainer) {
  // Check to be sure that the standalone version of InContainer works with
  // tagged types, both with and without custom node options.
  struct Obj
      : public fbl::ContainableBaseClasses<fbl::TaggedSinglyLinkedListable<Obj*, TagType1>,
                                           fbl::TaggedDoublyLinkedListable<Obj*, TagType2>,
                                           fbl::TaggedWAVLTreeContainable<Obj*, TagType3>,
                                           fbl::SinglyLinkedListable<Obj*, NodeOptTag4, TagType4>,
                                           fbl::DoublyLinkedListable<Obj*, NodeOptTag5, TagType5>,
                                           fbl::WAVLTreeContainable<Obj*, NodeOptTag6, TagType6>> {
    uint32_t a, b, c;
  } test_obj;

  ASSERT_FALSE(fbl::InContainer<TagType1>(test_obj));
  ASSERT_FALSE(fbl::InContainer<TagType2>(test_obj));
  ASSERT_FALSE(fbl::InContainer<TagType3>(test_obj));
  ASSERT_FALSE(fbl::InContainer<TagType4>(test_obj));
  ASSERT_FALSE(fbl::InContainer<TagType5>(test_obj));
  ASSERT_FALSE(fbl::InContainer<TagType6>(test_obj));
}

// Start a new anon namespace for the Copy/Move node tests.  We are going to
// define some boilerplate node and container types for the tests, and we don't
// want their definitions to leak out into the rest of the test environment.
namespace {

template <fbl::NodeOptions Options>
struct TestSLLObj : public fbl::SinglyLinkedListable<TestSLLObj<Options>*, Options> {};

template <fbl::NodeOptions Options>
using TestSLLContainer = fbl::SinglyLinkedList<TestSLLObj<Options>*>;

template <fbl::NodeOptions Options>
struct TestDLLObj : public fbl::DoublyLinkedListable<TestDLLObj<Options>*, Options> {};

template <fbl::NodeOptions Options>
using TestDLLContainer = fbl::DoublyLinkedList<TestDLLObj<Options>*>;

template <fbl::NodeOptions Options>
struct TestWAVLObj : public fbl::WAVLTreeContainable<TestWAVLObj<Options>*, Options> {
  TestWAVLObj() = default;

  // Make sure that our keys are always unique even though we are using the
  // implicit default constructor and assignment operators.
  uint64_t GetKey() const { return reinterpret_cast<uint64_t>(this); }
};

template <fbl::NodeOptions Options>
using TestWAVLContainer = fbl::WAVLTree<uint64_t, TestWAVLObj<Options>*>;

// By default, none of these operations will be allowed at compile time.
// Sadly, negative compilation testing here involves enabling each of these
// cases and making sure that it properly fails to compile.
template <typename Container>
void CopyTestHelper() {
  using Obj = typename Container::ValueType;
  constexpr bool kAnyCopyAllowed =
      Container::NodeTraits::NodeState::kNodeOptions &
      (fbl::NodeOptions::AllowCopy | fbl::NodeOptions::AllowCopyFromContainer);
  constexpr bool kFromContainerAllowed =
      Container::NodeTraits::NodeState::kNodeOptions & fbl::NodeOptions::AllowCopyFromContainer;

  // Copy construct while not in a container
  Obj A, C;
  Obj B{A};

  ASSERT_FALSE(A.InContainer());
  ASSERT_FALSE(B.InContainer());
  ASSERT_FALSE(C.InContainer());

  // Copy assign while not in a container
  C = A;

  ASSERT_FALSE(A.InContainer());
  ASSERT_FALSE(C.InContainer());

  // Don't bother to expand any of the subsequent tests if no copy of any form
  // is allowed.
  if constexpr (kAnyCopyAllowed) {
    // Make sure that we always clean our container before allowing the
    // container, or any nodes in the container the chance to destruct.
    Container container;
    auto cleanup = fit::defer([&container]() { container.clear(); });

    // For these tests, we want A and B to be in the container, while C is not
    // in the container.  Also, keep track of who is initially first the
    // container and who is second.  While we can control the order of elements
    // in the container for sequenced container, we are using object pointers as
    // keys for the WAVL objects, whose order we cannot control as well.
    if constexpr (Container::IsAssociative) {
      container.insert(&A);
      container.insert(&B);
    } else {
      container.push_front(&A);
      container.push_front(&B);
    }

    const auto& first_obj = container.front();
    const auto& second_obj = *(++container.begin());

    // No matter what we do with the rest of these tests, the positions of A and
    // B in the container, and the fact that C is _not_ in the container, should
    // remain unchanged.  Make a small lambda that we can use to check this over
    // and over again.
    auto SanityCheckABC = [&]() {
      ASSERT_TRUE(A.InContainer());
      ASSERT_TRUE(B.InContainer());
      ASSERT_FALSE(C.InContainer());
      ASSERT_EQ(&first_obj, &container.front());
      ASSERT_EQ(&second_obj, &(*(++container.begin())));
    };
    ASSERT_NO_FAILURES(SanityCheckABC());

    if constexpr (kFromContainerAllowed || !ZX_DEBUG_ASSERT_IMPLEMENTED) {
      // Attempt to copy construct D from A which is currently in the container.
      // This should succeed as we are either explicitly allowed to do this (by
      // NodeOptions), or because DEBUG_ASSERTs are not enabled in this build.
      // Once the construction has happened, both A and B should still be in the
      // container, and their positions unchanged.
      Obj D{container.front()};
      ASSERT_FALSE(D.InContainer());
      ASSERT_NO_FAILURES(SanityCheckABC());

      // Assignment from A (in the container) to C (not in container) should
      // succeed for the same reason and preserve all of the same things.
      C = container.front();
      ASSERT_NO_FAILURES(SanityCheckABC());

      // Assignment from C (not in the container) to A (in container) should
      // succeed.
      container.front() = C;
      ASSERT_NO_FAILURES(SanityCheckABC());

      // Finally, assignment from A to B (both in the container) should succeed,
      // but not change anything about the positions of A or B in the container.
      B = container.front();
      ASSERT_NO_FAILURES(SanityCheckABC());
    } else {
      // ASSERT_DEATH is only defined for __Fuchsia__
#ifdef __Fuchsia__
      // Do tests we did in the other half of this `if`, but this time, expect
      // them to result in death.  The NodeOptions do not allow us to do these
      // copies, and DEBUG_ASSERTs are enabled.

      // Copy construct a D.
      auto copy_construct_D = [&container]() { Obj D{container.front()}; };
      ASSERT_DEATH(copy_construct_D);
      ASSERT_NO_FAILURES(SanityCheckABC());

      // Assign A (in container) to C (not in container)
      auto copy_assign_A_to_C_lambda = [&container, &C]() { C = container.front(); };
      ASSERT_DEATH(copy_assign_A_to_C_lambda);
      ASSERT_NO_FAILURES(SanityCheckABC());

      // Assign C (not in container) to A (in container)
      auto copy_assign_C_to_A_lambda = [&container, &C]() { container.front() = C; };
      ASSERT_DEATH(copy_assign_C_to_A_lambda);
      ASSERT_NO_FAILURES(SanityCheckABC());

      // Assign A (not in container) to B (in container)
      auto copy_assign_A_to_B_lambda = [&A, &B]() { B = A; };
      ASSERT_DEATH(copy_assign_A_to_B_lambda);
      ASSERT_NO_FAILURES(SanityCheckABC());
      ASSERT_TRUE(A.InContainer());
      ASSERT_TRUE(B.InContainer());
      ASSERT_FALSE(C.InContainer());
#endif
    }
  }
}

template <typename Container>
void MoveTestHelper() {
  // Same tests as the CopyTestHelper, but this time use Move instead.
  using Obj = typename Container::ValueType;
  constexpr bool kAnyMoveAllowed =
      Container::NodeTraits::NodeState::kNodeOptions &
      (fbl::NodeOptions::AllowMove | fbl::NodeOptions::AllowMoveFromContainer);
  constexpr bool kFromContainerAllowed =
      Container::NodeTraits::NodeState::kNodeOptions & fbl::NodeOptions::AllowMoveFromContainer;
  // Move construct while not in a container
  Obj A, C;
  Obj B{std::move(A)};

  ASSERT_FALSE(A.InContainer());
  ASSERT_FALSE(B.InContainer());
  ASSERT_FALSE(C.InContainer());

  // Move assign while not in a container
  C = std::move(A);

  ASSERT_FALSE(A.InContainer());
  ASSERT_FALSE(C.InContainer());

  // Don't bother to expand any of the subsequent tests if no move of any form
  // is allowed.
  if constexpr (kAnyMoveAllowed) {
    // Make sure that we always clean our container before allowing the
    // container, or any nodes in the container the chance to destruct.
    Container container;
    auto cleanup = fit::defer([&container]() { container.clear(); });

    // For these tests, we want A and B to be in the container, while C is not
    // in the container.  Also, keep track of who is initially first the
    // container and who is second.  While we can control the order of elements
    // in the container for sequenced container, we are using object pointers as
    // keys for the WAVL objects, whose order we cannot control as well.
    if constexpr (Container::IsAssociative) {
      container.insert(&A);
      container.insert(&B);
    } else {
      container.push_front(&B);
      container.push_front(&A);
    }

    const auto& first_obj = container.front();
    const auto& second_obj = *(++container.begin());

    // No matter what we do with the rest of these tests, the positions of A and
    // B in the container, and the fact that C is _not_ in the container, should
    // remain unchanged.  Make a small lambda that we can use to check this over
    // and over again.
    auto SanityCheckABC = [&]() {
      ASSERT_TRUE(A.InContainer());
      ASSERT_TRUE(B.InContainer());
      ASSERT_FALSE(C.InContainer());
      ASSERT_EQ(&first_obj, &container.front());
      ASSERT_EQ(&second_obj, &(*(++container.begin())));
    };
    ASSERT_NO_FAILURES(SanityCheckABC());

    if constexpr (kFromContainerAllowed || !ZX_DEBUG_ASSERT_IMPLEMENTED) {
      // Attempt to move construct D from A which is currently in the container.
      // This should succeed as we are either explicitly allowed to do this (by
      // NodeOptions), or because DEBUG_ASSERTs are not enabled in this build.
      // Once the construction has happened, both A and B should still be in the
      // container, and their positions unchanged.
      Obj D{std::move(container.front())};
      ASSERT_FALSE(D.InContainer());
      ASSERT_NO_FAILURES(SanityCheckABC());

      // Move assignment from A (in the container) to C (not in container) should
      // succeed for the same reason and preserve all of the same things.
      C = std::move(container.front());
      ASSERT_NO_FAILURES(SanityCheckABC());

      // Assignment from C (not in the container) to A (in container) should
      // succeed.
      container.front() = std::move(C);
      ASSERT_NO_FAILURES(SanityCheckABC());

      // Finally, assignment from A to B (both in the container) should succeed,
      // but not change anything about the positions of A or B in the container.
      B = std::move(A);
      ASSERT_NO_FAILURES(SanityCheckABC());
    } else {
      // ASSERT_DEATH is only defined for __Fuchsia__
#ifdef __Fuchsia__
      // Do tests we did in the other half of this `if`, but this time, expect
      // them to result in death.  The NodeOptions do not allow us to do these
      // copies, and DEBUG_ASSERTs are enabled.

      // Move construct a D.
      auto move_construct_D = [&container]() { Obj D{std::move(container.front())}; };
      ASSERT_DEATH(move_construct_D);
      ASSERT_NO_FAILURES(SanityCheckABC());

      // Assign A (in container) to C (not in container)
      auto move_assign_A_to_C_lambda = [&container, &C]() { C = std::move(container.front()); };
      ASSERT_DEATH(move_assign_A_to_C_lambda);
      ASSERT_NO_FAILURES(SanityCheckABC());

      // Assign C (not in container) to A (in container)
      auto move_assign_C_to_A_lambda = [&container, &C]() { container.front() = std::move(C); };
      ASSERT_DEATH(move_assign_C_to_A_lambda);
      ASSERT_NO_FAILURES(SanityCheckABC());

      // Assign A (not in container) to B (in container)
      auto move_assign_A_to_B_lambda = [&A, &B]() { B = std::move(A); };
      ASSERT_DEATH(move_assign_A_to_B_lambda);
      ASSERT_NO_FAILURES(SanityCheckABC());
#endif
    }
  }
}

TEST(IntrusiveContainerNodeTest, CopyAndMoveDisallowed) {
  using Opts [[maybe_unused]] = fbl::NodeOptions;
#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(CopyTestHelper<TestSLLContainer<Opts::None>>());
  ASSERT_NO_FAILURES(MoveTestHelper<TestSLLContainer<Opts::None>>());
#endif

#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(CopyTestHelper<TestDLLContainer<Opts::None>>());
  ASSERT_NO_FAILURES(MoveTestHelper<TestDLLContainer<Opts::None>>());
#endif

#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(CopyTestHelper<TestWAVLContainer<Opts::None>>());
  ASSERT_NO_FAILURES(MoveTestHelper<TestWAVLContainer<Opts::None>>());
#endif
}

TEST(IntrusiveContainerNodeTest, CopyAllowedOutsideOfContainer) {
  using Opts = fbl::NodeOptions;
  ASSERT_NO_FAILURES(CopyTestHelper<TestSLLContainer<Opts::AllowCopy>>());
#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(MoveTestHelper<TestSLLContainer<Opts::AllowCopy>>());
#endif

  ASSERT_NO_FAILURES(CopyTestHelper<TestDLLContainer<Opts::AllowCopy>>());
#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(MoveTestHelper<TestDLLContainer<Opts::AllowCopy>>());
#endif

  ASSERT_NO_FAILURES(CopyTestHelper<TestWAVLContainer<Opts::AllowCopy>>());
#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(MoveTestHelper<TestWAVLContainer<Opts::AllowCopy>>());
#endif
}

TEST(IntrusiveContainerNodeTest, CopyAllowedWhileInsideContainer) {
  using Opts = fbl::NodeOptions;
  ASSERT_NO_FAILURES(CopyTestHelper<TestSLLContainer<Opts::AllowCopyFromContainer>>());
#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(MoveTestHelper<TestSLLContainer<Opts::AllowCopyFromContainer>>());
#endif

  ASSERT_NO_FAILURES(CopyTestHelper<TestDLLContainer<Opts::AllowCopyFromContainer>>());
#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(MoveTestHelper<TestDLLContainer<Opts::AllowCopyFromContainer>>());
#endif

  ASSERT_NO_FAILURES(CopyTestHelper<TestWAVLContainer<Opts::AllowCopyFromContainer>>());
#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(MoveTestHelper<TestWAVLContainer<Opts::AllowCopyFromContainer>>());
#endif
}

TEST(IntrusiveContainerNodeTest, MoveAllowedOutsideOfContainer) {
  using Opts = fbl::NodeOptions;
#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(CopyTestHelper<TestSLLContainer<Opts::AllowMove>>());
#endif
  ASSERT_NO_FAILURES(MoveTestHelper<TestSLLContainer<Opts::AllowMove>>());

#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(CopyTestHelper<TestDLLContainer<Opts::AllowMove>>());
#endif
  ASSERT_NO_FAILURES(MoveTestHelper<TestDLLContainer<Opts::AllowMove>>());

#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(CopyTestHelper<TestWAVLContainer<Opts::AllowMove>>());
#endif
  ASSERT_NO_FAILURES(MoveTestHelper<TestWAVLContainer<Opts::AllowMove>>());
}

TEST(IntrusiveContainerNodeTest, MoveAllowedWhileInsideContainer) {
  using Opts = fbl::NodeOptions;
#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(CopyTestHelper<TestSLLContainer<Opts::AllowMoveFromContainer>>());
#endif
  ASSERT_NO_FAILURES(MoveTestHelper<TestSLLContainer<Opts::AllowMoveFromContainer>>());

#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(CopyTestHelper<TestDLLContainer<Opts::AllowMoveFromContainer>>());
#endif
  ASSERT_NO_FAILURES(MoveTestHelper<TestDLLContainer<Opts::AllowMoveFromContainer>>());

#if TEST_WILL_NOT_COMPILE || 0
  ASSERT_NO_FAILURES(CopyTestHelper<TestWAVLContainer<Opts::AllowMoveFromContainer>>());
#endif
  ASSERT_NO_FAILURES(MoveTestHelper<TestWAVLContainer<Opts::AllowMoveFromContainer>>());
}

TEST(IntrusiveContainerNodeTest, CopyMoveAllowedOutsideOfContainer) {
  using Opts = fbl::NodeOptions;

  // Test both the long form (using the option | operator) as well as the
  // shorthand (CopyMove) form of the option flags.
  ASSERT_NO_FAILURES(CopyTestHelper<TestSLLContainer<Opts::AllowCopy | Opts::AllowMove>>());
  ASSERT_NO_FAILURES(MoveTestHelper<TestSLLContainer<Opts::AllowCopy | Opts::AllowMove>>());
  ASSERT_NO_FAILURES(CopyTestHelper<TestSLLContainer<Opts::AllowCopyMove>>());
  ASSERT_NO_FAILURES(MoveTestHelper<TestSLLContainer<Opts::AllowCopyMove>>());

  ASSERT_NO_FAILURES(CopyTestHelper<TestDLLContainer<Opts::AllowCopy | Opts::AllowMove>>());
  ASSERT_NO_FAILURES(MoveTestHelper<TestDLLContainer<Opts::AllowCopy | Opts::AllowMove>>());
  ASSERT_NO_FAILURES(CopyTestHelper<TestDLLContainer<Opts::AllowCopyMove>>());
  ASSERT_NO_FAILURES(MoveTestHelper<TestDLLContainer<Opts::AllowCopyMove>>());

  ASSERT_NO_FAILURES(CopyTestHelper<TestWAVLContainer<Opts::AllowCopy | Opts::AllowMove>>());
  ASSERT_NO_FAILURES(MoveTestHelper<TestWAVLContainer<Opts::AllowCopy | Opts::AllowMove>>());
  ASSERT_NO_FAILURES(CopyTestHelper<TestWAVLContainer<Opts::AllowCopyMove>>());
  ASSERT_NO_FAILURES(MoveTestHelper<TestWAVLContainer<Opts::AllowCopyMove>>());
}

TEST(IntrusiveContainerNodeTest, CopyMoveAllowedWhileInsideContainer) {
  using Opts = fbl::NodeOptions;

  // Test both the long form (using the option | operator) as well as the
  // shorthand (CopyMove) form of the option flags.
  ASSERT_NO_FAILURES(
      CopyTestHelper<
          TestSLLContainer<Opts::AllowCopyFromContainer | Opts::AllowMoveFromContainer>>());
  ASSERT_NO_FAILURES(
      MoveTestHelper<
          TestSLLContainer<Opts::AllowCopyFromContainer | Opts::AllowMoveFromContainer>>());
  ASSERT_NO_FAILURES(CopyTestHelper<TestSLLContainer<Opts::AllowCopyMoveFromContainer>>());
  ASSERT_NO_FAILURES(MoveTestHelper<TestSLLContainer<Opts::AllowCopyMoveFromContainer>>());

  ASSERT_NO_FAILURES(
      CopyTestHelper<
          TestDLLContainer<Opts::AllowCopyFromContainer | Opts::AllowMoveFromContainer>>());
  ASSERT_NO_FAILURES(
      MoveTestHelper<
          TestDLLContainer<Opts::AllowCopyFromContainer | Opts::AllowMoveFromContainer>>());
  ASSERT_NO_FAILURES(CopyTestHelper<TestDLLContainer<Opts::AllowCopyMoveFromContainer>>());
  ASSERT_NO_FAILURES(MoveTestHelper<TestDLLContainer<Opts::AllowCopyMoveFromContainer>>());

  ASSERT_NO_FAILURES(
      CopyTestHelper<
          TestWAVLContainer<Opts::AllowCopyFromContainer | Opts::AllowMoveFromContainer>>());
  ASSERT_NO_FAILURES(
      MoveTestHelper<
          TestWAVLContainer<Opts::AllowCopyFromContainer | Opts::AllowMoveFromContainer>>());
  ASSERT_NO_FAILURES(CopyTestHelper<TestWAVLContainer<Opts::AllowCopyMoveFromContainer>>());
  ASSERT_NO_FAILURES(MoveTestHelper<TestWAVLContainer<Opts::AllowCopyMoveFromContainer>>());
}

TEST(IntrusiveContainerNodeTest, AllowMultiContainerUptrTest) {
  // Make sure that objects which can exist in multiple containers
  // simultaniously, but which use unique_ptr to track the object lifecycle,
  // need to explicitly enable the behavior using the AllowMultiContainerUptr
  // node option.

  // Start with the example used in the Option's comment.
  struct TwoListsOneUptrObj
      : public fbl::ContainableBaseClasses<
            fbl::DoublyLinkedListable<std::unique_ptr<TwoListsOneUptrObj>,
                                      fbl::NodeOptions::AllowMultiContainerUptr, TagType1>,
            fbl::TaggedSinglyLinkedListable<TwoListsOneUptrObj*, TagType2>> {
    uint32_t a, b, c;
  };

  {
    [[maybe_unused]] fbl::TaggedDoublyLinkedList<std::unique_ptr<TwoListsOneUptrObj>, TagType1> dll;
    [[maybe_unused]] fbl::TaggedSinglyLinkedList<TwoListsOneUptrObj*, TagType2> sll;
  }

  // An object which can exist in either one container type, or the other (just
  // not simultaneously) is also legal if the user opts-in.
  struct DisjointObj
      : public fbl::ContainableBaseClasses<
            fbl::DoublyLinkedListable<std::unique_ptr<DisjointObj>,
                                      fbl::NodeOptions::AllowMultiContainerUptr, TagType1>,
            fbl::WAVLTreeContainable<std::unique_ptr<DisjointObj>,
                                     fbl::NodeOptions::AllowMultiContainerUptr, TagType2>> {
    uint32_t GetKey() const { return a; }
    uint32_t a, b, c;
  };

  {
    [[maybe_unused]] fbl::TaggedDoublyLinkedList<std::unique_ptr<DisjointObj>, TagType1> dll;
    [[maybe_unused]] fbl::TaggedWAVLTree<uint32_t, std::unique_ptr<DisjointObj>, TagType2> tree;
  }

  // A list of containers which contains exactly one container whose pointer
  // type is unique_ptr is OK as well.
  struct IllegalOneListObj
      : public fbl::ContainableBaseClasses<
            fbl::TaggedDoublyLinkedListable<std::unique_ptr<IllegalOneListObj>, TagType1>> {
    uint32_t a, b, c;
  };

  {
    [[maybe_unused]] fbl::TaggedDoublyLinkedList<std::unique_ptr<IllegalOneListObj>, TagType1> dll;
  }

  // If we add _any_ other containers (regardless of pointer type), this should
  // fail.
#if TEST_WILL_NOT_COMPILE || 0
  struct IllegalTwoListObjRawPtr
      : public fbl::ContainableBaseClasses<
            fbl::TaggedDoublyLinkedListable<std::unique_ptr<IllegalTwoListObjRawPtr>, TagType1>,
            fbl::TaggedDoublyLinkedListable<IllegalTwoListObjRawPtr*, TagType2>> {
    uint32_t a, b, c;
  };

  {
    [[maybe_unused]] fbl::TaggedDoublyLinkedList<std::unique_ptr<IllegalTwoListObjRawPtr>, TagType1>
        dll1;
    [[maybe_unused]] fbl::TaggedDoublyLinkedList<std::unique_ptr<IllegalTwoListObjRawPtr>, TagType2>
        dll2;
  }
#endif

#if TEST_WILL_NOT_COMPILE || 0
  struct IllegalTwoListObjUPtr
      : public fbl::ContainableBaseClasses<
            fbl::TaggedDoublyLinkedListable<std::unique_ptr<IllegalTwoListObjUPtr>, TagType1>,
            fbl::TaggedDoublyLinkedListable<std::unique_ptr<IllegalTwoListObjUPtr>, TagType2>> {
    uint32_t a, b, c;
  };

  {
    [[maybe_unused]] fbl::TaggedDoublyLinkedList<std::unique_ptr<IllegalTwoListObjUPtr>, TagType1>
        dll1;
    [[maybe_unused]] fbl::TaggedDoublyLinkedList<std::unique_ptr<IllegalTwoListObjUPtr>, TagType2>
        dll2;
  }
#endif

#if TEST_WILL_NOT_COMPILE || 0
  struct IllegalTwoListObjRefPtr
      : public fbl::RefCounted<IllegalTwoListObjRefPtr>,
        public fbl::ContainableBaseClasses<
            fbl::TaggedDoublyLinkedListable<std::unique_ptr<IllegalTwoListObjRefPtr>, TagType1>,
            fbl::TaggedDoublyLinkedListable<fbl::RefPtr<IllegalTwoListObjRefPtr>, TagType2>> {
    uint32_t a, b, c;
  };

  {
    [[maybe_unused]] fbl::TaggedDoublyLinkedList<std::unique_ptr<IllegalTwoListObjRefPtr>, TagType1>
        dll1;
    [[maybe_unused]] fbl::TaggedDoublyLinkedList<std::unique_ptr<IllegalTwoListObjRefPtr>, TagType2>
        dll2;
  }
#endif
}

}  // namespace
}  // namespace
