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

template <typename PtrType, NodeOptions kNodeOptions = NodeOptions::None>
class DLLTraits {
 public:
  // clang-format off
    using TestObjBaseType         = TestObjBase;

    using ContainerType           = DoublyLinkedList<PtrType>;
    using ContainableBaseClass    = DoublyLinkedListable<PtrType, kNodeOptions>;
    using ContainerStateType      = DoublyLinkedListNodeState<PtrType, kNodeOptions>;

    using OtherContainerStateType = ContainerStateType;
    using OtherContainerTraits    = OtherListTraits<OtherContainerStateType>;
    using OtherContainerType      = DoublyLinkedListCustomTraits<PtrType, OtherContainerTraits>;

    struct Tag1 {};
    struct Tag2 {};
    struct Tag3 {};

    using TaggedContainableBaseClasses =
        fbl::ContainableBaseClasses<DoublyLinkedListable<PtrType, kNodeOptions, Tag1>,
                                    DoublyLinkedListable<PtrType, kNodeOptions, Tag2>,
                                    DoublyLinkedListable<PtrType, kNodeOptions, Tag3>>;

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

// clang-format off
DEFINE_TEST_OBJECTS(DLL);
using UMTE   = DEFINE_TEST_THUNK(Sequence, DLL, Unmanaged);
using UPDDTE = DEFINE_TEST_THUNK(Sequence, DLL, UniquePtrDefaultDeleter);
using UPCDTE = DEFINE_TEST_THUNK(Sequence, DLL, UniquePtrCustomDeleter);
using RPTE   = DEFINE_TEST_THUNK(Sequence, DLL, RefPtr);
VERIFY_CONTAINER_SIZES(DLL, sizeof(void*));

// Versions of the test objects which support removing an object from its
// container without needing a reference to the container itself.
template <typename PtrType>
using RFC_DLLTraits = DLLTraits<PtrType, fbl::NodeOptions::AllowRemoveFromContainer>;
DEFINE_TEST_OBJECTS(RFC_DLL);
using RFC_UMTE   = DEFINE_TEST_THUNK(Sequence, RFC_DLL, Unmanaged);
using RFC_UPDDTE = DEFINE_TEST_THUNK(Sequence, RFC_DLL, UniquePtrDefaultDeleter);
using RFC_UPCDTE = DEFINE_TEST_THUNK(Sequence, RFC_DLL, UniquePtrCustomDeleter);
using RFC_RPTE   = DEFINE_TEST_THUNK(Sequence, RFC_DLL, RefPtr);
VERIFY_CONTAINER_SIZES(RFC_DLL, sizeof(void*));

// Versions of the test objects which support clear_unsafe.
template <typename PtrType>
using CU_DLLTraits = DLLTraits<PtrType, fbl::NodeOptions::AllowClearUnsafe>;
DEFINE_TEST_OBJECTS(CU_DLL);
using CU_UMTE   = DEFINE_TEST_THUNK(Sequence, CU_DLL, Unmanaged);
using CU_UPDDTE = DEFINE_TEST_THUNK(Sequence, CU_DLL, UniquePtrDefaultDeleter);
VERIFY_CONTAINER_SIZES(CU_DLL, sizeof(void*));

//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(DoublyLinkedListTest, UMTE,    Clear)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  Clear)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  Clear)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    Clear)

#if TEST_WILL_NOT_COMPILE || 0
// Won't compile because node lacks AllowClearUnsafe option.
RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ClearUnsafe)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  ClearUnsafe)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  ClearUnsafe)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ClearUnsafe)
#endif

#if TEST_WILL_NOT_COMPILE || 0
// Won't compile because pointer type is managed.
RUN_ZXTEST(DoublyLinkedListTest, CU_UPDDTE,  ClearUnsafe)
#endif

RUN_ZXTEST(DoublyLinkedListTest, CU_UMTE, ClearUnsafe)

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

#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UMTE,    ObjRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  ObjRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  ObjRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    ObjRemoveFromContainer)
#endif

#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UMTE,    NodeRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  NodeRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  NodeRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    NodeRemoveFromContainer)
#endif

#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedListTest, UMTE,    GlobalRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  GlobalRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  GlobalRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    GlobalRemoveFromContainer)
#endif

RUN_ZXTEST(DoublyLinkedListTest, RFC_UMTE,    ObjRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, RFC_UPDDTE,  ObjRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, RFC_UPCDTE,  ObjRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, RFC_RPTE,    ObjRemoveFromContainer)

RUN_ZXTEST(DoublyLinkedListTest, RFC_UMTE,    NodeRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, RFC_UPDDTE,  NodeRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, RFC_UPCDTE,  NodeRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, RFC_RPTE,    NodeRemoveFromContainer)

RUN_ZXTEST(DoublyLinkedListTest, RFC_UMTE,    GlobalRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, RFC_UPDDTE,  GlobalRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, RFC_UPCDTE,  GlobalRemoveFromContainer)
RUN_ZXTEST(DoublyLinkedListTest, RFC_RPTE,    GlobalRemoveFromContainer)

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

RUN_ZXTEST(DoublyLinkedListTest, UMTE,    SplitAfter)
RUN_ZXTEST(DoublyLinkedListTest, UPDDTE,  SplitAfter)
RUN_ZXTEST(DoublyLinkedListTest, UPCDTE,  SplitAfter)
RUN_ZXTEST(DoublyLinkedListTest, RPTE,    SplitAfter)

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

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl
