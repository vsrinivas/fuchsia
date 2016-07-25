// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <unittest.h>
#include <utils/intrusive_double_list.h>
#include <utils/newcode_hash_table.h>
#include <utils/tests/intrusive_containers/associative_container_test_environment.h>
#include <utils/tests/intrusive_containers/test_thunks.h>

namespace utils {
namespace tests {
namespace intrusive_containers {

template <typename ContainerStateType>
struct OtherHashTraits {
    using HashType        = size_t;
    using KeyType         = typename ContainerStateType::KeyType;
    using BucketStateType = typename ContainerStateType::BucketStateType;
    using PtrTraits       = typename BucketStateType::PtrTraits;
    using PtrType         = typename PtrTraits::PtrType;

    static constexpr HashType kNumBuckets = 23;

    static BucketStateType& node_state(typename PtrTraits::RefType obj) {
        return obj.other_container_state_.bucket_state_;
    }

    static KeyType GetKey(typename PtrTraits::ConstRefType obj) {
        return obj.other_container_state_.key_;
    }

    static HashType GetHash(const KeyType& key) {
        return (static_cast<HashType>(key) * 0xaee58187) % kNumBuckets;
    }

    // Set key is a trait which is only used by the tests, not by the containers
    // themselves.
    static void SetKey(typename PtrTraits::RefType obj, KeyType key) {
        obj.other_container_state_.key_ = key;
    }
};

template <typename _KeyType, typename _BucketStateType>
struct OtherHashState {
    using KeyType         = _KeyType;
    using BucketStateType = _BucketStateType;

private:
    friend struct OtherHashTraits<OtherHashState<KeyType, BucketStateType>>;

    KeyType         key_;
    BucketStateType bucket_state_;
};

template <typename PtrType>
class HTDLLTraits {
public:
    using KeyType                 = size_t;
    using HashTraits              = newcode::DefaultHashTraits<KeyType, PtrType>;
    using HashType                = typename HashTraits::HashType;
    using TestObjBaseType         = HashedTestObjBase<HashTraits>;

    using ContainerType           = newcode::HashTable<
                                        newcode::DefaultHashTraits<KeyType, PtrType>,
                                        DoublyLinkedList<PtrType> >;
    using ContainableBaseClass    = DoublyLinkedListable<PtrType>;
    using ContainerStateType      = DoublyLinkedListNodeState<PtrType>;

    using OtherContainerStateType = OtherHashState<KeyType, DoublyLinkedListNodeState<PtrType>>;
    using OtherContainerTraits    = OtherHashTraits<OtherContainerStateType>;
    using OtherBucketType         = DoublyLinkedList<PtrType, OtherContainerTraits>;
    using OtherContainerType      = newcode::HashTable<OtherContainerTraits, OtherBucketType>;
};

DEFINE_TEST_OBJECTS(HTDLL);
using UMTE = DEFINE_TEST_THUNK(Associative, HTDLL, Unmanaged);
using UPTE = DEFINE_TEST_THUNK(Associative, HTDLL, UniquePtr);
using RPTE = DEFINE_TEST_THUNK(Associative, HTDLL, RefPtr);

UNITTEST_START_TESTCASE(hashtable_dll_tests)
//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
UNITTEST("Clear (unmanaged)",            UMTE::ClearTest)
UNITTEST("Clear (unique)",               UPTE::ClearTest)
UNITTEST("Clear (RefPtr)",               RPTE::ClearTest)

UNITTEST("IsEmpty (unmanaged)",          UMTE::IsEmptyTest)
UNITTEST("IsEmpty (unique)",             UPTE::IsEmptyTest)
UNITTEST("IsEmpty (RefPtr)",             RPTE::IsEmptyTest)

UNITTEST("Iterate (unmanaged)",          UMTE::IterateTest)
UNITTEST("Iterate (unique)",             UPTE::IterateTest)
UNITTEST("Iterate (RefPtr)",             RPTE::IterateTest)

UNITTEST("IterErase (unmanaged)",        UMTE::IterEraseTest)
UNITTEST("IterErase (unique)",           UPTE::IterEraseTest)
UNITTEST("IterErase (RefPtr)",           RPTE::IterEraseTest)

UNITTEST("DirectErase (unmanaged)",      UMTE::DirectEraseTest)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("DirectErase (unique)",         UPTE::DirectEraseTest)
#endif
UNITTEST("DirectErase (RefPtr)",         RPTE::DirectEraseTest)

UNITTEST("MakeIterator (unmanaged)",     UMTE::MakeIteratorTest)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("MakeIterator (unique)",        UPTE::MakeIteratorTest)
#endif
UNITTEST("MakeIterator (RefPtr)",        RPTE::MakeIteratorTest)

UNITTEST("ReverseIterErase (unmanaged)", UMTE::ReverseIterEraseTest)
UNITTEST("ReverseIterErase (unique)",    UPTE::ReverseIterEraseTest)
UNITTEST("ReverseIterErase (RefPtr)",    RPTE::ReverseIterEraseTest)

UNITTEST("ReverseIterate (unmanaged)",   UMTE::ReverseIterateTest)
UNITTEST("ReverseIterate (unique)",      UPTE::ReverseIterateTest)
UNITTEST("ReverseIterate (RefPtr)",      RPTE::ReverseIterateTest)

// Hash tables do not support swapping or Rvalue operations (Assignment or
// construction) as doing so would be an O(n) operation (With 'n' == to the
// number of buckets in the hashtable)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("Swap (unmanaged)",             UMTE::SwapTest)
UNITTEST("Swap (unique)",                UPTE::SwapTest)
UNITTEST("Swap (RefPtr)",                RPTE::SwapTest)

UNITTEST("Rvalue Ops (unmanaged)",       UMTE::RvalueOpsTest)
UNITTEST("Rvalue Ops (unique)",          UPTE::RvalueOpsTest)
UNITTEST("Rvalue Ops (RefPtr)",          RPTE::RvalueOpsTest)
#endif

UNITTEST("Scope (unique)",               UPTE::ScopeTest)
UNITTEST("Scope (RefPtr)",               RPTE::ScopeTest)

UNITTEST("TwoContainer (unmanaged)",     UMTE::TwoContainerTest)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("TwoContainer (unique)",        UPTE::TwoContainerTest)
#endif
UNITTEST("TwoContainer (RefPtr)",        RPTE::TwoContainerTest)

UNITTEST("EraseIf (unmanaged)",          UMTE::EraseIfTest)
UNITTEST("EraseIf (unique)",             UPTE::EraseIfTest)
UNITTEST("EraseIf (RefPtr)",             RPTE::EraseIfTest)

UNITTEST("FindIf (unmanaged)",           UMTE::FindIfTest)
UNITTEST("FindIf (unique)",              UPTE::FindIfTest)
UNITTEST("FindIf (RefPtr)",              RPTE::FindIfTest)

//////////////////////////////////////////
// Associative container specific tests.
//////////////////////////////////////////
UNITTEST("InsertAscending (unmanaged)",  UMTE::InsertAscendingTest)
UNITTEST("InsertAscending (unique)",     UPTE::InsertAscendingTest)
UNITTEST("InsertAscending (RefPtr)",     RPTE::InsertAscendingTest)

UNITTEST("InsertDescending (unmanaged)", UMTE::InsertDescendingTest)
UNITTEST("InsertDescending (unique)",    UPTE::InsertDescendingTest)
UNITTEST("InsertDescending (RefPtr)",    RPTE::InsertDescendingTest)

UNITTEST("InsertRandom (unmanaged)",     UMTE::InsertRandomTest)
UNITTEST("InsertRandom (unique)",        UPTE::InsertRandomTest)
UNITTEST("InsertRandom (RefPtr)",        RPTE::InsertRandomTest)

UNITTEST_END_TESTCASE(hashtable_dll_tests,
                      "htdll",
                      "Intrusive hash table tests (doubly linked list buckets).",
                      NULL, NULL);

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace utils
