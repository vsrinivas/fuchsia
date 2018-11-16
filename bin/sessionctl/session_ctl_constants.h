// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONCTL_COMMAND_NAMES_H_
#define PERIDOT_BIN_SESSIONCTL_COMMAND_NAMES_H_

namespace modular {

// Commands available to SessionCtlApp.
constexpr char kAddModCommandString[] = "add_mod";
constexpr char kDeleteStoryCommandString[] = "delete_story";
constexpr char kRemoveModCommandString[] = "remove_mod";

// Flags to pass to SessionCtlApp.
constexpr char kJsonOutFlagString[] = "json_out";
constexpr char kFocusModFlagString[] = "focus_mod";
constexpr char kFocusStoryFlagString[] = "focus_story";
constexpr char kModNameFlagString[] = "mod_name";
constexpr char kModUrlFlagString[] = "mod_url";
constexpr char kStoryIdFlagString[] = "story_id";
constexpr char kStoryNameFlagString[] = "story_name";

// Internal error string returned from SessionCtlApp.ExecuteCommand() if
// the user does not set a required flag.
constexpr char kGetUsageErrorString[] = "GetUsage";
}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONCTL_COMMAND_NAMES_H_
