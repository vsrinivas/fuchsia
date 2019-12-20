// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/id.h"

#include <atomic>

namespace {

// The session id is global to support cross-system registration and debugging.
std::atomic<scheduling::SessionId> next_session_id = 1;

}  // namespace

namespace scheduling {

SessionId GetNextSessionId() { return next_session_id++; }

}  // namespace scheduling
