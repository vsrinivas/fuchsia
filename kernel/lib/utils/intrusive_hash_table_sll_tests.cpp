// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <unittest.h>
#include <utils/intrusive_single_list.h>
#include <utils/intrusive_hash_table.h>
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
class HTSLLTraits {
public:
    using KeyType                 = size_t;
    using HashTraits              = DefaultHashTraits<KeyType, PtrType>;
    using HashType                = typename HashTraits::HashType;
    using TestObjBaseType         = HashedTestObjBase<HashTraits>;

    using ContainerType           = DefaultHashTable<KeyType, PtrType>;
    using ContainableBaseClass    = SinglyLinkedListable<PtrType>;
    using ContainerStateType      = SinglyLinkedListNodeState<PtrType>;

    using OtherContainerStateType = OtherHashState<KeyType, SinglyLinkedListNodeState<PtrType>>;
    using OtherContainerTraits    = OtherHashTraits<OtherContainerStateType>;
    using OtherBucketType         = SinglyLinkedList<PtrType, OtherContainerTraits>;
    using OtherContainerType      = HashTable<OtherContainerTraits, OtherBucketType>;
};

DEFINE_TEST_OBJECTS(HTSLL);
using UMTE = DEFINE_TEST_THUNK(Associative, HTSLL, Unmanaged);
using UPTE = DEFINE_TEST_THUNK(Associative, HTSLL, UniquePtr);
using RPTE = DEFINE_TEST_THUNK(Associative, HTSLL, RefPtr);

UNITTEST_START_TESTCASE(hashtable_sll_tests)
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

// Hashtables with singly linked list bucket can perform direct
// iterator/reference erase operations, but the operations will be O(n)
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

// HashTables with SinglyLinkedList buckets cannot iterate backwards (because
// their buckets cannot iterate backwards)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("ReverseIterErase (unmanaged)", UMTE::ReverseIterEraseTest)
UNITTEST("ReverseIterErase (unique)",    UPTE::ReverseIterEraseTest)
UNITTEST("ReverseIterErase (RefPtr)",    RPTE::ReverseIterEraseTest)

UNITTEST("ReverseIterate (unmanaged)",   UMTE::ReverseIterateTest)
UNITTEST("ReverseIterate (unique)",      UPTE::ReverseIterateTest)
UNITTEST("ReverseIterate (RefPtr)",      RPTE::ReverseIterateTest)
#endif

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
UNITTEST("InsertByKey (unmanaged)",      UMTE::InsertByKeyTest)
UNITTEST("InsertByKey (unique)",         UPTE::InsertByKeyTest)
UNITTEST("InsertByKey (RefPtr)",         RPTE::InsertByKeyTest)

UNITTEST("FindByKey (unmanaged)",        UMTE::FindByKeyTest)
UNITTEST("FindByKey (unique)",           UPTE::FindByKeyTest)
UNITTEST("FindByKey (RefPtr)",           RPTE::FindByKeyTest)

UNITTEST("EraseByKey (unmanaged)",       UMTE::EraseByKeyTest)
UNITTEST("EraseByKey (unique)",          UPTE::EraseByKeyTest)
UNITTEST("EraseByKey (RefPtr)",          RPTE::EraseByKeyTest)
UNITTEST_END_TESTCASE(hashtable_sll_tests,
                      "htsll",
                      "Intrusive hash table tests (singly linked list buckets).",
                      NULL, NULL);

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace utils
