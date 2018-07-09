// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include "garnet/bin/zxdb/console/actions.h"

namespace zxdb {

void PrintVersion();
Err PrintHelp(const std::string& cmd_name);

// Returns the set of actions that the vector wants to run.
Err ProcessScriptFile(const std::string& path, std::vector<Action>*,
                      const std::string& mock_contents = std::string());

}   // namespace zxdb
