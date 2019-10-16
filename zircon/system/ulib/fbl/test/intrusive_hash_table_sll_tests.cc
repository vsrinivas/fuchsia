// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/tests/intrusive_containers/associative_container_test_environment.h>
#include <fbl/tests/intrusive_containers/intrusive_hash_table_checker.h>
#include <fbl/tests/intrusive_containers/test_thunks.h>
#include <zxtest/zxtest.h>

namespace fbl {
namespace tests {
namespace intrusive_containers {

using OtherKeyType = uint16_t;
using OtherHashType = uint32_t;
static constexpr OtherHashType kOtherNumBuckets = 23;

template <typename PtrType>
struct OtherHashTraits {
  using ObjType = typename ::fbl::internal::ContainerPtrTraits<PtrType>::ValueType;
  using BucketStateType = SinglyLinkedListNodeState<PtrType>;

  // Linked List Traits
  static BucketStateType& node_state(ObjType& obj) {
    return obj.other_container_state_.bucket_state_;
  }

  // Keyed Object Traits
  static OtherKeyType GetKey(const ObjType& obj) { return obj.other_container_state_.key_; }

  static bool LessThan(const OtherKeyType& key1, const OtherKeyType& key2) { return key1 < key2; }

  static bool EqualTo(const OtherKeyType& key1, const OtherKeyType& key2) { return key1 == key2; }

  // Hash Traits
  static OtherHashType GetHash(const OtherKeyType& key) {
    return static_cast<OtherHashType>((key * 0xaee58187) % kOtherNumBuckets);
  }

  // Set key is a trait which is only used by the tests, not by the containers
  // themselves.
  static void SetKey(ObjType& obj, OtherKeyType key) { obj.other_container_state_.key_ = key; }
};

template <typename PtrType>
struct OtherHashState {
 private:
  friend struct OtherHashTraits<PtrType>;
  OtherKeyType key_;
  typename OtherHashTraits<PtrType>::BucketStateType bucket_state_;
};

template <typename PtrType>
class HTSLLTraits {
 public:
  // clang-format off
    using ObjType = typename ::fbl::internal::ContainerPtrTraits<PtrType>::ValueType;

    using ContainerType           = HashTable<size_t, PtrType>;
    using ContainableBaseClass    = SinglyLinkedListable<PtrType>;
    using ContainerStateType      = SinglyLinkedListNodeState<PtrType>;
    using KeyType                 = typename ContainerType::KeyType;
    using HashType                = typename ContainerType::HashType;

    using OtherContainerTraits    = OtherHashTraits<PtrType>;
    using OtherContainerStateType = OtherHashState<PtrType>;
    using OtherBucketType         = SinglyLinkedList<PtrType, OtherContainerTraits>;
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

    struct Tag1 {};
    struct Tag2 {};
    struct Tag3 {};

    using TaggedContainableBaseClasses =
        fbl::ContainableBaseClasses<SinglyLinkedListable<PtrType, Tag1>,
                                    SinglyLinkedListable<PtrType, Tag2>,
                                    SinglyLinkedListable<PtrType, Tag3>>;

    using TaggedType1 = TaggedHashTable<size_t, PtrType, Tag1>;
    using TaggedType2 = TaggedHashTable<size_t, PtrType, Tag2>;
    using TaggedType3 = TaggedHashTable<size_t, PtrType, Tag3>;
  // clang-format on
};

// clang-format off
DEFINE_TEST_OBJECTS(HTSLL);
using UMTE    = DEFINE_TEST_THUNK(Associative, HTSLL, Unmanaged);
using UPTE    = DEFINE_TEST_THUNK(Associative, HTSLL, UniquePtr);
using SUPDDTE = DEFINE_TEST_THUNK(Associative, HTSLL, StdUniquePtrDefaultDeleter);
using SUPCDTE = DEFINE_TEST_THUNK(Associative, HTSLL, StdUniquePtrCustomDeleter);
using RPTE    = DEFINE_TEST_THUNK(Associative, HTSLL, RefPtr);

//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     Clear)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     Clear)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  Clear)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  Clear)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     Clear)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     ClearUnsafe)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     ClearUnsafe)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  ClearUnsafe)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  ClearUnsafe)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     ClearUnsafe)
#endif

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     IsEmpty)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     IsEmpty)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  IsEmpty)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  IsEmpty)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     IsEmpty)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     Iterate)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     Iterate)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  Iterate)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  Iterate)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     Iterate)

// Hashtables with singly linked list bucket can perform direct
// iterator/reference erase operations, but the operations will be O(n)
RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     IterErase)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     IterErase)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  IterErase)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  IterErase)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     IterErase)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     DirectErase)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     DirectErase)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  DirectErase)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  DirectErase)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     DirectErase)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     MakeIterator)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     MakeIterator)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  MakeIterator)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  MakeIterator)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     MakeIterator)

// HashTables with SinglyLinkedList buckets cannot iterate backwards (because
// their buckets cannot iterate backwards)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     ReverseIterErase)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     ReverseIterErase)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  ReverseIterErase)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  ReverseIterErase)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     ReverseIterErase)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     ReverseIterate)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     ReverseIterate)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  ReverseIterate)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  ReverseIterate)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     ReverseIterate)
#endif

// Hash tables do not support swapping or Rvalue operations (Assignment or
// construction) as doing so would be an O(n) operation (With 'n' == to the
// number of buckets in the hashtable)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     Swap)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     Swap)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  Swap)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  Swap)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     Swap)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     RvalueOps)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     RvalueOps)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  RvalueOps)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  RvalueOps)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     RvalueOps)
#endif

RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     Scope)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  Scope)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  Scope)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     Scope)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     TwoContainer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     TwoContainer)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  TwoContainer)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  TwoContainer)
#endif
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     TwoContainer)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     ThreeContainerHelper)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     ThreeContainerHelper)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  ThreeContainerHelper)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  ThreeContainerHelper)
#endif
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     ThreeContainerHelper)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     IterCopyPointer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     IterCopyPointer)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  IterCopyPointer)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  IterCopyPointer)
#endif
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     IterCopyPointer)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     EraseIf)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     EraseIf)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  EraseIf)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  EraseIf)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     EraseIf)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     FindIf)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     FindIf)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  FindIf)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  FindIf)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     FindIf)

//////////////////////////////////////////
// Associative container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     InsertByKey)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     InsertByKey)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  InsertByKey)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  InsertByKey)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     InsertByKey)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     FindByKey)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     FindByKey)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  FindByKey)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  FindByKey)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     FindByKey)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     EraseByKey)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     EraseByKey)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  EraseByKey)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  EraseByKey)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     EraseByKey)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     InsertOrFind)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     InsertOrFind)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  InsertOrFind)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  InsertOrFind)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     InsertOrFind)

RUN_ZXTEST(SinglyLinkedHashTableTest, UMTE,     InsertOrReplace)
RUN_ZXTEST(SinglyLinkedHashTableTest, UPTE,     InsertOrReplace)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPDDTE,  InsertOrReplace)
RUN_ZXTEST(SinglyLinkedHashTableTest, SUPCDTE,  InsertOrReplace)
RUN_ZXTEST(SinglyLinkedHashTableTest, RPTE,     InsertOrReplace)
// clang-format on

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl
