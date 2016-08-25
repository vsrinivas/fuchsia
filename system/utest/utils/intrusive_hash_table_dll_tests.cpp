// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <unittest.h>
#include <utils/intrusive_double_list.h>
#include <utils/intrusive_hash_table.h>
#include <utils/tests/intrusive_containers/associative_container_test_environment.h>
#include <utils/tests/intrusive_containers/intrusive_hash_table_checker.h>
#include <utils/tests/intrusive_containers/test_thunks.h>

namespace utils {
namespace tests {
namespace intrusive_containers {

using OtherKeyType  = uint16_t;
using OtherHashType = uint32_t;
static constexpr OtherHashType kOtherNumBuckets = 23;

template <typename PtrType>
struct OtherHashTraits {
    using ObjType = typename ::utils::internal::ContainerPtrTraits<PtrType>::ValueType;
    using BucketStateType = DoublyLinkedListNodeState<PtrType>;

    // Linked List Traits
    static BucketStateType& node_state(ObjType& obj) {
        return obj.other_container_state_.bucket_state_;
    }

    // Keyed Object Traits
    static OtherKeyType GetKey(const ObjType& obj) {
        return obj.other_container_state_.key_;
    }

    static bool LessThan(const OtherKeyType& key1, const OtherKeyType& key2) {
        return key1 <  key2;
    }

    static bool EqualTo(const OtherKeyType& key1, const OtherKeyType& key2) {
        return key1 == key2;
    }

    // Hash Traits
    static OtherHashType GetHash(const OtherKeyType& key) {
        return static_cast<OtherHashType>((key * 0xaee58187) % kOtherNumBuckets);
    }

    // Set key is a trait which is only used by the tests, not by the containers
    // themselves.
    static void SetKey(ObjType& obj, OtherKeyType key) {
        obj.other_container_state_.key_ = key;
    }
};

template <typename PtrType>
struct OtherHashState {
private:
    friend struct OtherHashTraits<PtrType>;
    OtherKeyType key_;
    typename OtherHashTraits<PtrType>::BucketStateType bucket_state_;
};

template <typename PtrType>
class HTDLLTraits {
public:
    using ObjType = typename ::utils::internal::ContainerPtrTraits<PtrType>::ValueType;

    using ContainerType           = HashTable<size_t, PtrType, DoublyLinkedList<PtrType>>;
    using ContainableBaseClass    = DoublyLinkedListable<PtrType>;
    using ContainerStateType      = DoublyLinkedListNodeState<PtrType>;
    using KeyType                 = typename ContainerType::KeyType;
    using HashType                = typename ContainerType::HashType;

    using OtherContainerTraits    = OtherHashTraits<PtrType>;
    using OtherContainerStateType = OtherHashState<PtrType>;
    using OtherBucketType         = DoublyLinkedList<PtrType, OtherContainerTraits>;
    using OtherContainerType      = HashTable<OtherKeyType,
                                              PtrType,
                                              OtherBucketType,
                                              OtherHashType,
                                              kOtherNumBuckets,
                                              OtherContainerTraits,
                                              OtherContainerTraits>;

    using TestObjBaseType  = HashedTestObjBase<typename ContainerType::KeyType,
                                               typename ContainerType::HashType,
                                               ContainerType::kNumBuckets>;
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
UNITTEST("InsertByKey (unmanaged)",      UMTE::InsertByKeyTest)
UNITTEST("InsertByKey (unique)",         UPTE::InsertByKeyTest)
UNITTEST("InsertByKey (RefPtr)",         RPTE::InsertByKeyTest)

UNITTEST("FindByKey (unmanaged)",        UMTE::FindByKeyTest)
UNITTEST("FindByKey (unique)",           UPTE::FindByKeyTest)
UNITTEST("FindByKey (RefPtr)",           RPTE::FindByKeyTest)

UNITTEST("EraseByKey (unmanaged)",       UMTE::EraseByKeyTest)
UNITTEST("EraseByKey (unique)",          UPTE::EraseByKeyTest)
UNITTEST("EraseByKey (RefPtr)",          RPTE::EraseByKeyTest)

UNITTEST("InsertOrFind (unmanaged)",     UMTE::InsertOrFindTest)
UNITTEST("InsertOrFind (unique)",        UPTE::InsertOrFindTest)
UNITTEST("InsertOrFind (RefPtr)",        RPTE::InsertOrFindTest)
UNITTEST_END_TESTCASE(hashtable_dll_tests,
                      "htdll",
                      "Intrusive hash table tests (doubly linked list buckets).",
                      NULL, NULL);

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace utils
