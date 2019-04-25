// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <zircon/types.h>

#include <io-scheduler/stream-op.h>

namespace ioscheduler {

class Scheduler;
class Stream;
using StreamRef = fbl::RefPtr<Stream>;

// Stream - a logical sequence of ops.
// The Stream class is not thread safe, streams depend on the scheduler for synchronization.
// Certain calls must be performed with the Scheduler's stream_lock_ held.
class Stream : public fbl::RefCounted<Stream> {
public:
    Stream(uint32_t id, uint32_t pri);
    ~Stream();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Stream);

    uint32_t Id() { return id_; }
    uint32_t Priority() { return priority_; }

    void Close();

    // Functions requiring the Scheduler stream lock be held.
    // ---------------------------------------------------------

    // Insert an op into the tail of the stream (subject to reordering).
    // On error op's error status is set and it is moved to |*op_err|.
    zx_status_t Push(UniqueOp op, UniqueOp* op_err);

    // Fetch an op from the head of the stream.
    UniqueOp Pop();

    // Does the stream contain any ops that are not yet issued?
    bool IsEmpty() { return (num_acquired_ == 0); }

    // ---------------------------------------------------------
    // End functions requiring stream lock.

    // WAVL Tree support.
    using WAVLTreeNodeState = fbl::WAVLTreeNodeState<StreamRef>;
    struct WAVLTreeNodeTraitsSortById {
        static WAVLTreeNodeState& node_state(Stream& s) { return s.map_node_; }
    };

    struct KeyTraitsSortById {
        static const uint32_t& GetKey(const Stream& s) { return s.id_; }
        static bool LessThan(const uint32_t s1, const uint32_t s2) { return (s1 < s2); }
        static bool EqualTo(const uint32_t s1, const uint32_t s2) { return (s1 == s2); }
    };

    using WAVLTreeSortById = fbl::WAVLTree<uint32_t, StreamRef, KeyTraitsSortById,
                                           WAVLTreeNodeTraitsSortById>;

    // List support.
    using ListNodeState = fbl::DoublyLinkedListNodeState<StreamRef>;
    struct ListTraitsUnsorted {
        static ListNodeState& node_state(Stream& s) { return s.list_node_; }
    };

    using ListUnsorted = fbl::DoublyLinkedList<StreamRef, ListTraitsUnsorted>;

private:
    friend struct WAVLTreeNodeTraitsSortById;
    friend struct KeyTraitsSortById;

    WAVLTreeNodeState map_node_;
    ListNodeState list_node_;
    uint32_t id_;
    uint32_t priority_;
    bool open_ = true;      // Stream is open, can accept more ops.

    uint32_t num_acquired_ = 0; // Number of ops acquired and waiting for issue.
    fbl::DoublyLinkedList<StreamOp*> acquired_list_;
};


} // namespace ioscheduler
