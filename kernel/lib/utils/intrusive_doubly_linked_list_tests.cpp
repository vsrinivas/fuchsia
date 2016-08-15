// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <unittest.h>
#include <utils/intrusive_double_list.h>
#include <utils/tests/intrusive_containers/intrusive_doubly_linked_list_checker.h>
#include <utils/tests/intrusive_containers/sequence_container_test_environment.h>
#include <utils/tests/intrusive_containers/test_thunks.h>

namespace utils {
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
using UMTE = DEFINE_TEST_THUNK(Sequence, DLL, Unmanaged);
using UPTE = DEFINE_TEST_THUNK(Sequence, DLL, UniquePtr);
using RPTE = DEFINE_TEST_THUNK(Sequence, DLL, RefPtr);

UNITTEST_START_TESTCASE(double_linked_list_tests)
//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
UNITTEST("Clear (unmanaged)",             UMTE::ClearTest)
UNITTEST("Clear (unique)",                UPTE::ClearTest)
UNITTEST("Clear (RefPtr)",                RPTE::ClearTest)

UNITTEST("IsEmpty (unmanaged)",           UMTE::IsEmptyTest)
UNITTEST("IsEmpty (unique)",              UPTE::IsEmptyTest)
UNITTEST("IsEmpty (RefPtr)",              RPTE::IsEmptyTest)

UNITTEST("Iterate (unmanaged)",           UMTE::IterateTest)
UNITTEST("Iterate (unique)",              UPTE::IterateTest)
UNITTEST("Iterate (RefPtr)",              RPTE::IterateTest)

UNITTEST("IterErase (unmanaged)",         UMTE::IterEraseTest)
UNITTEST("IterErase (unique)",            UPTE::IterEraseTest)
UNITTEST("IterErase (RefPtr)",            RPTE::IterEraseTest)

UNITTEST("DirectErase (unmanaged)",       UMTE::DirectEraseTest)
UNITTEST("DirectErase (unique)",          UPTE::DirectEraseTest)
UNITTEST("DirectErase (RefPtr)",          RPTE::DirectEraseTest)

UNITTEST("MakeIterator (unmanaged)",      UMTE::MakeIteratorTest)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("MakeIterator (unique)",         UPTE::MakeIteratorTest)
#endif
UNITTEST("MakeIterator (RefPtr)",         RPTE::MakeIteratorTest)

UNITTEST("ReverseIterErase (unmanaged)",  UMTE::ReverseIterEraseTest)
UNITTEST("ReverseIterErase (unique)",     UPTE::ReverseIterEraseTest)
UNITTEST("ReverseIterErase (RefPtr)",     RPTE::ReverseIterEraseTest)

UNITTEST("ReverseIterate (unmanaged)",    UMTE::ReverseIterateTest)
UNITTEST("ReverseIterate (unique)",       UPTE::ReverseIterateTest)
UNITTEST("ReverseIterate (RefPtr)",       RPTE::ReverseIterateTest)

UNITTEST("Swap (unmanaged)",              UMTE::SwapTest)
UNITTEST("Swap (unique)",                 UPTE::SwapTest)
UNITTEST("Swap (RefPtr)",                 RPTE::SwapTest)

UNITTEST("Rvalue Ops (unmanaged)",        UMTE::RvalueOpsTest)
UNITTEST("Rvalue Ops (unique)",           UPTE::RvalueOpsTest)
UNITTEST("Rvalue Ops (RefPtr)",           RPTE::RvalueOpsTest)

UNITTEST("Scope (unique)",                UPTE::ScopeTest)
UNITTEST("Scope (RefPtr)",                RPTE::ScopeTest)

UNITTEST("TwoContainer (unmanaged)",      UMTE::TwoContainerTest)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("TwoContainer (unique)",         UPTE::TwoContainerTest)
#endif
UNITTEST("TwoContainer (RefPtr)",         RPTE::TwoContainerTest)

UNITTEST("EraseIf (unmanaged)",           UMTE::EraseIfTest)
UNITTEST("EraseIf (unique)",              UPTE::EraseIfTest)
UNITTEST("EraseIf (RefPtr)",              RPTE::EraseIfTest)

UNITTEST("FindIf (unmanaged)",            UMTE::FindIfTest)
UNITTEST("FindIf (unique)",               UPTE::FindIfTest)
UNITTEST("FindIf (RefPtr)",               RPTE::FindIfTest)

//////////////////////////////////////////
// Sequence container specific tests.
//////////////////////////////////////////
UNITTEST("PushFront (unmanaged)",         UMTE::PushFrontTest)
UNITTEST("PushFront (unique)",            UPTE::PushFrontTest)
UNITTEST("PushFront (RefPtr)",            RPTE::PushFrontTest)

UNITTEST("PopFront (unmanaged)",          UMTE::PopFrontTest)
UNITTEST("PopFront (unique)",             UPTE::PopFrontTest)
UNITTEST("PopFront (RefPtr)",             RPTE::PopFrontTest)

UNITTEST("PushBack (unmanaged)",          UMTE::PushBackTest)
UNITTEST("PushBack (unique)",             UPTE::PushBackTest)
UNITTEST("PushBack (RefPtr)",             RPTE::PushBackTest)

UNITTEST("PopBack (unmanaged)",           UMTE::PopBackTest)
UNITTEST("PopBack (unique)",              UPTE::PopBackTest)
UNITTEST("PopBack (RefPtr)",              RPTE::PopBackTest)

UNITTEST("SeqIterate (unmanaged)",        UMTE::SeqIterateTest)
UNITTEST("SeqIterate (unique)",           UPTE::SeqIterateTest)
UNITTEST("SeqIterate (RefPtr)",           RPTE::SeqIterateTest)

UNITTEST("SeqReverseIterate (unmanaged)", UMTE::SeqReverseIterateTest)
UNITTEST("SeqReverseIterate (unique)",    UPTE::SeqReverseIterateTest)
UNITTEST("SeqReverseIterate (RefPtr)",    RPTE::SeqReverseIterateTest)

UNITTEST("EraseNext (unmanaged)",         UMTE::EraseNextTest)
UNITTEST("EraseNext (unique)",            UPTE::EraseNextTest)
UNITTEST("EraseNext (RefPtr)",            RPTE::EraseNextTest)

UNITTEST("InsertAfter (unmanaged)",       UMTE::InsertAfterTest)
UNITTEST("InsertAfter (unique)",          UPTE::InsertAfterTest)
UNITTEST("InsertAfter (RefPtr)",          RPTE::InsertAfterTest)

UNITTEST("Insert (unmanaged)",            UMTE::InsertTest)
UNITTEST("Insert (unique)",               UPTE::InsertTest)
UNITTEST("Insert (RefPtr)",               RPTE::InsertTest)

UNITTEST("DirectInsert (unmanaged)",      UMTE::DirectInsertTest)
UNITTEST("DirectInsert (unique)",         UPTE::DirectInsertTest)
UNITTEST("DirectInsert (RefPtr)",         RPTE::DirectInsertTest)

UNITTEST_END_TESTCASE(double_linked_list_tests,
                      "dll",
                      "Intrusive doubly linked list tests.",
                      NULL, NULL);

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace utils

