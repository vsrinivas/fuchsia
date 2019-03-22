// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONCTL_SESSION_CTL_CONSTANTS_H_
#define PERIDOT_BIN_SESSIONCTL_SESSION_CTL_CONSTANTS_H_

namespace modular {

constexpr char kSessionCtlString[] = "sessionctl";

// Commands available to SessionCtlApp.
constexpr char kAddModCommandString[] = "add_mod";
constexpr char kDeleteAllStoriesCommandString[] = "delete_all_stories";
constexpr char kDeleteStoryCommandString[] = "delete_story";
constexpr char kListStoriesCommandString[] = "list_stories";
constexpr char kLoginGuestCommandString[] = "login_guest";
constexpr char kRemoveModCommandString[] = "remove_mod";
constexpr char kRestartSessionCommandString[] = "restart_session";
constexpr char kHelpCommandString[] = "help";

// Flags to pass to SessionCtlApp.
constexpr char kJsonOutFlagString[] = "json_out";
constexpr char kFocusModFlagString[] = "focus_mod";
constexpr char kFocusStoryFlagString[] = "focus_story";
constexpr char kModNameFlagString[] = "mod_name";
constexpr char kModUrlFlagString[] = "mod_url";
constexpr char kStoryIdFlagString[] = "story_id";
constexpr char kStoryNameFlagString[] = "story_name";
constexpr char kWaitForSessionFlagString[] = "wait_for_session";

// Internal error string returned from SessionCtlApp.ExecuteCommand() if
// the user does not set a required flag.
constexpr char kGetUsageErrorString[] = "GetUsage";

// Fuchsia package paths for add_mod
constexpr char kFuchsiaPkgPrefix[] = "fuchsia-pkg://";
constexpr char kFuchsiaPkgPath[] = "fuchsia-pkg://fuchsia.com/%s#meta/%s.cmx";

// hub paths to debug services.
constexpr char kSessionCtlServiceGlobPath[] =
    "/hub/c/sessionmgr.cmx/*/out/debug/sessionctl";
constexpr char kBasemgrDebugServiceGlobPath[] =
    "/hub/c/basemgr.cmx/*/out/debug/basemgr";
}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONCTL_SESSION_CTL_CONSTANTS_H_
