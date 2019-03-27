// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/tests/intrusive_containers/intrusive_doubly_linked_list_checker.h>
#include <fbl/tests/intrusive_containers/sequence_container_test_environment.h>
#include <fbl/tests/intrusive_containers/test_thunks.h>

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
    using TestObjBaseType         = TestObjBase;

    using ContainerType           = DoublyLinkedList<PtrType>;
    using ContainableBaseClass    = DoublyLinkedListable<PtrType>;
    using ContainerStateType      = DoublyLinkedListNodeState<PtrType>;

    using OtherContainerStateType = ContainerStateType;
    using OtherContainerTraits    = OtherListTraits<OtherContainerStateType>;
    using OtherContainerType      = DoublyLinkedList<PtrType, OtherContainerTraits>;
};

DEFINE_TEST_OBJECTS(DLL);
using UMTE    = DEFINE_TEST_THUNK(Sequence, DLL, Unmanaged);
using UPTE    = DEFINE_TEST_THUNK(Sequence, DLL, UniquePtr);
using SUPDDTE = DEFINE_TEST_THUNK(Sequence, DLL, StdUniquePtrDefaultDeleter);
using SUPCDTE = DEFINE_TEST_THUNK(Sequence, DLL, StdUniquePtrCustomDeleter);
using RPTE    = DEFINE_TEST_THUNK(Sequence, DLL, RefPtr);

BEGIN_TEST_CASE(double_linked_list_tests)
//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
RUN_NAMED_TEST("Clear (unmanaged)",                 UMTE::ClearTest)
RUN_NAMED_TEST("Clear (unique)",                    UPTE::ClearTest)
RUN_NAMED_TEST("Clear (std::uptr)",                 SUPDDTE::ClearTest)
RUN_NAMED_TEST("Clear (std::uptr<Del>)",            SUPCDTE::ClearTest)
RUN_NAMED_TEST("Clear (RefPtr)",                    RPTE::ClearTest)

RUN_NAMED_TEST("ClearUnsafe (unmanaged)",           UMTE::ClearUnsafeTest)
#if TEST_WILL_NOT_COMPILE || 0
RUN_NAMED_TEST("ClearUnsafe (unique)",              UPTE::ClearUnsafeTest)
RUN_NAMED_TEST("ClearUnsafe (std::uptr)",           SUPDDTE::ClearUnsafeTest)
RUN_NAMED_TEST("ClearUnsafe (std::uptr<Del>)",      SUPCDTE::ClearUnsafeTest)
RUN_NAMED_TEST("ClearUnsafe (RefPtr)",              RPTE::ClearUnsafeTest)
#endif

RUN_NAMED_TEST("IsEmpty (unmanaged)",               UMTE::IsEmptyTest)
RUN_NAMED_TEST("IsEmpty (unique)",                  UPTE::IsEmptyTest)
RUN_NAMED_TEST("IsEmpty (std::uptr)",               SUPDDTE::IsEmptyTest)
RUN_NAMED_TEST("IsEmpty (std::uptr<Del>)",          SUPCDTE::IsEmptyTest)
RUN_NAMED_TEST("IsEmpty (RefPtr)",                  RPTE::IsEmptyTest)

RUN_NAMED_TEST("Iterate (unmanaged)",               UMTE::IterateTest)
RUN_NAMED_TEST("Iterate (unique)",                  UPTE::IterateTest)
RUN_NAMED_TEST("Iterate (std::uptr)",               SUPDDTE::IterateTest)
RUN_NAMED_TEST("Iterate (std::uptr<Del>)",          SUPCDTE::IterateTest)
RUN_NAMED_TEST("Iterate (RefPtr)",                  RPTE::IterateTest)

RUN_NAMED_TEST("IterErase (unmanaged)",             UMTE::IterEraseTest)
RUN_NAMED_TEST("IterErase (unique)",                UPTE::IterEraseTest)
RUN_NAMED_TEST("IterErase (std::uptr)",             SUPDDTE::IterEraseTest)
RUN_NAMED_TEST("IterErase (std::uptr<Del>)",        SUPCDTE::IterEraseTest)
RUN_NAMED_TEST("IterErase (RefPtr)",                RPTE::IterEraseTest)

RUN_NAMED_TEST("DirectErase (unmanaged)",           UMTE::DirectEraseTest)
RUN_NAMED_TEST("DirectErase (unique)",              UPTE::DirectEraseTest)
RUN_NAMED_TEST("DirectErase (std::uptr)",           SUPDDTE::DirectEraseTest)
RUN_NAMED_TEST("DirectErase (std::uptr<Del>)",      SUPCDTE::DirectEraseTest)
RUN_NAMED_TEST("DirectErase (RefPtr)",              RPTE::DirectEraseTest)

RUN_NAMED_TEST("MakeIterator (unmanaged)",          UMTE::MakeIteratorTest)
#if TEST_WILL_NOT_COMPILE || 0
RUN_NAMED_TEST("MakeIterator (unique)",             UPTE::MakeIteratorTest)
RUN_NAMED_TEST("MakeIterator (std::uptr)",          SUPDDTE::MakeIteratorTest)
RUN_NAMED_TEST("MakeIterator (std::uptr<Del>)",     SUPCDTE::MakeIteratorTest)
#endif
RUN_NAMED_TEST("MakeIterator (RefPtr)",             RPTE::MakeIteratorTest)

RUN_NAMED_TEST("ReverseIterErase (unmanaged)",      UMTE::ReverseIterEraseTest)
RUN_NAMED_TEST("ReverseIterErase (unique)",         UPTE::ReverseIterEraseTest)
RUN_NAMED_TEST("ReverseIterErase (std::uptr)",      SUPDDTE::ReverseIterEraseTest)
RUN_NAMED_TEST("ReverseIterErase (std::uptr<Del>)", SUPCDTE::ReverseIterEraseTest)
RUN_NAMED_TEST("ReverseIterErase (RefPtr)",         RPTE::ReverseIterEraseTest)

RUN_NAMED_TEST("ReverseIterate (unmanaged)",        UMTE::ReverseIterateTest)
RUN_NAMED_TEST("ReverseIterate (unique)",           UPTE::ReverseIterateTest)
RUN_NAMED_TEST("ReverseIterate (std::uptr)",        SUPDDTE::ReverseIterateTest)
RUN_NAMED_TEST("ReverseIterate (std::uptr<Del>)",   SUPCDTE::ReverseIterateTest)
RUN_NAMED_TEST("ReverseIterate (RefPtr)",           RPTE::ReverseIterateTest)

RUN_NAMED_TEST("Swap (unmanaged)",                  UMTE::SwapTest)
RUN_NAMED_TEST("Swap (unique)",                     UPTE::SwapTest)
RUN_NAMED_TEST("Swap (std::uptr)",                  SUPDDTE::SwapTest)
RUN_NAMED_TEST("Swap (std::uptr<Del>)",             SUPCDTE::SwapTest)
RUN_NAMED_TEST("Swap (RefPtr)",                     RPTE::SwapTest)

RUN_NAMED_TEST("Rvalue Ops (unmanaged)",            UMTE::RvalueOpsTest)
RUN_NAMED_TEST("Rvalue Ops (unique)",               UPTE::RvalueOpsTest)
RUN_NAMED_TEST("Rvalue Ops (std::uptr)",            SUPDDTE::RvalueOpsTest)
RUN_NAMED_TEST("Rvalue Ops (std::uptr<Del>)",       SUPCDTE::RvalueOpsTest)
RUN_NAMED_TEST("Rvalue Ops (RefPtr)",               RPTE::RvalueOpsTest)

RUN_NAMED_TEST("Scope (unique)",                    UPTE::ScopeTest)
RUN_NAMED_TEST("Scope (std::uptr)",                 SUPDDTE::ScopeTest)
RUN_NAMED_TEST("Scope (std::uptr<Del>)",            SUPCDTE::ScopeTest)
RUN_NAMED_TEST("Scope (RefPtr)",                    RPTE::ScopeTest)

RUN_NAMED_TEST("TwoContainer (unmanaged)",          UMTE::TwoContainerTest)
#if TEST_WILL_NOT_COMPILE || 0
RUN_NAMED_TEST("TwoContainer (unique)",             UPTE::TwoContainerTest)
RUN_NAMED_TEST("TwoContainer (std::uptr)",          SUPDDTE::TwoContainerTest)
RUN_NAMED_TEST("TwoContainer (std::uptr<Del>)",     SUPCDTE::TwoContainerTest)
#endif
RUN_NAMED_TEST("TwoContainer (RefPtr)",             RPTE::TwoContainerTest)

RUN_NAMED_TEST("IterCopyPointer (unmanaged)",       UMTE::IterCopyPointerTest)
#if TEST_WILL_NOT_COMPILE || 0
RUN_NAMED_TEST("IterCopyPointer (unique)",          UPTE::IterCopyPointerTest)
RUN_NAMED_TEST("IterCopyPointer (std::uptr)",       SUPDDTE::IterCopyPointerTest)
RUN_NAMED_TEST("IterCopyPointer (std::uptr<Del>)",  SUPCDTE::IterCopyPointerTest)
#endif
RUN_NAMED_TEST("IterCopyPointer (RefPtr)",          RPTE::IterCopyPointerTest)

RUN_NAMED_TEST("EraseIf (unmanaged)",               UMTE::EraseIfTest)
RUN_NAMED_TEST("EraseIf (unique)",                  UPTE::EraseIfTest)
RUN_NAMED_TEST("EraseIf (std::uptr)",               SUPDDTE::EraseIfTest)
RUN_NAMED_TEST("EraseIf (std::uptr<Del>)",          SUPCDTE::EraseIfTest)
RUN_NAMED_TEST("EraseIf (RefPtr)",                  RPTE::EraseIfTest)

RUN_NAMED_TEST("FindIf (unmanaged)",                UMTE::FindIfTest)
RUN_NAMED_TEST("FindIf (unique)",                   UPTE::FindIfTest)
RUN_NAMED_TEST("FindIf (std::uptr)",                SUPDDTE::FindIfTest)
RUN_NAMED_TEST("FindIf (std::uptr<Del>)",           SUPCDTE::FindIfTest)
RUN_NAMED_TEST("FindIf (RefPtr)",                   RPTE::FindIfTest)

//////////////////////////////////////////
// Sequence container specific tests.
//////////////////////////////////////////
RUN_NAMED_TEST("PushFront (unmanaged)",             UMTE::PushFrontTest)
RUN_NAMED_TEST("PushFront (unique)",                UPTE::PushFrontTest)
RUN_NAMED_TEST("PushFront (std::uptr)",             SUPDDTE::PushFrontTest)
RUN_NAMED_TEST("PushFront (std::uptr<Del>)",        SUPCDTE::PushFrontTest)
RUN_NAMED_TEST("PushFront (RefPtr)",                RPTE::PushFrontTest)

RUN_NAMED_TEST("PopFront (unmanaged)",              UMTE::PopFrontTest)
RUN_NAMED_TEST("PopFront (unique)",                 UPTE::PopFrontTest)
RUN_NAMED_TEST("PopFront (std::uptr)",              SUPDDTE::PopFrontTest)
RUN_NAMED_TEST("PopFront (std::uptr<Del>)",         SUPCDTE::PopFrontTest)
RUN_NAMED_TEST("PopFront (RefPtr)",                 RPTE::PopFrontTest)

RUN_NAMED_TEST("PushBack (unmanaged)",              UMTE::PushBackTest)
RUN_NAMED_TEST("PushBack (unique)",                 UPTE::PushBackTest)
RUN_NAMED_TEST("PushBack (std::uptr)",              SUPDDTE::PushBackTest)
RUN_NAMED_TEST("PushBack (std::uptr<Del>)",         SUPCDTE::PushBackTest)
RUN_NAMED_TEST("PushBack (RefPtr)",                 RPTE::PushBackTest)

RUN_NAMED_TEST("PopBack (unmanaged)",               UMTE::PopBackTest)
RUN_NAMED_TEST("PopBack (unique)",                  UPTE::PopBackTest)
RUN_NAMED_TEST("PopBack (std::uptr)",               SUPDDTE::PopBackTest)
RUN_NAMED_TEST("PopBack (std::uptr<Del>)",          SUPCDTE::PopBackTest)
RUN_NAMED_TEST("PopBack (RefPtr)",                  RPTE::PopBackTest)

RUN_NAMED_TEST("SeqIterate (unmanaged)",            UMTE::SeqIterateTest)
RUN_NAMED_TEST("SeqIterate (unique)",               UPTE::SeqIterateTest)
RUN_NAMED_TEST("SeqIterate (std::uptr)",            SUPDDTE::SeqIterateTest)
RUN_NAMED_TEST("SeqIterate (std::uptr<Del>)",       SUPCDTE::SeqIterateTest)
RUN_NAMED_TEST("SeqIterate (RefPtr)",               RPTE::SeqIterateTest)

RUN_NAMED_TEST("SeqReverseIterate (unmanaged)",     UMTE::SeqReverseIterateTest)
RUN_NAMED_TEST("SeqReverseIterate (unique)",        UPTE::SeqReverseIterateTest)
RUN_NAMED_TEST("SeqReverseIterate (std::uptr)",     SUPDDTE::SeqReverseIterateTest)
RUN_NAMED_TEST("SeqReverseIterate (std::uptr<Del>)",SUPCDTE::SeqReverseIterateTest)
RUN_NAMED_TEST("SeqReverseIterate (RefPtr)",        RPTE::SeqReverseIterateTest)

RUN_NAMED_TEST("EraseNext (unmanaged)",             UMTE::EraseNextTest)
RUN_NAMED_TEST("EraseNext (unique)",                UPTE::EraseNextTest)
RUN_NAMED_TEST("EraseNext (std::uptr)",             SUPDDTE::EraseNextTest)
RUN_NAMED_TEST("EraseNext (std::uptr<Del>)",        SUPCDTE::EraseNextTest)
RUN_NAMED_TEST("EraseNext (RefPtr)",                RPTE::EraseNextTest)

RUN_NAMED_TEST("InsertAfter (unmanaged)",           UMTE::InsertAfterTest)
RUN_NAMED_TEST("InsertAfter (unique)",              UPTE::InsertAfterTest)
RUN_NAMED_TEST("InsertAfter (std::uptr)",           SUPDDTE::InsertAfterTest)
RUN_NAMED_TEST("InsertAfter (std::uptr<Del>)",      SUPCDTE::InsertAfterTest)
RUN_NAMED_TEST("InsertAfter (RefPtr)",              RPTE::InsertAfterTest)

RUN_NAMED_TEST("Insert (unmanaged)",                UMTE::InsertTest)
RUN_NAMED_TEST("Insert (unique)",                   UPTE::InsertTest)
RUN_NAMED_TEST("Insert (std::uptr)",                SUPDDTE::InsertTest)
RUN_NAMED_TEST("Insert (std::uptr<Del>)",           SUPCDTE::InsertTest)
RUN_NAMED_TEST("Insert (RefPtr)",                   RPTE::InsertTest)

RUN_NAMED_TEST("DirectInsert (unmanaged)",          UMTE::DirectInsertTest)
RUN_NAMED_TEST("DirectInsert (unique)",             UPTE::DirectInsertTest)
RUN_NAMED_TEST("DirectInsert (std::uptr)",          SUPDDTE::DirectInsertTest)
RUN_NAMED_TEST("DirectInsert (std::uptr<Del>)",     SUPCDTE::DirectInsertTest)
RUN_NAMED_TEST("DirectInsert (RefPtr)",             RPTE::DirectInsertTest)

RUN_NAMED_TEST("Splice (unmanaged)",                UMTE::SpliceTest)
RUN_NAMED_TEST("Splice (unique)",                   UPTE::SpliceTest)
RUN_NAMED_TEST("Splice (std::uptr)",                SUPDDTE::SpliceTest)
RUN_NAMED_TEST("Splice (std::uptr<Del>)",           SUPCDTE::SpliceTest)
RUN_NAMED_TEST("Splice (RefPtr)",                   RPTE::SpliceTest)

RUN_NAMED_TEST("ReplaceIfCopy (unmanaged)",         UMTE::ReplaceIfCopyTest)
#if TEST_WILL_NOT_COMPILE || 0
RUN_NAMED_TEST("ReplaceIfCopy (unique)",            UPTE::ReplaceIfCopyTest)
RUN_NAMED_TEST("ReplaceIfCopy (std::uptr)",         SUPDDTE::ReplaceIfCopyTest)
RUN_NAMED_TEST("ReplaceIfCopy (std::uptr<Del>)",    SUPCDTE::ReplaceIfCopyTest)
#endif
RUN_NAMED_TEST("ReplaceIfCopy (RefPtr)",            RPTE::ReplaceIfCopyTest)

RUN_NAMED_TEST("ReplaceIfMove (unmanaged)",         UMTE::ReplaceIfMoveTest)
RUN_NAMED_TEST("ReplaceIfMove (unique)",            UPTE::ReplaceIfMoveTest)
RUN_NAMED_TEST("ReplaceIfMove (std::uptr)",         SUPDDTE::ReplaceIfMoveTest)
RUN_NAMED_TEST("ReplaceIfMove (std::uptr<Del>)",    SUPCDTE::ReplaceIfMoveTest)
RUN_NAMED_TEST("ReplaceIfMove (RefPtr)",            RPTE::ReplaceIfMoveTest)

RUN_NAMED_TEST("ReplaceCopy (unmanaged)",           UMTE::ReplaceCopyTest)
#if TEST_WILL_NOT_COMPILE || 0
RUN_NAMED_TEST("ReplaceCopy (unique)",              UPTE::ReplaceCopyTest)
RUN_NAMED_TEST("ReplaceCopy (std::uptr)",           SUPDDTE::ReplaceCopyTest)
RUN_NAMED_TEST("ReplaceCopy (std::uptr<Del>)",      SUPCDTE::ReplaceCopyTest)
#endif
RUN_NAMED_TEST("ReplaceCopy (RefPtr)",              RPTE::ReplaceCopyTest)

RUN_NAMED_TEST("ReplaceMove (unmanaged)",           UMTE::ReplaceMoveTest)
RUN_NAMED_TEST("ReplaceMove (unique)",              UPTE::ReplaceMoveTest)
RUN_NAMED_TEST("ReplaceMove (std::uptr)",           SUPDDTE::ReplaceMoveTest)
RUN_NAMED_TEST("ReplaceMove (std::uptr<Del>)",      SUPCDTE::ReplaceMoveTest)
RUN_NAMED_TEST("ReplaceMove (RefPtr)",              RPTE::ReplaceMoveTest)

END_TEST_CASE(double_linked_list_tests)

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl

