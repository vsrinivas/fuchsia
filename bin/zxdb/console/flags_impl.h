// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include "garnet/bin/zxdb/console/actions.h"

namespace zxdb {

// NOTE: Flags should be ordered in the order they will be evaluated in
//       ProcessCommandLine

void PrintVersion();

Err PrintHelp(const std::string& cmd_name);

// Will enqueue a Connect Action
Err ProcessConnect(const std::string& host, std::vector<Action>*);

// Will enqueue a Connect Action
Err ProcessRun(const std::string& path, std::vector<Action>*);

// Returns the set of actions that the vector wants to run.
Err ProcessScriptFile(const std::string& path, std::vector<Action>*,
                      const std::string& mock_contents = std::string());

}   // namespace zxdb
