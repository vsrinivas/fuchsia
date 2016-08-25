// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <unittest.h>
#include <utils/intrusive_single_list.h>
#include <utils/tests/intrusive_containers/intrusive_singly_linked_list_checker.h>
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
class SLLTraits {
public:
    using TestObjBaseType         = TestObjBase;

    using ContainerType           = SinglyLinkedList<PtrType>;
    using ContainableBaseClass    = SinglyLinkedListable<PtrType>;
    using ContainerStateType      = SinglyLinkedListNodeState<PtrType>;

    using OtherContainerStateType = ContainerStateType;
    using OtherContainerTraits    = OtherListTraits<OtherContainerStateType>;
    using OtherContainerType      = SinglyLinkedList<PtrType, OtherContainerTraits>;
};

DEFINE_TEST_OBJECTS(SLL);
using UMTE = DEFINE_TEST_THUNK(Sequence, SLL, Unmanaged);
using UPTE = DEFINE_TEST_THUNK(Sequence, SLL, UniquePtr);
using RPTE = DEFINE_TEST_THUNK(Sequence, SLL, RefPtr);

UNITTEST_START_TESTCASE(single_linked_list_tests)
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

// SinglyLinkedLists cannot perform direct erase operations, nor can they erase
// using an iterator.
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("IterErase (unmanaged)",         UMTE::IterEraseTest)
UNITTEST("IterErase (unique)",            UPTE::IterEraseTest)
UNITTEST("IterErase (RefPtr)",            RPTE::IterEraseTest)

UNITTEST("DirectErase (unmanaged)",       UMTE::DirectEraseTest)
UNITTEST("DirectErase (unique)",          UPTE::DirectEraseTest)
UNITTEST("DirectErase (RefPtr)",          RPTE::DirectEraseTest)
#endif

UNITTEST("MakeIterator (unmanaged)",      UMTE::MakeIteratorTest)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("MakeIterator (unique)",         UPTE::MakeIteratorTest)
#endif
UNITTEST("MakeIterator (RefPtr)",         RPTE::MakeIteratorTest)

// SinglyLinkedLists cannot iterate backwards.
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("ReverseIterErase (unmanaged)",  UMTE::ReverseIterEraseTest)
UNITTEST("ReverseIterErase (unique)",     UPTE::ReverseIterEraseTest)
UNITTEST("ReverseIterErase (RefPtr)",     RPTE::ReverseIterEraseTest)

UNITTEST("ReverseIterate (unmanaged)",    UMTE::ReverseIterateTest)
UNITTEST("ReverseIterate (unique)",       UPTE::ReverseIterateTest)
UNITTEST("ReverseIterate (RefPtr)",       RPTE::ReverseIterateTest)
#endif

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

// Singly linked lists cannot push/pop to/from the back
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("PushBack (unmanaged)",          UMTE::PushBackTest)
UNITTEST("PushBack (unique)",             UPTE::PushBackTest)
UNITTEST("PushBack (RefPtr)",             RPTE::PushBackTest)

UNITTEST("PopBack (unmanaged)",           UMTE::PopBackTest)
UNITTEST("PopBack (unique)",              UPTE::PopBackTest)
UNITTEST("PopBack (RefPtr)",              RPTE::PopBackTest)
#endif

UNITTEST("SeqIterate (unmanaged)",        UMTE::SeqIterateTest)
UNITTEST("SeqIterate (unique)",           UPTE::SeqIterateTest)
UNITTEST("SeqIterate (RefPtr)",           RPTE::SeqIterateTest)

// SinglyLinkedLists cannot iterate backwards.
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("SeqReverseIterate (unmanaged)", UMTE::SeqReverseIterateTest)
UNITTEST("SeqReverseIterate (unique)",    UPTE::SeqReverseIterateTest)
UNITTEST("SeqReverseIterate (RefPtr)",    RPTE::SeqReverseIterateTest)
#endif

UNITTEST("EraseNext (unmanaged)",         UMTE::EraseNextTest)
UNITTEST("EraseNext (unique)",            UPTE::EraseNextTest)
UNITTEST("EraseNext (RefPtr)",            RPTE::EraseNextTest)

UNITTEST("InsertAfter (unmanaged)",       UMTE::InsertAfterTest)
UNITTEST("InsertAfter (unique)",          UPTE::InsertAfterTest)
UNITTEST("InsertAfter (RefPtr)",          RPTE::InsertAfterTest)

// SinglyLinkedLists cannot perform inserts-before operations, either with an
// iterator or with a direct object reference.
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("Insert (unmanaged)",            UMTE::InsertTest)
UNITTEST("Insert (unique)",               UPTE::InsertTest)
UNITTEST("Insert (RefPtr)",               RPTE::InsertTest)

UNITTEST("DirectInsert (unmanaged)",      UMTE::DirectInsertTest)
UNITTEST("DirectInsert (unique)",         UPTE::DirectInsertTest)
UNITTEST("DirectInsert (RefPtr)",         RPTE::DirectInsertTest)
#endif

UNITTEST_END_TESTCASE(single_linked_list_tests,
                      "sll",
                      "Intrusive singly linked list tests.",
                      NULL, NULL);

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace utils
