// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"

namespace zxdb {

class Command;
class Console;
class ConsoleContext;
class Thread;

// Handles execution of command input consisting of a noun and no verb.
// For example "process", "process 2 thread", "thread 5".
Err ExecuteNoun(ConsoleContext* context, const Command& cmd);

// Populates the nounds map.
void AppendNouns(std::map<Noun, NounRecord>* nouns);

// Returns the set of all switches valid for nouns. Since a command can have
// multiple nouns, which set of switches apply can be complicated.
//
// Currently, when a command lacks a verb, the logic in ExecuteNoun() will
// prioritize which one the user meant and therefore, which one the switches
// will apply to.
//
// If the noun switches start getting more complicated, we will probably want
// to have a priority associated with a noun so the parser can figure out
// which noun is being executed and apply switches on a per-noun basis.
const std::vector<SwitchRecord>& GetNounSwitches();

}  // namespace zxdb
