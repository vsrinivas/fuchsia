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
        fbl::ContainableBaseClasses<TaggedDoublyLinkedListable<PtrType, Tag1>,
                                    TaggedDoublyLinkedListable<PtrType, Tag2>,
                                    TaggedDoublyLinkedListable<PtrType, Tag3>>;

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
using UMTE   = DEFINE_TEST_THUNK(Sequence, DLL, Unmanaged);
using UPDDTE = DEFINE_TEST_THUNK(Sequence, DLL, UniquePtrDefaultDeleter);
using UPCDTE = DEFINE_TEST_THUNK(Sequence, DLL, UniquePtrCustomDeleter);
using RPTE   = DEFINE_TEST_THUNK(Sequence, DLL, RefPtr);
VERIFY_CONTAINER_SIZES(DLL, sizeof(void*));

//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(DoublyLinkedListTest, UMTE,    Clear)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  Clear)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  Clear)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Clear)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ClearUnsafe)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  ClearUnsafe)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  ClearUnsafe)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ClearUnsafe)
#endif

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    IsEmpty)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  IsEmpty)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  IsEmpty)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    IsEmpty)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    Iterate)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  Iterate)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  Iterate)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Iterate)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    IterErase)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  IterErase)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  IterErase)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    IterErase)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    DirectErase)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  DirectErase)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  DirectErase)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    DirectErase)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    MakeIterator)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  MakeIterator)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  MakeIterator)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    MakeIterator)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ReverseIterErase)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  ReverseIterErase)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  ReverseIterErase)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ReverseIterErase)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  ReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  ReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ReverseIterate)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    Swap)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  Swap)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  Swap)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Swap)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    RvalueOps)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  RvalueOps)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  RvalueOps)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    RvalueOps)

RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  Scope)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  Scope)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Scope)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    TwoContainer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  TwoContainer)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  TwoContainer)
#endif
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    TwoContainer)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ThreeContainerHelper)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  ThreeContainerHelper)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  ThreeContainerHelper)
#endif
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ThreeContainerHelper)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    IterCopyPointer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  IterCopyPointer)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  IterCopyPointer)
#endif
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    IterCopyPointer)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    EraseIf)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  EraseIf)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  EraseIf)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    EraseIf)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    FindIf)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  FindIf)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  FindIf)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    FindIf)

//////////////////////////////////////////
// Sequence container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(DoublyLinkedListTest, UMTE,    PushFront)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  PushFront)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  PushFront)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    PushFront)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    PopFront)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  PopFront)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  PopFront)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    PopFront)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    PushBack)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  PushBack)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  PushBack)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    PushBack)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    PopBack)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  PopBack)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  PopBack)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    PopBack)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    SeqIterate)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  SeqIterate)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  SeqIterate)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    SeqIterate)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    SeqReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  SeqReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  SeqReverseIterate)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    SeqReverseIterate)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    EraseNext)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  EraseNext)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  EraseNext)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    EraseNext)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    InsertAfter)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  InsertAfter)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  InsertAfter)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    InsertAfter)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    Insert)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  Insert)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  Insert)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Insert)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    DirectInsert)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  DirectInsert)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  DirectInsert)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    DirectInsert)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    Splice)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  Splice)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  Splice)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Splice)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ReplaceIfCopy)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  ReplaceIfCopy)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  ReplaceIfCopy)
#endif
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ReplaceIfCopy)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ReplaceIfMove)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  ReplaceIfMove)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  ReplaceIfMove)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ReplaceIfMove)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ReplaceCopy)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  ReplaceCopy)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  ReplaceCopy)
#endif
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ReplaceCopy)

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ReplaceMove)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  ReplaceMove)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  ReplaceMove)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ReplaceMove)
// clang-format on

// TODO(50594) : Remove this when we can.
//
// Negative compilation tests which make sure that we don't accidentally
// mismatch pointer types between the node and the container.
TEST(DoublyLinkedListTest, MismatchedPointerType) {
  struct Obj {
    fbl::DoublyLinkedListNodeState<Obj*> dll_node_state_;
  };
#if TEST_WILL_NOT_COMPILE || 0
  [[maybe_unused]] fbl::DoublyLinkedList<std::unique_ptr<Obj>> list;
#endif
}

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl
