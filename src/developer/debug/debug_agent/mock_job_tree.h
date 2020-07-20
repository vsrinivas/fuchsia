// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_JOB_TREE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_JOB_TREE_H_

#include "src/developer/debug/debug_agent/mock_job_handle.h"

namespace debug_agent {

// Creates a default process tree:
//
//  j: 1 root
//    p: 2 root-p1
//      t: 3 initial-thread
//    p: 4 root-p2
//      t: 5 initial-thread
//    p: 6 root-p3
//      t: 7 initial-thread
//    j: 8 job1
//      p: 9 job1-p1
//        t: 10 initial-thread
//      p: 11 job1-p2
//        t: 12 initial-thread
//      j: 13 job11
//        p: 14 job11-p1
//          t: 15 initial-thread
//          t: 16 second-thread
//      j: 17 job12
//        j: 18 job121
//          p: 19 job121-p1
//            t: 20 initial-thread
//          p: 21 job121-p2
//            t: 22 initial-thread
//            t: 23 second-thread
//            t: 24 third-thread
std::unique_ptr<MockJobHandle> GetMockJobTree();

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_JOB_TREE_H_
