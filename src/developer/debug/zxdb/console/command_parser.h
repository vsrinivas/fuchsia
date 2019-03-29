// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

namespace zxdb {

class Command;
class Err;

// Converts the given string to a series of tokens. This is used by ParseCommand
// and is exposed
// separate for testing purposes.
Err TokenizeCommand(const std::string& input, std::vector<std::string>* result);

Err ParseCommand(const std::string& input, Command* output);

// Returns a set of possible completions for the given input. The result will
// be empty if there are none.
std::vector<std::string> GetCommandCompletions(const std::string& input);

}  // namespace zxdb
