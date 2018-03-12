// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/err.h"

namespace zxdb {

class Command;
class ConsoleContext;

// Handles execution of command input consisting of a noun and no verb.
// For example "process", "process 2 thread", "thread 5".
Err ExecuteNoun(ConsoleContext* context, const Command& cmd);

}  // namespace zxdb
