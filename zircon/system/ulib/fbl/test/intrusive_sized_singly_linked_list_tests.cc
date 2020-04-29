// Copyright 2019 The Fuchsia Authors. All rights reserved.
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

template <typename PtrType>
class SizedSLLTraits {
 public:
  // clang-format off
    using TestObjBaseType         = TestObjBase;

    using ContainerType           = SizedSinglyLinkedList<PtrType>;
    using ContainableBaseClass    = SinglyLinkedListable<PtrType>;
    using ContainerStateType      = SinglyLinkedListNodeState<PtrType>;

    using OtherContainerStateType = ContainerStateType;
    using OtherContainerTraits    = OtherListTraits<OtherContainerStateType>;
    using OtherContainerType      = SizedSinglyLinkedList<PtrType, OtherContainerTraits>;

    struct Tag1 {};
    struct Tag2 {};
    struct Tag3 {};

    using TaggedContainableBaseClasses =
        fbl::ContainableBaseClasses<TaggedSinglyLinkedListable<PtrType, Tag1>,
                                    TaggedSinglyLinkedListable<PtrType, Tag2>,
                                    TaggedSinglyLinkedListable<PtrType, Tag3>>;

    using TaggedType1 = SizedTaggedSinglyLinkedList<PtrType, Tag1>;
    using TaggedType2 = SizedTaggedSinglyLinkedList<PtrType, Tag2>;
    using TaggedType3 = SizedTaggedSinglyLinkedList<PtrType, Tag3>;
  // clang-format on
};

// Just a sanity check so we know our metaprogramming nonsense is
// doing what we expect:
static_assert(std::is_same_v<
              typename SizedSLLTraits<int*>::TaggedContainableBaseClasses::TagTypes,
              std::tuple<typename SizedSLLTraits<int*>::Tag1, typename SizedSLLTraits<int*>::Tag2,
                         typename SizedSLLTraits<int*>::Tag3>>);

// clang-format off
DEFINE_TEST_OBJECTS(SizedSLL);
using UMTE   = DEFINE_TEST_THUNK(Sequence, SizedSLL, Unmanaged);
using UPDDTE = DEFINE_TEST_THUNK(Sequence, SizedSLL, UniquePtrDefaultDeleter);
using UPCDTE = DEFINE_TEST_THUNK(Sequence, SizedSLL, UniquePtrCustomDeleter);
using RPTE   = DEFINE_TEST_THUNK(Sequence, SizedSLL, RefPtr);

//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    Clear)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  Clear)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  Clear)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    Clear)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    ClearUnsafe)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  ClearUnsafe)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  ClearUnsafe)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    ClearUnsafe)
#endif

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    IsEmpty)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  IsEmpty)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  IsEmpty)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    IsEmpty)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    Iterate)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  Iterate)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  Iterate)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    Iterate)

// SizedSinglyLinkedLists cannot perform direct erase operations, nor can they erase
// using an iterator.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    IterErase)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  IterErase)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  IterErase)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    IterErase)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    DirectErase)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  DirectErase)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  DirectErase)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    DirectErase)
#endif

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    MakeIterator)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  MakeIterator)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  MakeIterator)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    MakeIterator)

// SizedSinglyLinkedLists cannot iterate backwards.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    ReverseIterErase)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  ReverseIterErase)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  ReverseIterErase)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    ReverseIterErase)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    ReverseIterate)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  ReverseIterate)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  ReverseIterate)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    ReverseIterate)
#endif

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    Swap)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  Swap)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  Swap)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    Swap)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    RvalueOps)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  RvalueOps)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  RvalueOps)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    RvalueOps)

RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  Scope)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  Scope)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    Scope)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    TwoContainer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  TwoContainer)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  TwoContainer)
#endif
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    TwoContainer)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    ThreeContainerHelper)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  ThreeContainerHelper)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  ThreeContainerHelper)
#endif
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    ThreeContainerHelper)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    IterCopyPointer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  IterCopyPointer)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  IterCopyPointer)
#endif
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    IterCopyPointer)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    EraseIf)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  EraseIf)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  EraseIf)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    EraseIf)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    FindIf)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  FindIf)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  FindIf)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    FindIf)

//////////////////////////////////////////
// Sequence container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    PushFront)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  PushFront)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  PushFront)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    PushFront)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    PopFront)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  PopFront)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  PopFront)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    PopFront)

// SizedSingly linked lists cannot push/pop to/from the back
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    PushBack)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  PushBack)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  PushBack)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    PushBack)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    PopBack)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  PopBack)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  PopBack)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    PopBack)
#endif

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    SeqIterate)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  SeqIterate)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  SeqIterate)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    SeqIterate)

// SizedSinglyLinkedLists cannot iterate backwards.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    SeqReverseIterate)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  SeqReverseIterate)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  SeqReverseIterate)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    SeqReverseIterate)
#endif

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    EraseNext)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  EraseNext)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  EraseNext)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    EraseNext)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    InsertAfter)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  InsertAfter)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  InsertAfter)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    InsertAfter)

// SizedSinglyLinkedLists cannot perform inserts-before operations, either with an
// iterator or with a direct object reference.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    Insert)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  Insert)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  Insert)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    Insert)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    DirectInsert)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  DirectInsert)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  DirectInsert)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    DirectInsert)
#endif

// SizedSinglyLinkedLists cannot perform splice operations.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    Splice)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  Splice)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  Splice)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    Splice)
#endif

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    ReplaceIfCopy)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  ReplaceIfCopy)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  ReplaceIfCopy)
#endif
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    ReplaceIfCopy)

RUN_ZXTEST(SizedSinglyLinkedListTest, UMTE,    ReplaceIfMove)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPDDTE,  ReplaceIfMove)
RUN_ZXTEST(SizedSinglyLinkedListTest, UPCDTE,  ReplaceIfMove)
RUN_ZXTEST(SizedSinglyLinkedListTest, RPTE,    ReplaceIfMove)
// clang-format on

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl
