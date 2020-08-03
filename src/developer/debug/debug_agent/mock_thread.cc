// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_thread.h"

#include "src/developer/debug/debug_agent/mock_exception_handle.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

MockThread::MockThread(DebuggedProcess* process, zx_koid_t thread_koid)
    : DebuggedThread(process->debug_agent(), process,
                     std::make_unique<MockThreadHandle>(thread_koid)) {}

MockThread::~MockThread() {}

}  // namespace debug_agent
