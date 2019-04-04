// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace ioscheduler {

class Stream;
using StreamRef = fbl::RefPtr<Stream>;

class Stream : public fbl::RefCounted<Stream> {
public:
    Stream(uint32_t id, uint32_t pri);
    ~Stream();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Stream);

    uint32_t Id() { return id_; }
    uint32_t Priority() { return priority_; }

    // Functions requiring the Scheduler stream lock be held.
    bool IsActive() { return active_; }
    void SetActive(bool active) { active_ = active; }
    void Close();

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
    bool active_ = false;   // Stream has ops and is being scheduled.
};


} // namespace ioscheduler
