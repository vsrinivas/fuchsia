// Copyright 2016 The Fuchsia Authors. All rights reserved.
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

template <typename PtrType>
class DLLTraits {
 public:
  // clang-format off
    using TestObjBaseType         = TestObjBase;

    using ContainerType           = DoublyLinkedList<PtrType>;
    using ContainableBaseClass    = DoublyLinkedListable<PtrType>;
    using ContainerStateType      = DoublyLinkedListNodeState<PtrType>;

    using OtherContainerStateType = ContainerStateType;
    using OtherContainerTraits    = OtherListTraits<OtherContainerStateType>;
    using OtherContainerType      = DoublyLinkedList<PtrType, OtherContainerTraits>;

    struct Tag1 {};
    struct Tag2 {};
    struct Tag3 {};

    using TaggedContainableBaseClasses =
        fbl::ContainableBaseClasses<DoublyLinkedListable<PtrType, Tag1>,
                                    DoublyLinkedListable<PtrType, Tag2>,
                                    DoublyLinkedListable<PtrType, Tag3>>;

    using TaggedType1 = TaggedDoublyLinkedList<PtrType, Tag1>;
    using TaggedType2 = TaggedDoublyLinkedList<PtrType, Tag2>;
    using TaggedType3 = TaggedDoublyLinkedList<PtrType, Tag3>;
  // clang-format on
};

// Just a sanity check so we know our metaprogramming nonsense is
// doing what we expect:
static_assert(
    std::is_same_v<typename DLLTraits<int*>::TaggedContainableBaseClasses::TagTypes,
                   std::tuple<typename DLLTraits<int*>::Tag1, typename DLLTraits<int*>::Tag2,
                              typename DLLTraits<int*>::Tag3>>);

// clang-format off
DEFINE_TEST_OBJECTS(DLL);
using UMTE    = DEFINE_TEST_THUNK(Sequence, DLL, Unmanaged);
using UPTE    = DEFINE_TEST_THUNK(Sequence, DLL, UniquePtr);
using SUPDDTE = DEFINE_TEST_THUNK(Sequence, DLL, StdUniquePtrDefaultDeleter);
using SUPCDTE = DEFINE_TEST_THUNK(Sequence, DLL, StdUniquePtrCustomDeleter);
using RPTE    = DEFINE_TEST_THUNK(Sequence, DLL, RefPtr);

//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(DoublyLinkedListTest, UMTE,    Clear)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    Clear)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, Clear)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, Clear)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Clear)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ClearUnsafe)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    ClearUnsafe)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, ClearUnsafe)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, ClearUnsafe)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ClearUnsafe)
#endif

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    IsEmpty)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    IsEmpty)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, IsEmpty)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, IsEmpty)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    IsEmpty)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    Iterate)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    Iterate)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, Iterate)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, Iterate)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Iterate)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    IterErase)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    IterErase)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, IterErase)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, IterErase)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    IterErase)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    DirectErase)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    DirectErase)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, DirectErase)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, DirectErase)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    DirectErase)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    MakeIterator)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    MakeIterator)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, MakeIterator)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, MakeIterator)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    MakeIterator)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ReverseIterErase)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    ReverseIterErase)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, ReverseIterErase)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, ReverseIterErase)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ReverseIterErase)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    ReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, ReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, ReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ReverseIterate)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    Swap)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    Swap)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, Swap)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, Swap)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Swap)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    RvalueOps)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    RvalueOps)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, RvalueOps)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, RvalueOps)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    RvalueOps)

RUN_ZXTEST(DoublyLinkedListTest, UPTE,    Scope)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, Scope)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, Scope)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Scope)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    TwoContainer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    TwoContainer)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, TwoContainer)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, TwoContainer)
#endif
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    TwoContainer)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ThreeContainerHelper)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    ThreeContainerHelper)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, ThreeContainerHelper)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, ThreeContainerHelper)
#endif
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ThreeContainerHelper)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    IterCopyPointer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    IterCopyPointer)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, IterCopyPointer)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, IterCopyPointer)
#endif
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    IterCopyPointer)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    EraseIf)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    EraseIf)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, EraseIf)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, EraseIf)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    EraseIf)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    FindIf)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    FindIf)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, FindIf)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, FindIf)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    FindIf)

//////////////////////////////////////////
// Sequence container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(DoublyLinkedListTest, UMTE,    PushFront)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    PushFront)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, PushFront)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, PushFront)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    PushFront)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    PopFront)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    PopFront)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, PopFront)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, PopFront)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    PopFront)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    PushBack)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    PushBack)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, PushBack)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, PushBack)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    PushBack)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    PopBack)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    PopBack)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, PopBack)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, PopBack)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    PopBack)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    SeqIterate)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    SeqIterate)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, SeqIterate)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, SeqIterate)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    SeqIterate)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    SeqReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    SeqReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, SeqReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, SeqReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    SeqReverseIterate)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    EraseNext)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    EraseNext)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, EraseNext)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, EraseNext)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    EraseNext)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    InsertAfter)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    InsertAfter)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, InsertAfter)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, InsertAfter)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    InsertAfter)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    Insert)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    Insert)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, Insert)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, Insert)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Insert)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    DirectInsert)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    DirectInsert)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, DirectInsert)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, DirectInsert)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    DirectInsert)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    Splice)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    Splice)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, Splice)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, Splice)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Splice)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ReplaceIfCopy)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    ReplaceIfCopy)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, ReplaceIfCopy)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, ReplaceIfCopy)
#endif
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ReplaceIfCopy)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ReplaceIfMove)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    ReplaceIfMove)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, ReplaceIfMove)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, ReplaceIfMove)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ReplaceIfMove)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ReplaceCopy)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    ReplaceCopy)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, ReplaceCopy)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, ReplaceCopy)
#endif
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ReplaceCopy)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ReplaceMove)
RUN_ZXTEST(DoublyLinkedListTest, UPTE,    ReplaceMove)
RUN_ZXTEST(DoublyLinkedListTest, SUPDDTE, ReplaceMove)
RUN_ZXTEST(DoublyLinkedListTest, SUPCDTE, ReplaceMove)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ReplaceMove)
// clang-format on

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl
