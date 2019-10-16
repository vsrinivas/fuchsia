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

template <typename PtrType>
class SLLTraits {
 public:
  // clang-format off
    using TestObjBaseType         = TestObjBase;

    using ContainerType           = SinglyLinkedList<PtrType>;
    using ContainableBaseClass    = SinglyLinkedListable<PtrType>;
    using ContainerStateType      = SinglyLinkedListNodeState<PtrType>;

    using OtherContainerStateType = ContainerStateType;
    using OtherContainerTraits    = OtherListTraits<OtherContainerStateType>;
    using OtherContainerType      = SinglyLinkedList<PtrType, OtherContainerTraits>;

    struct Tag1 {};
    struct Tag2 {};
    struct Tag3 {};

    using TaggedContainableBaseClasses =
        fbl::ContainableBaseClasses<SinglyLinkedListable<PtrType, Tag1>,
                                    SinglyLinkedListable<PtrType, Tag2>,
                                    SinglyLinkedListable<PtrType, Tag3>>;

    using TaggedType1 = TaggedSinglyLinkedList<PtrType, Tag1>;
    using TaggedType2 = TaggedSinglyLinkedList<PtrType, Tag2>;
    using TaggedType3 = TaggedSinglyLinkedList<PtrType, Tag3>;
  // clang-format on
};

// Just a sanity check so we know our metaprogramming nonsense is
// doing what we expect:
static_assert(
    std::is_same_v<typename SLLTraits<int*>::TaggedContainableBaseClasses::TagTypes,
                   std::tuple<typename SLLTraits<int*>::Tag1, typename SLLTraits<int*>::Tag2,
                              typename SLLTraits<int*>::Tag3>>);

// clang-format off
DEFINE_TEST_OBJECTS(SLL);
using UMTE    = DEFINE_TEST_THUNK(Sequence, SLL, Unmanaged);
using UPTE    = DEFINE_TEST_THUNK(Sequence, SLL, UniquePtr);
using SUPDDTE = DEFINE_TEST_THUNK(Sequence, SLL, StdUniquePtrDefaultDeleter);
using SUPCDTE = DEFINE_TEST_THUNK(Sequence, SLL, StdUniquePtrCustomDeleter);
using RPTE    = DEFINE_TEST_THUNK(Sequence, SLL, RefPtr);

//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    Clear)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    Clear)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, Clear)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, Clear)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    Clear)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    ClearUnsafe)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    ClearUnsafe)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, ClearUnsafe)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, ClearUnsafe)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    ClearUnsafe)
#endif

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    IsEmpty)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    IsEmpty)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, IsEmpty)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, IsEmpty)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    IsEmpty)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    Iterate)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    Iterate)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, Iterate)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, Iterate)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    Iterate)

// SinglyLinkedLists cannot perform direct erase operations, nor can they erase
// using an iterator.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    IterErase)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    IterErase)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, IterErase)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, IterErase)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    IterErase)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    DirectErase)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    DirectErase)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, DirectErase)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, DirectErase)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    DirectErase)
#endif

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    MakeIterator)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    MakeIterator)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, MakeIterator)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, MakeIterator)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    MakeIterator)

// SinglyLinkedLists cannot iterate backwards.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    ReverseIterErase)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    ReverseIterErase)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, ReverseIterErase)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, ReverseIterErase)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    ReverseIterErase)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    ReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    ReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, ReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, ReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    ReverseIterate)
#endif

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    Swap)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    Swap)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, Swap)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, Swap)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    Swap)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    RvalueOps)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    RvalueOps)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, RvalueOps)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, RvalueOps)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    RvalueOps)

RUN_ZXTEST(SinglyLinkedListTest, UPTE,    Scope)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, Scope)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, Scope)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    Scope)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    TwoContainer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    TwoContainer)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, TwoContainer)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, TwoContainer)
#endif
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    TwoContainer)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    ThreeContainerHelper)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    ThreeContainerHelper)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, ThreeContainerHelper)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, ThreeContainerHelper)
#endif
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    ThreeContainerHelper)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    IterCopyPointer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    IterCopyPointer)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, IterCopyPointer)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, IterCopyPointer)
#endif
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    IterCopyPointer)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    EraseIf)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    EraseIf)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, EraseIf)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, EraseIf)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    EraseIf)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    FindIf)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    FindIf)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, FindIf)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, FindIf)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    FindIf)

//////////////////////////////////////////
// Sequence container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    PushFront)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    PushFront)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, PushFront)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, PushFront)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    PushFront)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    PopFront)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    PopFront)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, PopFront)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, PopFront)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    PopFront)

// Singly linked lists cannot push/pop to/from the back
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    PushBack)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    PushBack)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, PushBack)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, PushBack)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    PushBack)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    PopBack)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    PopBack)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, PopBack)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, PopBack)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    PopBack)
#endif

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    SeqIterate)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    SeqIterate)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, SeqIterate)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, SeqIterate)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    SeqIterate)

// SinglyLinkedLists cannot iterate backwards.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    SeqReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    SeqReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, SeqReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, SeqReverseIterate)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    SeqReverseIterate)
#endif

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    EraseNext)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    EraseNext)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, EraseNext)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, EraseNext)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    EraseNext)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    InsertAfter)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    InsertAfter)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, InsertAfter)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, InsertAfter)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    InsertAfter)

// SinglyLinkedLists cannot perform inserts-before operations, either with an
// iterator or with a direct object reference.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    Insert)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    Insert)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, Insert)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, Insert)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    Insert)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    DirectInsert)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    DirectInsert)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, DirectInsert)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, DirectInsert)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    DirectInsert)
#endif

// SinglyLinkedLists cannot perform splice operations.
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UMTE,    Splice)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    Splice)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, Splice)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, Splice)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    Splice)
#endif

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    ReplaceIfCopy)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    ReplaceIfCopy)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, ReplaceIfCopy)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, ReplaceIfCopy)
#endif
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    ReplaceIfCopy)

RUN_ZXTEST(SinglyLinkedListTest, UMTE,    ReplaceIfMove)
RUN_ZXTEST(SinglyLinkedListTest, UPTE,    ReplaceIfMove)
RUN_ZXTEST(SinglyLinkedListTest, SUPDDTE, ReplaceIfMove)
RUN_ZXTEST(SinglyLinkedListTest, SUPCDTE, ReplaceIfMove)
RUN_ZXTEST(SinglyLinkedListTest, RPTE,    ReplaceIfMove)
// clang-format on

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl
