// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>

#include <fbl/intrusive_pointer_traits.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/tests/intrusive_containers/intrusive_wavl_tree_checker.h>
#include <fbl/tests/intrusive_containers/ordered_associative_container_test_environment.h>
#include <fbl/tests/intrusive_containers/test_thunks.h>
#include <zxtest/zxtest.h>

namespace fbl {
namespace tests {
namespace intrusive_containers {

using ::fbl::internal::valid_sentinel_ptr;

template <typename ContainerStateType>
struct OtherTreeTraits {
  using KeyType = typename ContainerStateType::KeyType;
  using PtrType = typename ContainerStateType::PtrType;
  using PtrTraits = ::fbl::internal::ContainerPtrTraits<PtrType>;

  // Node Traits
  static WAVLTreeNodeState<PtrType>& node_state(typename PtrTraits::RefType obj) {
    return obj.other_container_state_.node_state_;
  }

  // Key Traits
  static KeyType GetKey(typename PtrTraits::ConstRefType obj) {
    return obj.other_container_state_.key_;
  }
  static bool LessThan(const KeyType& key1, const KeyType& key2) { return key1 < key2; }
  static bool EqualTo(const KeyType& key1, const KeyType& key2) { return key1 == key2; }

  // Set key is a trait which is only used by the tests, not by the containers
  // themselves.
  static void SetKey(typename PtrTraits::RefType obj, KeyType key) {
    obj.other_container_state_.key_ = key;
  }
};

template <typename _KeyType, typename _PtrType>
class OtherTreeNodeState {
 public:
  using KeyType = _KeyType;
  using PtrType = _PtrType;

 private:
  friend struct OtherTreeTraits<OtherTreeNodeState<KeyType, PtrType>>;
  WAVLTreeNodeState<PtrType> node_state_;
  KeyType key_ = 0;
};

template <typename PtrType>
class WAVLTraits {
 public:
  // clang-format off
    using KeyType                 = size_t;
    using TestObjBaseType         = KeyedTestObjBase<KeyType>;

    using ContainerType           = WAVLTree<KeyType, PtrType>;
    using ContainableBaseClass    = WAVLTreeContainable<PtrType>;
    using ContainerStateType      = WAVLTreeNodeState<PtrType>;

    using OtherContainerStateType = OtherTreeNodeState<KeyType, PtrType>;
    using OtherContainerTraits    = OtherTreeTraits<OtherContainerStateType>;
    using OtherContainerType      = WAVLTree<KeyType,
                                             PtrType,
                                             OtherContainerTraits,
                                             OtherContainerTraits>;

    struct Tag1 {};
    struct Tag2 {};
    struct Tag3 {};

    using TaggedContainableBaseClasses =
        fbl::ContainableBaseClasses<WAVLTreeContainable<PtrType, Tag1>,
                                    WAVLTreeContainable<PtrType, Tag2>,
                                    WAVLTreeContainable<PtrType, Tag3>>;

    using TaggedType1 = TaggedWAVLTree<KeyType, PtrType, Tag1>;
    using TaggedType2 = TaggedWAVLTree<KeyType, PtrType, Tag2>;
    using TaggedType3 = TaggedWAVLTree<KeyType, PtrType, Tag3>;
  // clang-format on
};

// Just a sanity check so we know our metaprogramming nonsense is
// doing what we expect:
static_assert(
    std::is_same_v<typename WAVLTraits<int*>::TaggedContainableBaseClasses::TagTypes,
                   std::tuple<typename WAVLTraits<int*>::Tag1, typename WAVLTraits<int*>::Tag2,
                              typename WAVLTraits<int*>::Tag3>>);

// Generate all of the standard tests.
// clang-format off
DEFINE_TEST_OBJECTS(WAVL);
using UMTE   = DEFINE_TEST_THUNK(OrderedAssociative, WAVL, Unmanaged);
using UPDDTE = DEFINE_TEST_THUNK(OrderedAssociative, WAVL, UniquePtrDefaultDeleter);
using UPCDTE = DEFINE_TEST_THUNK(OrderedAssociative, WAVL, UniquePtrCustomDeleter);
using RPTE   = DEFINE_TEST_THUNK(OrderedAssociative, WAVL, RefPtr);
VERIFY_CONTAINER_SIZES(WAVL, sizeof(void*) * 4);
// clang-format on

// WAVLBalanceTestObserver
//
// An implementation of a WAVLTree Observer which collects stats on the number
// of balance operations (inserts, erases, rank promotions, rank demotions and
// rotatations) which have taken place.  It is used by the BalanceTest to verify
// that...
//
// 1) The computation costs of rebalancing after insert and erase are amortized
//    constant and obey their specific worst-case constant bounds.
// 2) The maximum depth bounds for trees with just insert operations, and with
//    both insert and erase operations, are obeyed.
// 3) Sufficient code coverage has been achieved during testing (eg. all of the
//    rebalancing edge cases have been run over the length of the test).
class WAVLBalanceTestObserver {
 public:
  struct OpCounts {
    OpCounts() { reset(); }

    void reset() {
      insert_ops_ = 0;
      insert_promotes_ = 0;
      insert_rotations_ = 0;
      insert_double_rotations_ = 0;
      erase_ops_ = 0;
      erase_demotes_ = 0;
      erase_rotations_ = 0;
      erase_double_rotations_ = 0;
    }

    void accumulate(OpCounts& target) {
      target.insert_ops_ += insert_ops_;
      target.insert_promotes_ += insert_promotes_;
      target.insert_rotations_ += insert_rotations_;
      target.insert_double_rotations_ += insert_double_rotations_;
      target.erase_ops_ += erase_ops_;
      target.erase_demotes_ += erase_demotes_;
      target.erase_rotations_ += erase_rotations_;
      target.erase_double_rotations_ += erase_double_rotations_;
    }

    size_t insert_ops_;
    size_t insert_promotes_;
    size_t insert_rotations_;
    size_t insert_double_rotations_;

    size_t erase_ops_;
    size_t erase_demotes_;
    size_t erase_rotations_;
    size_t erase_double_rotations_;
  };

  static void ResetObserverOpCounts() { op_counts_.reset(); }
  static void AccumulateObserverOpCounts(OpCounts& target) { op_counts_.accumulate(target); }

  static void RecordInsert() { ++op_counts_.insert_ops_; }
  static void RecordInsertPromote() { ++op_counts_.insert_promotes_; }
  static void RecordInsertRotation() { ++op_counts_.insert_rotations_; }
  static void RecordInsertDoubleRotation() { ++op_counts_.insert_double_rotations_; }
  static void RecordErase() { ++op_counts_.erase_ops_; }
  static void RecordEraseDemote() { ++op_counts_.erase_demotes_; }
  static void RecordEraseRotation() { ++op_counts_.erase_rotations_; }
  static void RecordEraseDoubleRotation() { ++op_counts_.erase_double_rotations_; }

  template <typename TreeType>
  static void VerifyRankRule(const TreeType& tree, typename TreeType::RawPtrType node) {
    using NodeTraits = typename TreeType::NodeTraits;

    // Check the rank rule.  The rules for a WAVL tree are...
    // 1) All rank differences are either 1 or 2
    // 2) All leaf nodes have rank 0 (by implication, all rank
    //    differences are non-negative)
    const auto& ns = NodeTraits::node_state(*node);
    ASSERT_LE(0, ns.rank_, "All ranks must be non-negative.");

    if (!valid_sentinel_ptr(ns.left_) && !valid_sentinel_ptr(ns.right_)) {
      ASSERT_EQ(0, ns.rank_, "Leaf nodes must have rank 0!");
    } else {
      if (valid_sentinel_ptr(ns.left_)) {
        auto& left_ns = NodeTraits::node_state(*ns.left_);
        auto delta = ns.rank_ - left_ns.rank_;
        ASSERT_LE(1, delta, "Left hand rank difference not on range [1, 2]");
        ASSERT_GE(2, delta, "Left hand rank difference not on range [1, 2]");
      }

      if (valid_sentinel_ptr(ns.right_)) {
        auto& right_ns = NodeTraits::node_state(*ns.right_);
        auto delta = ns.rank_ - right_ns.rank_;
        ASSERT_LE(1, delta, "Right hand rank difference not on range [1, 2]");
        ASSERT_GE(2, delta, "Right hand rank difference not on range [1, 2]");
      }
    }
  }

  template <typename TreeType>
  static void VerifyBalance(const TreeType& tree, uint64_t depth) {
    // Compute the maximum expected depth.
    uint64_t max_depth = 0;
    if (tree.size()) {
      // If we have performed erase operations, the max depth should be
      // rounddown(2 * log_2(N)) + 1.
      //
      // If we have not performed any erases, then the max depth should be
      // rounddown(log_phi(N)) + 1.  We know that...
      //
      // phi = (1 + sqrt(5)) / 2
      // log_phi(N) = log_2(N) / log_2(phi)
      //
      // Start by computing log_2(N), then scale by either 2.0, or
      // (1/log_2(phi)).
      constexpr double one_over_log2_phi = 1.4404200904125563642566021371749229729175567626953125;
      double log2N = log2(static_cast<double>(tree.size()));
      double scale = op_counts_.erase_ops_ ? 2.0 : one_over_log2_phi;

      max_depth = static_cast<uint64_t>(log2N * scale) + 1;
    }

    size_t total_insert_rotations =
        op_counts_.insert_rotations_ + op_counts_.insert_double_rotations_;
    EXPECT_LE(op_counts_.insert_promotes_,
              (3 * op_counts_.insert_ops_) + (2 * op_counts_.erase_ops_),
              "#insert promotes must be <= (3 * #inserts) + (2 * #erases)");
    EXPECT_LE(total_insert_rotations, op_counts_.insert_ops_,
              "#insert_rotations must be <= #inserts");

    size_t total_erase_rotations = op_counts_.erase_rotations_ + op_counts_.erase_double_rotations_;
    EXPECT_LE(op_counts_.erase_demotes_, op_counts_.erase_ops_,
              "#erase demotes must be <= #erases");
    EXPECT_LE(total_erase_rotations, op_counts_.erase_ops_, "#erase_rotations must be <= #erases");

    EXPECT_GE(max_depth, depth);
  }

 private:
  static OpCounts op_counts_;
};

// Static storage for the observer.
WAVLBalanceTestObserver::OpCounts WAVLBalanceTestObserver::op_counts_;

// Test objects during the balance test will be allocated as a block all at once
// and cleaned up at the end of the test.  Our test containers, however, are
// containers of unique pointers to objects with a no-op delete.  This allows
// the containers to go out of scope with elements still in them (in case of a
// REQUIRE failure) without triggering the container assert for destroying a
// container of unmanaged pointer with elements still in it.
class BalanceTestObj;

using BalanceTestKeyType = uint64_t;
using BalanceTestObjPtr = std::unique_ptr<BalanceTestObj>;
using BalanceTestTree = WAVLTree<BalanceTestKeyType, BalanceTestObjPtr,
                                 DefaultKeyedObjectTraits<BalanceTestKeyType, BalanceTestObj>,
                                 DefaultWAVLTreeTraits<BalanceTestObjPtr, int32_t>,
                                 DefaultObjectTag, WAVLBalanceTestObserver>;

class BalanceTestObj {
 public:
  void Init(BalanceTestKeyType val) {
    key_ = val;
    erase_deck_ptr_ = this;
  }

  BalanceTestKeyType GetKey() const { return key_; }
  BalanceTestObj* EraseDeckPtr() const { return erase_deck_ptr_; }

  void SwapEraseDeckPtr(BalanceTestObj& other) {
    BalanceTestObj* tmp = erase_deck_ptr_;
    erase_deck_ptr_ = other.erase_deck_ptr_;
    other.erase_deck_ptr_ = tmp;
  }

  bool InContainer() const { return wavl_node_state_.InContainer(); }

 private:
  friend DefaultWAVLTreeTraits<BalanceTestObjPtr, int32_t>;

  static void operator delete(void* ptr) {
    // Deliberate no-op
  }
  friend class std::default_delete<BalanceTestObj>;

  BalanceTestKeyType key_;
  BalanceTestObj* erase_deck_ptr_;
  WAVLTreeNodeState<BalanceTestObjPtr, int32_t> wavl_node_state_;
};

static constexpr size_t kBalanceTestSize = 2048;

static void DoBalanceTestInsert(BalanceTestTree& tree, BalanceTestObj* ptr) {
  // The selected object should not be in the tree.
  ASSERT_NOT_NULL(ptr);
  ASSERT_FALSE(ptr->InContainer());

  // Put the object into the tree.  Assert that it succeeds, then
  // sanity check the tree.
  ASSERT_TRUE(tree.insert_or_find(BalanceTestObjPtr(ptr)));
  ASSERT_NO_FAILURES(WAVLTreeChecker::SanityCheck(tree));
}

static void DoBalanceTestErase(BalanceTestTree& tree, BalanceTestObj* ptr) {
  // The selected object should still be in the tree.
  ASSERT_NOT_NULL(ptr);
  ASSERT_TRUE(ptr->InContainer());

  // Erase should find the object and transfer its pointer back to us.
  // The object should no longer be in the tree.
  BalanceTestObjPtr erased = tree.erase(ptr->GetKey());
  ASSERT_EQ(ptr, erased.get());
  ASSERT_FALSE(ptr->InContainer());

  // Run a full sanity check on the tree.  Its depth should be
  // consistent with a tree which has seen both inserts and erases.
  ASSERT_NO_FAILURES(WAVLTreeChecker::SanityCheck(tree));
}

static void ShuffleEraseDeck(const std::unique_ptr<BalanceTestObj[]>& objects,
                             Lfsr<BalanceTestKeyType>& rng) {
  // Note: shuffle algorithm is a Fisher-Yates (aka Knuth) shuffle.
  static_assert(kBalanceTestSize > 0, "Test size must be positive!");
  for (size_t i = kBalanceTestSize - 1; i > 1; --i) {
    size_t ndx = static_cast<size_t>(rng.GetNext()) % i;
    if (ndx != i)
      objects[i].SwapEraseDeckPtr(objects[ndx]);
  }
}

// clang-format off
//////////////////////////////////////////
// General container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(WavlTreeTest, UMTE,     Clear)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   Clear)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   Clear)
RUN_ZXTEST(WavlTreeTest, RPTE,     Clear)

RUN_ZXTEST(WavlTreeTest, UMTE,     ClearUnsafe)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(WavlTreeTest, UPDDTE,   ClearUnsafe)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   ClearUnsafe)
RUN_ZXTEST(WavlTreeTest, RPTE,     ClearUnsafe)
#endif

RUN_ZXTEST(WavlTreeTest, UMTE,     IsEmpty)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   IsEmpty)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   IsEmpty)
RUN_ZXTEST(WavlTreeTest, RPTE,     IsEmpty)

RUN_ZXTEST(WavlTreeTest, UMTE,     Iterate)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   Iterate)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   Iterate)
RUN_ZXTEST(WavlTreeTest, RPTE,     Iterate)

RUN_ZXTEST(WavlTreeTest, UMTE,     IterErase)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   IterErase)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   IterErase)
RUN_ZXTEST(WavlTreeTest, RPTE,     IterErase)

RUN_ZXTEST(WavlTreeTest, UMTE,     DirectErase)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   DirectErase)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   DirectErase)
RUN_ZXTEST(WavlTreeTest, RPTE,     DirectErase)

RUN_ZXTEST(WavlTreeTest, UMTE,     MakeIterator)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   MakeIterator)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   MakeIterator)
RUN_ZXTEST(WavlTreeTest, RPTE,     MakeIterator)

RUN_ZXTEST(WavlTreeTest, UMTE,     ReverseIterErase)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   ReverseIterErase)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   ReverseIterErase)
RUN_ZXTEST(WavlTreeTest, RPTE,     ReverseIterErase)

RUN_ZXTEST(WavlTreeTest, UMTE,     ReverseIterate)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   ReverseIterate)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   ReverseIterate)
RUN_ZXTEST(WavlTreeTest, RPTE,     ReverseIterate)

RUN_ZXTEST(WavlTreeTest, UMTE,     Swap)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   Swap)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   Swap)
RUN_ZXTEST(WavlTreeTest, RPTE,     Swap)

RUN_ZXTEST(WavlTreeTest, UMTE,     RvalueOps)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   RvalueOps)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   RvalueOps)
RUN_ZXTEST(WavlTreeTest, RPTE,     RvalueOps)

RUN_ZXTEST(WavlTreeTest, UPDDTE,   Scope)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   Scope)
RUN_ZXTEST(WavlTreeTest, RPTE,     Scope)

RUN_ZXTEST(WavlTreeTest, UMTE,     TwoContainer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(WavlTreeTest, UPDDTE,   TwoContainer)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   TwoContainer)
#endif
RUN_ZXTEST(WavlTreeTest, RPTE,     TwoContainer)

RUN_ZXTEST(WavlTreeTest, UMTE,     ThreeContainerHelper)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(WavlTreeTest, UPDDTE,   ThreeContainerHelper)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   ThreeContainerHelper)
#endif
RUN_ZXTEST(WavlTreeTest, RPTE,     ThreeContainerHelper)

RUN_ZXTEST(WavlTreeTest, UMTE,     IterCopyPointer)
#if TEST_WILL_NOT_COMPILE || 0
RUN_ZXTEST(WavlTreeTest, UPDDTE,   IterCopyPointer)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   IterCopyPointer)
#endif
RUN_ZXTEST(WavlTreeTest, RPTE,     IterCopyPointer)

RUN_ZXTEST(WavlTreeTest, UMTE,     EraseIf)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   EraseIf)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   EraseIf)
RUN_ZXTEST(WavlTreeTest, RPTE,     EraseIf)

RUN_ZXTEST(WavlTreeTest, UMTE,     FindIf)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   FindIf)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   FindIf)
RUN_ZXTEST(WavlTreeTest, RPTE,     FindIf)

//////////////////////////////////////////
// Associative container specific tests.
//////////////////////////////////////////
RUN_ZXTEST(WavlTreeTest, UMTE,     InsertByKey)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   InsertByKey)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   InsertByKey)
RUN_ZXTEST(WavlTreeTest, RPTE,     InsertByKey)

RUN_ZXTEST(WavlTreeTest, UMTE,     FindByKey)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   FindByKey)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   FindByKey)
RUN_ZXTEST(WavlTreeTest, RPTE,     FindByKey)

RUN_ZXTEST(WavlTreeTest, UMTE,     EraseByKey)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   EraseByKey)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   EraseByKey)
RUN_ZXTEST(WavlTreeTest, RPTE,     EraseByKey)

RUN_ZXTEST(WavlTreeTest, UMTE,     InsertOrFind)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   InsertOrFind)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   InsertOrFind)
RUN_ZXTEST(WavlTreeTest, RPTE,     InsertOrFind)

RUN_ZXTEST(WavlTreeTest, UMTE,     InsertOrReplace)
RUN_ZXTEST(WavlTreeTest, UPDDTE,   InsertOrReplace)
RUN_ZXTEST(WavlTreeTest, UPCDTE,   InsertOrReplace)
RUN_ZXTEST(WavlTreeTest, RPTE,     InsertOrReplace)

////////////////////////////////////////////////
// OrderedAssociative container specific tests.
////////////////////////////////////////////////
RUN_ZXTEST(WavlTreeTest, UMTE, OrderedIter)
RUN_ZXTEST(WavlTreeTest, UPDDTE,  OrderedIter)
RUN_ZXTEST(WavlTreeTest, UPCDTE,  OrderedIter)
RUN_ZXTEST(WavlTreeTest, RPTE, OrderedIter)

RUN_ZXTEST(WavlTreeTest, UMTE, OrderedReverseIter)
RUN_ZXTEST(WavlTreeTest, UPDDTE,  OrderedReverseIter)
RUN_ZXTEST(WavlTreeTest, UPCDTE,  OrderedReverseIter)
RUN_ZXTEST(WavlTreeTest, RPTE, OrderedReverseIter)

RUN_ZXTEST(WavlTreeTest, UMTE, UpperBound)
RUN_ZXTEST(WavlTreeTest, UPDDTE,  UpperBound)
RUN_ZXTEST(WavlTreeTest, UPCDTE,  UpperBound)
RUN_ZXTEST(WavlTreeTest, RPTE, UpperBound)

RUN_ZXTEST(WavlTreeTest, UMTE, LowerBound)
RUN_ZXTEST(WavlTreeTest, UPDDTE,  LowerBound)
RUN_ZXTEST(WavlTreeTest, UPCDTE,  LowerBound)
RUN_ZXTEST(WavlTreeTest, RPTE, LowerBound)

// The balance test is a pretty heavy test.  Only enable it when asked to do so.
#if FBL_TEST_ENABLE_WAVL_TREE_BALANCE_TEST
TEST(WavlTreeTest, Balance) {
  WAVLBalanceTestObserver::OpCounts op_counts;

  // Declare these in a specific order (object pointer first) so that the tree
  // has a chance to clean up before the memory backing the objects gets
  // cleaned up.
  std::unique_ptr<BalanceTestObj[]> objects;
  BalanceTestTree tree;

  // We will run this test 3 times with 3 different (constant) seeds.  During
  // the first run, we will insert all of the elements with ascending key
  // order.  During the second run, we will insert all of the keys with
  // descending key order.  During the final run, we will insert all of the
  // keys in a random order.
  Lfsr<BalanceTestKeyType> rng;
  static const BalanceTestKeyType seeds[] = {0xe87e1062fc1f4f80u, 0x03d6bffb124b4918u,
                                             0x8f7d83e8d10b4765u};

  // Allocate the objects we will use for the test.
  {
    AllocChecker ac;
    objects.reset(new (&ac) BalanceTestObj[kBalanceTestSize]);
    ASSERT_TRUE(ac.check(), "Failed to allocate test objects!");
  }

  for (size_t seed_ndx = 0; seed_ndx < fbl::count_of(seeds); ++seed_ndx) {
    auto seed = seeds[seed_ndx];

    // Seed the RNG and reset the observer stats.
    rng.SetCore(seed);
    WAVLBalanceTestObserver::ResetObserverOpCounts();

    // Initialize each object with the proper key for this run.  This places
    // the object in the erase deck sequence at the same time.
    switch (seed_ndx) {
      case 0u:
        for (size_t i = 0; i < kBalanceTestSize; ++i)
          objects[i].Init(i);
        break;

      case 1u:
        for (size_t i = 0; i < kBalanceTestSize; ++i)
          objects[i].Init(kBalanceTestSize - i);
        break;

      default:
        for (size_t i = 0; i < kBalanceTestSize; ++i)
          objects[i].Init(rng.GetNext());
        break;
    }

    // Place each object into the tree, then perform a full sanity check on
    // the tree.  If anything goes wrong, just abort the test.  If we keep
    // going, we are just going to get an unmanageable amt of errors.
    for (size_t i = 0; i < kBalanceTestSize; ++i) {
      ASSERT_NO_FAILURES(DoBalanceTestInsert(tree, &objects[i]));
    }

    // Shuffle the erase deck.
    ShuffleEraseDeck(objects, rng);

    // Erase half of the elements in the tree.
    for (size_t i = 0; i < (kBalanceTestSize >> 1); ++i) {
      ASSERT_NO_FAILURES(DoBalanceTestErase(tree, objects[i].EraseDeckPtr()));
    }

    // Put the elements back so that we have inserted some elements into a
    // non-empty tree which has seen erase operations.
    for (size_t i = 0; i < (kBalanceTestSize >> 1); ++i) {
      ASSERT_NO_FAILURES(DoBalanceTestInsert(tree, objects[i].EraseDeckPtr()));
    }

    // Shuffle the erase deck again.
    ShuffleEraseDeck(objects, rng);

    // Now erase every element from the tree.
    for (size_t i = 0; i < kBalanceTestSize; ++i) {
      ASSERT_NO_FAILURES(DoBalanceTestErase(tree, objects[i].EraseDeckPtr()));
    }

    ASSERT_EQ(0u, tree.size());

    WAVLBalanceTestObserver::AccumulateObserverOpCounts(op_counts);
  }

  // Finally, make sure that we have exercised all of the different re-balance
  // cases.
  EXPECT_LT(0u, op_counts.insert_ops_, "Insufficient test coverage!");
  EXPECT_LT(0u, op_counts.insert_promotes_, "Insufficient test coverage!");
  EXPECT_LT(0u, op_counts.insert_rotations_, "Insufficient test coverage!");
  EXPECT_LT(0u, op_counts.insert_double_rotations_, "Insufficient test coverage!");
  EXPECT_LT(0u, op_counts.erase_ops_, "Insufficient test coverage!");
  EXPECT_LT(0u, op_counts.erase_demotes_, "Insufficient test coverage!");
  EXPECT_LT(0u, op_counts.erase_rotations_, "Insufficient test coverage!");
  EXPECT_LT(0u, op_counts.erase_double_rotations_, "Insufficient test coverage!");
}
#endif

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl
