// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace utils {

namespace tests {
namespace intrusive_containers {
// Fwd decl of sanity checker class used by tests.
class WAVLTreeChecker;

// Definition of the default (no-op) Observer.
//
// Observers are used by the test framework to record the number of insert,
// erase, rank-promote, rank-demote and rotation operations performed during
// usage.  The DefaultWAVLTreeObserver does nothing and should fall out of the
// code during template expansion.
//
// Note: Records of promotions and demotions are used by tests to demonstrate
// that the computational complexity of insert/erase rebalancing is amortized
// constant.  Promotions and demotions which are side effects of the rotation
// phase of rebalancing are considered to be part of the cost of rotation and
// are not tallied in the overall promote/demote accounting.
//
struct DefaultWAVLTreeObserver {
    static void RecordInsert()               { }
    static void RecordInsertPromote()        { }
    static void RecordInsertRotation()       { }
    static void RecordInsertDoubleRotation() { }

    static void RecordErase()                { }
    static void RecordEraseDemote()          { }
    static void RecordEraseRotation()        { }
    static void RecordEraseDoubleRotation()  { }

    template <typename TreeType>
    static bool VerifyRankRule(const TreeType& tree, typename TreeType::RawPtrType node) {
        return true;
    }

    template <typename TreeType>
    static bool VerifyBalance(const TreeType& tree, uint64_t depth) {
        return true;
    }
};

}  // namespace tests
}  // namespace intrusive_containers

// General prototype of the WAVL tree rank state.  By default, we just use a
// bool to record the rank parity of a node.  During testing, however, we
// actually use a specialized version of the node state in which the rank is
// stored as an int32_t so that extra sanity checks can be made during balance
// testing.
template <typename RankType> struct WAVLTreeNodeRank;

// Testing specialization of the rank state.  Stores rank as an int32_t so that
// the rank rule can be explicitly verified during testing.
template <>
struct WAVLTreeNodeRank<int32_t> {
    bool rank_parity() const   { return ((rank_ & 0x1) != 0); }
    void promote_rank()        { rank_ += 1; }
    void double_promote_rank() { rank_ += 2; }
    void demote_rank()         { rank_ -= 1; }
    void double_demote_rank()  { rank_ -= 2; }

    int32_t rank_;
};

}  // namespace utils
