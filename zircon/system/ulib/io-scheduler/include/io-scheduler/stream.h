// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
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

    // List support.
    using ListNodeState = fbl::DoublyLinkedListNodeState<StreamRef>;
    struct ListTraitsUnsorted {
        static ListNodeState& node_state(Stream& s) { return s.list_node_; }
    };

    using ListUnsorted = fbl::DoublyLinkedList<StreamRef, ListTraitsUnsorted>;

    ListNodeState list_node_;
    uint32_t id_;
    uint32_t priority_;
};


} // namespace ioscheduler
