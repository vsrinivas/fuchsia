// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/intrusive_double_list.h>
#include <fbl/tests/intrusive_containers/intrusive_doubly_linked_list_checker.h>
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
class SizedDLLTraits {
 public:
  // clang-format off
    using TestObjBaseType         = TestObjBase;

    using ContainerType           = SizedDoublyLinkedList<PtrType>;
    using ContainableBaseClass    = DoublyLinkedListable<PtrType, kNodeOptions>;
    using ContainerStateType      = DoublyLinkedListNodeState<PtrType, kNodeOptions>;

    using OtherContainerStateType = ContainerStateType;
    using OtherContainerTraits    = OtherListTraits<OtherContainerStateType>;
    using OtherContainerType      = SizedDoublyLinkedList<PtrType, DefaultObjectTag,
                                                          OtherContainerTraits>;

    struct Tag1 {};
    struct Tag2 {};
    struct Tag3 {};

    using TaggedContainableBaseClasses =
        fbl::ContainableBaseClasses<TaggedDoublyLinkedListable<PtrType, Tag1>,
                                    TaggedDoublyLinkedListable<PtrType, Tag2>,
                                    TaggedDoublyLinkedListable<PtrType, Tag3>>;

    using TaggedType1 = DoublyLinkedList<PtrType, Tag1, SizeOrder::Constant>;
    using TaggedType2 = DoublyLinkedList<PtrType, Tag2, SizeOrder::Constant>;
    using TaggedType3 = DoublyLinkedList<PtrType, Tag3, SizeOrder::Constant>;
  // clang-format on
};

// Just a sanity check so we know our metaprogramming nonsense is
// doing what we expect:
static_assert(std::is_same_v<
              typename SizedDLLTraits<int*>::TaggedContainableBaseClasses::TagTypes,
              std::tuple<typename SizedDLLTraits<int*>::Tag1, typename SizedDLLTraits<int*>::Tag2,
                         typename SizedDLLTraits<int*>::Tag3>>);

// Negative compilation test which make sure that we cannot try to use a node
// flagged with AllowRemoveFromContainer with a sized list.
TEST(SizedDoublyLinkedListTest, NoRemoveFromContainer) {
  struct Obj : public DoublyLinkedListable<Obj*, NodeOptions::AllowRemoveFromContainer> {};
#if TEST_WILL_NOT_COMPILE || 0
  [[maybe_unused]] fbl::SizedDoublyLinkedList<Obj*> list;
#endif
}

// clang-format off
DEFINE_TEST_OBJECTS(SizedDLL);
using UMTE   = DEFINE_TEST_THUNK(Sequence, SizedDLL, Unmanaged);
using UPDDTE = DEFINE_TEST_THUNK(Sequence, SizedDLL, UniquePtrDefaultDeleter);
using UPCDTE = DEFINE_TEST_THUNK(Sequence, SizedDLL, UniquePtrCustomDeleter);
using RPTE   = DEFINE_TEST_THUNK(Sequence, SizedDLL, RefPtr);

// Versions of the test objects which support clear_unsafe.
template <typename PtrType>
using CU_SizedDLLTraits = SizedDLLTraits<PtrType, fbl::NodeOptions::AllowClearUnsafe>;
DEFINE_TEST_OBJECTS(CU_SizedDLL);
using CU_UMTE   = DEFINE_TEST_THUNK(Sequence, CU_SizedDLL, Unmanaged);
using CU_UPDDTE = DEFINE_TEST_THUNK(Sequence, CU_SizedDLL, UniquePtrDefaultDeleter);

//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    Clear)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  Clear)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  Clear)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    Clear)

#if TEST_WILL_NOT_COMPILE || 0
// Won't compile because node lacks AllowClearUnsafe option.
RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    ClearUnsafe)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  ClearUnsafe)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  ClearUnsafe)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    ClearUnsafe)
#endif

#if TEST_WILL_NOT_COMPILE || 0
// Won't compile because pointer type is managed.
RUN_ZXTEST(SizedDoublyLinkedListTest, CU_UPDDTE,  ClearUnsafe)
#endif

RUN_ZXTEST(SizedDoublyLinkedListTest, CU_UMTE, ClearUnsafe)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    IsEmpty)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  IsEmpty)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  IsEmpty)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    IsEmpty)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    Iterate)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  Iterate)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  Iterate)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    Iterate)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    IterErase)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  IterErase)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  IterErase)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    IterErase)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    DirectErase)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  DirectErase)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  DirectErase)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    DirectErase)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    MakeIterator)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  MakeIterator)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  MakeIterator)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    MakeIterator)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    ReverseIterErase)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  ReverseIterErase)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  ReverseIterErase)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    ReverseIterErase)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    ReverseIterate)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  ReverseIterate)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  ReverseIterate)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    ReverseIterate)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    Swap)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  Swap)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  Swap)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    Swap)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    RvalueOps)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  RvalueOps)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  RvalueOps)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    RvalueOps)

RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  Scope)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  Scope)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    Scope)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    TwoContainer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  TwoContainer)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  TwoContainer)
#endif
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    TwoContainer)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    ThreeContainerHelper)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  ThreeContainerHelper)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  ThreeContainerHelper)
#endif
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    ThreeContainerHelper)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    IterCopyPointer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  IterCopyPointer)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  IterCopyPointer)
#endif
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    IterCopyPointer)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    EraseIf)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  EraseIf)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  EraseIf)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    EraseIf)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    FindIf)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  FindIf)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  FindIf)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    FindIf)

//////////////////////////////////////////
// Sequence container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    PushFront)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  PushFront)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  PushFront)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    PushFront)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    PopFront)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  PopFront)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  PopFront)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    PopFront)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    PushBack)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  PushBack)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  PushBack)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    PushBack)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    PopBack)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  PopBack)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  PopBack)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    PopBack)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    SeqIterate)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  SeqIterate)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  SeqIterate)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    SeqIterate)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    SeqReverseIterate)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  SeqReverseIterate)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  SeqReverseIterate)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    SeqReverseIterate)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    EraseNext)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  EraseNext)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  EraseNext)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    EraseNext)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    InsertAfter)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  InsertAfter)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  InsertAfter)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    InsertAfter)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    Insert)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  Insert)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  Insert)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    Insert)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    DirectInsert)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  DirectInsert)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  DirectInsert)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    DirectInsert)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    Splice)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  Splice)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  Splice)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    Splice)

#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    SplitAfter)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  SplitAfter)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  SplitAfter)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    SplitAfter)
#endif

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    ReplaceIfCopy)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  ReplaceIfCopy)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  ReplaceIfCopy)
#endif
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    ReplaceIfCopy)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    ReplaceIfMove)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  ReplaceIfMove)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  ReplaceIfMove)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    ReplaceIfMove)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    ReplaceCopy)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  ReplaceCopy)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  ReplaceCopy)
#endif
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    ReplaceCopy)

RUN_ZXTEST(SizedDoublyLinkedListTest, UMTE,    ReplaceMove)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPDDTE,  ReplaceMove)
RUN_ZXTEST(SizedDoublyLinkedListTest, UPCDTE,  ReplaceMove)
RUN_ZXTEST(SizedDoublyLinkedListTest, RPTE,    ReplaceMove)
// clang-format on

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl
