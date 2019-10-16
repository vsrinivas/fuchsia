// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_hash_table.h>
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
  using BucketStateType = DoublyLinkedListNodeState<PtrType>;

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
class HTDLLTraits {
 public:
  // clang-format off
    using ObjType = typename ::fbl::internal::ContainerPtrTraits<PtrType>::ValueType;

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

    struct Tag1 {};
    struct Tag2 {};
    struct Tag3 {};

    using TaggedContainableBaseClasses =
        fbl::ContainableBaseClasses<DoublyLinkedListable<PtrType, Tag1>,
                                    DoublyLinkedListable<PtrType, Tag2>,
                                    DoublyLinkedListable<PtrType, Tag3>>;

    using TaggedType1 = HashTable<size_t, PtrType, TaggedDoublyLinkedList<PtrType, Tag1>>;
    using TaggedType2 = HashTable<size_t, PtrType, TaggedDoublyLinkedList<PtrType, Tag2>>;
    using TaggedType3 = HashTable<size_t, PtrType, TaggedDoublyLinkedList<PtrType, Tag3>>;
  // clang-format on
};

// clang-format off
DEFINE_TEST_OBJECTS(HTDLL);
using UMTE    = DEFINE_TEST_THUNK(Associative, HTDLL, Unmanaged);
using UPTE    = DEFINE_TEST_THUNK(Associative, HTDLL, UniquePtr);
using SUPDDTE = DEFINE_TEST_THUNK(Associative, HTDLL, StdUniquePtrDefaultDeleter);
using SUPCDTE = DEFINE_TEST_THUNK(Associative, HTDLL, StdUniquePtrCustomDeleter);
using RPTE    = DEFINE_TEST_THUNK(Associative, HTDLL, RefPtr);

//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     Clear)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     Clear)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  Clear)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  Clear)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     Clear)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     ClearUnsafe)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     ClearUnsafe)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  ClearUnsafe)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  ClearUnsafe)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     ClearUnsafe)
#endif

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     IsEmpty)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     IsEmpty)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  IsEmpty)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  IsEmpty)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     IsEmpty)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     Iterate)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     Iterate)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  Iterate)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  Iterate)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     Iterate)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     IterErase)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     IterErase)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  IterErase)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  IterErase)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     IterErase)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     DirectErase)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     DirectErase)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  DirectErase)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  DirectErase)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     DirectErase)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     MakeIterator)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     MakeIterator)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  MakeIterator)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  MakeIterator)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     MakeIterator)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     ReverseIterErase)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     ReverseIterErase)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  ReverseIterErase)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  ReverseIterErase)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     ReverseIterErase)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     ReverseIterate)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     ReverseIterate)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  ReverseIterate)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  ReverseIterate)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     ReverseIterate)

// Hash tables do not support swapping or Rvalue operations (Assignment or
// construction) as doing so would be an O(n) operation (With 'n' == to the
// number of buckets in the hashtable)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     Swap)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     Swap)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  Swap)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  Swap)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     Swap)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     RvalueOps)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     RvalueOps)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  RvalueOps)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  RvalueOps)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     RvalueOps)
#endif

RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     Scope)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  Scope)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  Scope)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     Scope)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     TwoContainer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     TwoContainer)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  TwoContainer)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  TwoContainer)
#endif
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     TwoContainer)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     ThreeContainerHelper)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     ThreeContainerHelper)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  ThreeContainerHelper)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  ThreeContainerHelper)
#endif
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     ThreeContainerHelper)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     IterCopyPointer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     IterCopyPointer)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  IterCopyPointer)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  IterCopyPointer)
#endif
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     IterCopyPointer)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     EraseIf)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     EraseIf)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  EraseIf)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  EraseIf)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     EraseIf)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     FindIf)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     FindIf)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  FindIf)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  FindIf)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     FindIf)

//////////////////////////////////////////
// Associative container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     InsertByKey)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     InsertByKey)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  InsertByKey)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  InsertByKey)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     InsertByKey)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     FindByKey)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     FindByKey)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  FindByKey)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  FindByKey)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     FindByKey)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     EraseByKey)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     EraseByKey)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  EraseByKey)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  EraseByKey)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     EraseByKey)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     InsertOrFind)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     InsertOrFind)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  InsertOrFind)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  InsertOrFind)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     InsertOrFind)

RUN_ZXTEST(DoublyLinkedHashTableTest, UMTE,     InsertOrReplace)
RUN_ZXTEST(DoublyLinkedHashTableTest, UPTE,     InsertOrReplace)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPDDTE,  InsertOrReplace)
RUN_ZXTEST(DoublyLinkedHashTableTest, SUPCDTE,  InsertOrReplace)
RUN_ZXTEST(DoublyLinkedHashTableTest, RPTE,     InsertOrReplace)
// clang-format on

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl
