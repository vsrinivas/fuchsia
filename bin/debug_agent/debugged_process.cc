// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/debugged_process.h"

#include <utility>

DebuggedProcess::DebuggedProcess(zx_koid_t koid, zx::process proc)
    : koid_(koid), process_(std::move(proc)) {}
DebuggedProcess::~DebuggedProcess() = default;
