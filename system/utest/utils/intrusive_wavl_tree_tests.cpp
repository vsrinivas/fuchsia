// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <unittest.h>
#include <utils/intrusive_wavl_tree.h>
#include <utils/tests/intrusive_containers/intrusive_wavl_tree_checker.h>
#include <utils/tests/intrusive_containers/ordered_associative_container_test_environment.h>
#include <utils/tests/intrusive_containers/test_thunks.h>
#include <utils/intrusive_pointer_traits.h>

namespace utils {
namespace tests {
namespace intrusive_containers {

template <typename ContainerStateType>
struct OtherTreeTraits {
    using KeyType = typename ContainerStateType::KeyType;
    using PtrType = typename ContainerStateType::PtrType;
    using PtrTraits = ::utils::internal::ContainerPtrTraits<PtrType>;

    // Node Traits
    static WAVLTreeNodeState<PtrType>& node_state(typename PtrTraits::RefType obj) {
        return obj.other_container_state_.node_state_;
    }

    // Key Traits
    static KeyType GetKey(typename PtrTraits::ConstRefType obj) {
        return obj.other_container_state_.key_;
    }
    static bool LessThan(const KeyType& key1, const KeyType& key2)  { return key1 < key2; }
    static bool EqualTo (const KeyType& key1, const KeyType& key2)  { return key1 == key2; }

    // Set key is a trait which is only used by the tests, not by the containers
    // themselves.
    static void SetKey(typename PtrTraits::RefType obj, KeyType key) {
        obj.other_container_state_.key_ = key;
    }
};

template <typename _KeyType, typename _PtrType>
struct OtherTreeNodeState {
public:
    using KeyType = _KeyType;
    using PtrType = _PtrType;

private:
    friend class OtherTreeTraits<OtherTreeNodeState<KeyType, PtrType>>;
    WAVLTreeNodeState<PtrType> node_state_;
    KeyType key_ = 0;
};

template <typename PtrType>
class WAVLTraits {
public:
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
};

// Generate all of the standard tests.
DEFINE_TEST_OBJECTS(WAVL);
using UMTE = DEFINE_TEST_THUNK(OrderedAssociative, WAVL, Unmanaged);
using UPTE = DEFINE_TEST_THUNK(OrderedAssociative, WAVL, UniquePtr);
using RPTE = DEFINE_TEST_THUNK(OrderedAssociative, WAVL, RefPtr);

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
            insert_ops_              = 0;
            insert_promotes_         = 0;
            insert_rotations_        = 0;
            insert_double_rotations_ = 0;
            erase_ops_               = 0;
            erase_demotes_           = 0;
            erase_rotations_         = 0;
            erase_double_rotations_  = 0;
        }

        void accumulate(OpCounts& target) {
            target.insert_ops_              += insert_ops_;
            target.insert_promotes_         += insert_promotes_;
            target.insert_rotations_        += insert_rotations_;
            target.insert_double_rotations_ += insert_double_rotations_;
            target.erase_ops_               += erase_ops_;
            target.erase_demotes_           += erase_demotes_;
            target.erase_rotations_         += erase_rotations_;
            target.erase_double_rotations_  += erase_double_rotations_;
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

    static void RecordInsert()                  { ++op_counts_.insert_ops_; }
    static void RecordInsertPromote()           { ++op_counts_.insert_promotes_; }
    static void RecordInsertRotation()          { ++op_counts_.insert_rotations_; }
    static void RecordInsertDoubleRotation()    { ++op_counts_.insert_double_rotations_; }
    static void RecordErase()                   { ++op_counts_.erase_ops_; }
    static void RecordEraseDemote()             { ++op_counts_.erase_demotes_; }
    static void RecordEraseRotation()           { ++op_counts_.erase_rotations_; }
    static void RecordEraseDoubleRotation()     { ++op_counts_.erase_double_rotations_; }

    template <typename TreeType>
    static bool VerifyRankRule(const TreeType& tree, typename TreeType::RawPtrType node) {
        BEGIN_TEST;
        using NodeTraits = typename TreeType::NodeTraits;
        using PtrTraits  = typename TreeType::PtrTraits;

        REQUIRE_TRUE(PtrTraits::IsValid(node), "");

        // Check the rank rule.  The rules for a WAVL tree are...
        // 1) All rank differences are either 1 or 2
        // 2) All leaf nodes have rank 0 (by implication, all rank
        //    differences are non-negative)
        const auto& ns = NodeTraits::node_state(*node);
        REQUIRE_LE(0, ns.rank_, "All ranks must be non-negative.");

        if (!PtrTraits::IsValid(ns.left_) && !PtrTraits::IsValid(ns.right_)) {
            REQUIRE_EQ(0, ns.rank_, "Leaf nodes must have rank 0!");
        } else {
            if (PtrTraits::IsValid(ns.left_)) {
                auto& left_ns = NodeTraits::node_state(*ns.left_);
                auto  delta   = ns.rank_ - left_ns.rank_;
                REQUIRE_LE(1, delta, "Left hand rank difference not on range [1, 2]");
                REQUIRE_GE(2, delta, "Left hand rank difference not on range [1, 2]");
            }

            if (PtrTraits::IsValid(ns.right_)) {
                auto& right_ns = NodeTraits::node_state(*ns.right_);
                auto  delta    = ns.rank_ - right_ns.rank_;
                REQUIRE_LE(1, delta, "Right hand rank difference not on range [1, 2]");
                REQUIRE_GE(2, delta, "Right hand rank difference not on range [1, 2]");
            }
        }

        END_TEST;
    }

    template <typename TreeType>
    static bool VerifyBalance(const TreeType& tree, uint64_t depth) {
        BEGIN_TEST;

        // Compute the maximum expected depth.  If we have performed no erase
        // operations, this should be rounddown(log_phi(size) + 1) where phi is
        // the golden ratio.  Otherwise, this should be rounddown(log_2(size) +
        // 1).
        //
        // TODO(johngro): we have no math library in the kernel.  How are we
        // going to compute log_phi(size)?
        uint64_t max_depth = 0;
        if (tree.size()) {
            for (size_t tmp = tree.size(); tmp; tmp >>= 1)
                max_depth++;

            // If we have not performed any erases, then the max depth should be
            // log_phi(N).  Otherwise it should be 2 * log_2(N)
            if (!op_counts_.erase_ops_) {
                // TODO: tweak max_depth properly.  We know that...
                //
                // phi = (1 + 5^0.5) / 2
                // log_phi(N) = log_2(N) / log_2(phi)
                //
                // We can approximate log_2(phi) using a rational number and
                // scale max_depth, but max_depth is currently rounded up to the
                // nearest integer.  We need to keep it in floating point before
                // scaling and then rounding up in order to keep our bound as
                // tight as it should be.
                //
                // Restricting things to 32 bit multipliers, we can
                // approximate...
                //
                // X / log_2(phi) ~= (0xb85faf7e * X) / 0x80000000
                //                 = (0xb85faf7e * X) >> 31
                max_depth  *= 0xb85faf7e;
                max_depth >>= 31;
            } else {
                max_depth <<= 1;
            }
        }

        size_t total_insert_rotations = op_counts_.insert_rotations_
                                      + op_counts_.insert_double_rotations_;
        EXPECT_LE(op_counts_.insert_promotes_,
                (3 * op_counts_.insert_ops_) + (2 * op_counts_.erase_ops_),
                "#insert promotes must be <= (3 * #inserts) + (2 * #erases)");
        EXPECT_LE(total_insert_rotations, op_counts_.insert_ops_,
                "#insert_rotations must be <= #inserts");

        size_t total_erase_rotations = op_counts_.erase_rotations_
                                     + op_counts_.erase_double_rotations_;
        EXPECT_LE(op_counts_.erase_demotes_, op_counts_.erase_ops_,
                "#erase demotes must be <= #erases");
        EXPECT_LE(total_erase_rotations, op_counts_.erase_ops_,
                "#erase_rotations must be <= #erases");

        EXPECT_GE(max_depth, depth, "");

        END_TEST;
    }

private:
    static OpCounts op_counts_;
};

// Static storage for the observer.
WAVLBalanceTestObserver::OpCounts WAVLBalanceTestObserver::op_counts_;

// Test objects during the balance test will be allocated as a block all at once
// and cleaned up at the end of the test.  Our test containers, however, are
// containers of unique pointers with a no-op Deleter trait.  This allows the
// containers to go out of scope with elements still in them (in case of a
// REQUIRE failure) without triggering the container assert for destroying a
// container of unmanaged pointer with elements still in it.
class BalanceTestObj;
struct NopDelete { inline void operator()(BalanceTestObj*) const { } };

using BalanceTestKeyType = uint64_t;
using BalanceTestObjPtr  = unique_ptr<BalanceTestObj, NopDelete>;
using BalanceTestTree    = WAVLTree<BalanceTestKeyType,
                                    BalanceTestObjPtr,
                                    DefaultKeyedObjectTraits<BalanceTestKeyType, BalanceTestObj>,
                                    DefaultWAVLTreeTraits<BalanceTestObjPtr, int32_t>,
                                    WAVLBalanceTestObserver>;

class BalanceTestObj {
public:
    void Init(BalanceTestKeyType val) {
        key_ = val;
        erase_deck_ptr_ = this;
    }

    BalanceTestKeyType GetKey() const { return key_; }
    BalanceTestObj* EraseDeckPtr() const { return erase_deck_ptr_; };

    void SwapEraseDeckPtr(BalanceTestObj& other) {
        BalanceTestObj* tmp   = erase_deck_ptr_;
        erase_deck_ptr_       = other.erase_deck_ptr_;
        other.erase_deck_ptr_ = tmp;
    }

    bool InContainer() const { return wavl_node_state_.InContainer(); }

private:
    friend DefaultWAVLTreeTraits<BalanceTestObjPtr, int32_t>;

    BalanceTestKeyType key_;
    BalanceTestObj* erase_deck_ptr_;
    WAVLTreeNodeState<BalanceTestObjPtr, int32_t> wavl_node_state_;
};

static constexpr size_t kBalanceTestSize = 2048;

static bool DoBalanceTestInsert(BalanceTestTree& tree, BalanceTestObj* ptr) {
    BEGIN_TEST;

    // The selected object should not be in the tree.
    REQUIRE_NONNULL(ptr, "");
    REQUIRE_FALSE(ptr->InContainer(), "");

    // Put the object into the tree.  Assert that it succeeds, then
    // sanity check the tree.
    REQUIRE_TRUE(tree.insert_or_find(BalanceTestObjPtr(ptr)), "");
    REQUIRE_TRUE(WAVLTreeChecker::SanityCheck(tree), "");

    END_TEST;
}

static bool DoBalanceTestErase(BalanceTestTree& tree, BalanceTestObj* ptr) {
    BEGIN_TEST;

    // The selected object should still be in the tree.
    REQUIRE_NONNULL(ptr, "");
    REQUIRE_TRUE(ptr->InContainer(), "");

    // Erase should find the object and transfer its pointer back to us.
    // The object should no longer be in the tree.
    BalanceTestObjPtr erased = tree.erase(ptr->GetKey());
    REQUIRE_EQ(ptr, erased.get(), "");
    REQUIRE_FALSE(ptr->InContainer(), "");

    // Run a full sanity check on the tree.  Its depth should be
    // consistent with a tree which has seen both inserts and erases.
    REQUIRE_TRUE(WAVLTreeChecker::SanityCheck(tree), "");

    END_TEST;
}

static void ShuffleEraseDeck(const unique_ptr<BalanceTestObj[]>& objects,
                             Lfsr<BalanceTestKeyType>& rng) {
    // Note: shuffle algorithm is a Fisher-Yates (aka Knuth) shuffle.
    static_assert(kBalanceTestSize > 0, "Test size must be positive!");
    for (size_t i = kBalanceTestSize - 1; i > 1; --i) {
        size_t ndx = static_cast<size_t>(rng.GetNext()) % i;
        if (ndx != i)
            objects[i].SwapEraseDeckPtr(objects[ndx]);
    }
}

static bool WAVLBalanceTest(void*) {
    BEGIN_TEST;

    WAVLBalanceTestObserver::OpCounts op_counts;

    // Declare these in a specific order (object pointer first) so that the tree
    // has a chance to clean up before the memory backing the objects gets
    // cleaned up.
    unique_ptr<BalanceTestObj[]> objects;
    BalanceTestTree tree;

    // We will run this test 3 times with 3 different (constant) seeds.  During
    // the first run, we will insert all of the elements with ascending key
    // order.  During the second run, we will insert all of the keys with
    // descending key order.  During the final run, we will insert all of the
    // keys in a random order.
    Lfsr<BalanceTestKeyType> rng;
    static const BalanceTestKeyType seeds[] = { 0xe87e1062fc1f4f80u,
                                                0x03d6bffb124b4918u,
                                                0x8f7d83e8d10b4765u };

    // Allocate the objects we will use for the test.
    {
        AllocChecker ac;
        objects.reset(new (&ac) BalanceTestObj[kBalanceTestSize]);
        REQUIRE_TRUE(ac.check(), "Failed to allocate test objects!");
    }

    for (size_t seed_ndx = 0; seed_ndx < countof(seeds); ++seed_ndx) {
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
        for (size_t i = 0; i < kBalanceTestSize; ++i)
            REQUIRE_TRUE(DoBalanceTestInsert(tree, &objects[i]), "");

        // Shuffle the erase deck.
        ShuffleEraseDeck(objects, rng);

        // Erase half of the elements in the tree.
        for (size_t i = 0; i < (kBalanceTestSize >> 1); ++i)
            REQUIRE_TRUE(DoBalanceTestErase(tree, objects[i].EraseDeckPtr()), "");

        // Put the elements back so that we have inserted some elements into a
        // non-empty tree which has seen erase operations.
        for (size_t i = 0; i < (kBalanceTestSize >> 1); ++i)
            REQUIRE_TRUE(DoBalanceTestInsert(tree, objects[i].EraseDeckPtr()), "");

        // Shuffle the erase deck again.
        ShuffleEraseDeck(objects, rng);

        // Now erase every element from the tree.
        for (size_t i = 0; i < kBalanceTestSize; ++i)
            REQUIRE_TRUE(DoBalanceTestErase(tree, objects[i].EraseDeckPtr()), "");

        REQUIRE_EQ(0u, tree.size(), "");

        WAVLBalanceTestObserver::AccumulateObserverOpCounts(op_counts);
    }

    // Finally, make sure that we have exercised all of the different re-balance
    // cases.
    EXPECT_LT(0u, op_counts.insert_ops_,              "Insufficient test coverage!");
    EXPECT_LT(0u, op_counts.insert_promotes_,         "Insufficient test coverage!");
    EXPECT_LT(0u, op_counts.insert_rotations_,        "Insufficient test coverage!");
    EXPECT_LT(0u, op_counts.insert_double_rotations_, "Insufficient test coverage!");
    EXPECT_LT(0u, op_counts.erase_ops_,               "Insufficient test coverage!");
    EXPECT_LT(0u, op_counts.erase_demotes_,           "Insufficient test coverage!");
    EXPECT_LT(0u, op_counts.erase_rotations_,         "Insufficient test coverage!");
    EXPECT_LT(0u, op_counts.erase_double_rotations_,  "Insufficient test coverage!");

    END_TEST;
}

UNITTEST_START_TESTCASE(wavl_tree_tests)
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

UNITTEST("Swap (unmanaged)",             UMTE::SwapTest)
UNITTEST("Swap (unique)",                UPTE::SwapTest)
UNITTEST("Swap (RefPtr)",                RPTE::SwapTest)

UNITTEST("Rvalue Ops (unmanaged)",       UMTE::RvalueOpsTest)
UNITTEST("Rvalue Ops (unique)",          UPTE::RvalueOpsTest)
UNITTEST("Rvalue Ops (RefPtr)",          RPTE::RvalueOpsTest)

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

////////////////////////////////////////////////
// OrderedAssociative container specific tests.
////////////////////////////////////////////////
UNITTEST("OrderedIter (unmanaged)",        UMTE::OrderedIterTest)
UNITTEST("OrderedIter (unique)",           UPTE::OrderedIterTest)
UNITTEST("OrderedIter (RefPtr)",           RPTE::OrderedIterTest)

UNITTEST("OrderedReverseIter (unmanaged)", UMTE::OrderedReverseIterTest)
UNITTEST("OrderedReverseIter (unique)",    UPTE::OrderedReverseIterTest)
UNITTEST("OrderedReverseIter (RefPtr)",    RPTE::OrderedReverseIterTest)

UNITTEST("UpperBound (unmanaged)",         UMTE::UpperBoundTest)
UNITTEST("UpperBound (unique)",            UPTE::UpperBoundTest)
UNITTEST("UpperBound (RefPtr)",            RPTE::UpperBoundTest)

UNITTEST("LowerBound (unmanaged)",         UMTE::LowerBoundTest)
UNITTEST("LowerBound (unique)",            UPTE::LowerBoundTest)
UNITTEST("LowerBound (RefPtr)",            RPTE::LowerBoundTest)

////////////////////////////
// WAVLTree specific tests.
////////////////////////////
UNITTEST("BalanceTest", WAVLBalanceTest)

UNITTEST_END_TESTCASE(wavl_tree_tests,
                      "wavl",
                      "Intrusive WAVL tree tests.",
                      NULL, NULL);

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace utils
