// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/intrusive_single_list.h>
#include <fbl/tests/intrusive_containers/intrusive_singly_linked_list_checker.h>
#include <fbl/tests/intrusive_containers/sequence_container_test_environment.h>
#include <fbl/tests/intrusive_containers/test_thunks.h>
#include <zxtest/zxtest.h>

namespace fbl {
namespace tests {
namespace intrusive_containers {

template <typename ContainerStateType>
struct OtherListTraits {
  using PtrTraits = typename ContainerStateType::PtrTraits;
  static ContainerStateType& node_state(typename PtrTraits::RefType obj) {
    return obj.other_container_state_;
  }
};

template <typename PtrType, NodeOptions kNodeOptions = NodeOptions::None>
class SLLTraits {
 public:
  // clang-format off
  using TestObjBaseType         = TestObjBase;

  using ContainerType           = SinglyLinkedList<PtrType>;
  using ContainableBaseClass    = SinglyLinkedListable<PtrType, kNodeOptions>;
  using ContainerStateType      = SinglyLinkedListNodeState<PtrType, kNodeOptions>;

  using OtherContainerStateType = ContainerStateType;
  using OtherContainerTraits    = OtherListTraits<OtherContainerStateType>;
  using OtherContainerType      = SinglyLinkedListCustomTraits<PtrType, OtherContainerTraits>;
  // clang-format on

  struct Tag1 {};
  struct Tag2 {};
  struct Tag3 {};

  using TaggedContainableBaseClasses =
      fbl::ContainableBaseClasses<TaggedSinglyLinkedListable<PtrType, Tag1>,
                                  TaggedSinglyLinkedListable<PtrType, Tag2>,
                                  TaggedSinglyLinkedListable<PtrType, Tag3>>;

  using TaggedType1 = TaggedSinglyLinkedList<PtrType, Tag1>;
  using TaggedType2 = TaggedSinglyLinkedList<PtrType, Tag2>;
  using TaggedType3 = TaggedSinglyLinkedList<PtrType, Tag3>;
};

// Just a sanity check so we know our metaprogramming nonsense is
// doing what we expect:
static_assert(
    std::is_same_v<typename SLLTraits<int*>::TaggedContainableBaseClasses::TagTypes,
                   std::tuple<typename SLLTraits<int*>::Tag1, typename SLLTraits<int*>::Tag2,
                              typename SLLTraits<int*>::Tag3>>);

// Negative compilation tests which make sure that we don't accidentally
// mismatch pointer types between the node and the container.
TEST(SinglyLinkedListTest, MismatchedPointerType) {
  struct Obj {
    fbl::SinglyLinkedListNodeState<Obj*> sll_node_state_;
  };
#if TEST_WILL_NOT_COMPILE || 0
  [[maybe_unused]] fbl::SinglyLinkedList<std::unique_ptr<Obj>> list;
#endif
}

// Negative compilation test which make sure that we cannot try to use a node
// flagged with AllowRemoveFromContainer with a sized list.
TEST(SinglyLinkedListTest, NoRemoveFromContainer) {
  struct Obj : public SinglyLinkedListable<Obj*, NodeOptions::AllowRemoveFromContainer> {};
#if TEST_WILL_NOT_COMPILE || 0
  [[maybe_unused]] fbl::SinglyLinkedList<Obj*> list;
#endif
}

// clang-format off
DEFINE_TEST_OBJECTS(SLL);
using UMTE   = DEFINE_TEST_THUNK(Sequence, SLL, Unmanaged);
using UPDDTE = DEFINE_TEST_THUNK(Sequence, SLL, UniquePtrDefaultDeleter);
using UPCDTE = DEFINE_TEST_THUNK(Sequence, SLL, UniquePtrCustomDeleter);
using RPTE   = DEFINE_TEST_THUNK(Sequence, SLL, RefPtr);
VERIFY_CONTAINER_SIZES(SLL, sizeof(void*));

// Versions of the test objects which support clear_unsafe.
template <typename PtrType>
using CU_SLLTraits = SLLTraits<PtrType, fbl::NodeOptions::AllowClearUnsafe>;
DEFINE_TEST_OBJECTS(CU_SLL);
using CU_UMTE   = DEFINE_TEST_THUNK(Sequence, CU_SLL, Unmanaged);
using CU_UPDDTE = DEFINE_TEST_THUNK(Sequence, CU_SLL, UniquePtrDefaultDeleter);
VERIFY_CONTAINER_SIZES(CU_SLL, sizeof(void*));

//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    Clear)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  Clear)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  Clear)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    Clear)

#if TEST_WILL_NOT_COMPILE || 0
// Won't compile because node lacks AllowClearUnsafe option.
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    ClearUnsafe)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  ClearUnsafe)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  ClearUnsafe)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    ClearUnsafe)
#endif

#if TEST_WILL_NOT_COMPILE || 0
// Won't compile because pointer type is managed.
RUN_ZXTEST(SinglyLinkedListTest, CU_UPDDTE,  ClearUnsafe)
#endif

RUN_ZXTEST(SinglyLinkedListTest, CU_UMTE, ClearUnsafe)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    IsEmpty)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  IsEmpty)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  IsEmpty)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    IsEmpty)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    Iterate)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  Iterate)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  Iterate)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    Iterate)

// SinglyLinkedLists cannot perform direct erase operations, nor can they erase
// using an iterator.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    IterErase)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  IterErase)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  IterErase)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    IterErase)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    DirectErase)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  DirectErase)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  DirectErase)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    DirectErase)
#endif

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    MakeIterator)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  MakeIterator)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  MakeIterator)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    MakeIterator)

// SinglyLinkedLists cannot iterate backwards.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    ReverseIterErase)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  ReverseIterErase)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  ReverseIterErase)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    ReverseIterErase)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    ReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  ReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  ReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    ReverseIterate)
#endif

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    Swap)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  Swap)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  Swap)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    Swap)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    RvalueOps)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  RvalueOps)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  RvalueOps)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    RvalueOps)

RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  Scope)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  Scope)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    Scope)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    TwoContainer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  TwoContainer)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  TwoContainer)
#endif
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    TwoContainer)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    ThreeContainerHelper)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  ThreeContainerHelper)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  ThreeContainerHelper)
#endif
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    ThreeContainerHelper)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    IterCopyPointer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  IterCopyPointer)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  IterCopyPointer)
#endif
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    IterCopyPointer)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    EraseIf)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  EraseIf)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  EraseIf)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    EraseIf)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    FindIf)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  FindIf)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  FindIf)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    FindIf)

//////////////////////////////////////////
// Sequence container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    PushFront)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  PushFront)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  PushFront)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    PushFront)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    PopFront)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  PopFront)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  PopFront)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    PopFront)

// Singly linked lists cannot push/pop to/from the back
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    PushBack)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  PushBack)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  PushBack)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    PushBack)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    PopBack)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  PopBack)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  PopBack)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    PopBack)
#endif

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    SeqIterate)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  SeqIterate)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  SeqIterate)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    SeqIterate)

// SinglyLinkedLists cannot iterate backwards.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    SeqReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  SeqReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  SeqReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    SeqReverseIterate)
#endif

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    EraseNext)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  EraseNext)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  EraseNext)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    EraseNext)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    InsertAfter)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  InsertAfter)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  InsertAfter)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    InsertAfter)

// SinglyLinkedLists cannot perform inserts-before operations, either with an
// iterator or with a direct object reference.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    Insert)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  Insert)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  Insert)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    Insert)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    DirectInsert)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  DirectInsert)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  DirectInsert)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    DirectInsert)
#endif

// SinglyLinkedLists cannot perform splice operations.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    Splice)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  Splice)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  Splice)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    Splice)
#endif

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    SplitAfter)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  SplitAfter)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  SplitAfter)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    SplitAfter)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    ReplaceIfCopy)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  ReplaceIfCopy)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  ReplaceIfCopy)
#endif
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    ReplaceIfCopy)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    ReplaceIfMove)
RUN_ZXTEST(SinglyLinkedListTest, UPDDTE,  ReplaceIfMove)
RUN_ZXTEST(SinglyLinkedListTest, UPCDTE,  ReplaceIfMove)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    ReplaceIfMove)
// clang-format on

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl
