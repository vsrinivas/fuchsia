// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_CONSTANTS_H_
#define PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_CONSTANTS_H_

namespace modular_config {

inline constexpr char kBasemgrConfigName[] = "basemgr";
inline constexpr char kSessionmgrConfigName[] = "sessionmgr";
inline constexpr char kStartupConfigPath[] = "/config/data/startup.config";
inline constexpr char kTrue[] = "true";

// Presentation constants
inline constexpr char kDisplayUsage[] = "display_usage";
inline constexpr char kHandheld[] = "handheld";
inline constexpr char kClose[] = "close";
inline constexpr char kNear[] = "near";
inline constexpr char kMidrange[] = "midrange";
inline constexpr char kFar[] = "far";
inline constexpr char kUnknown[] = "unknown";
inline constexpr char kScreenHeight[] = "screen_height";
inline constexpr char kScreenWidth[] = "screen_width";

// Cloud provider constants
inline constexpr char kCloudProvider[] = "cloud_provider";
inline constexpr char kLetLedgerDecide[] = "LET_LEDGER_DECIDE";
inline constexpr char kFromEnvironment[] = "FROM_ENVIRONMENT";
inline constexpr char kNone[] = "NONE";

// Basemgr constants
inline constexpr char kEnableCobalt[] = "enable_cobalt";
inline constexpr char kEnablePresenter[] = "enable_presenter";
inline constexpr char kTest[] = "test";
inline constexpr char kUseMinfs[] = "use_minfs";
inline constexpr char kUseSessionShellForStoryShellFactory[] =
    "use_session_shell_for_story_shell_factory";

// Sessionmgr constants
inline constexpr char kEnableStoryShellPreload[] = "enable_story_shell_preload";
inline constexpr char kStartupAgents[] = "startup_agents";
inline constexpr char kSessionAgents[] = "session_agents";
inline constexpr char kUseMemfsForLedger[] = "use_memfs_for_ledger";

// Shell constants
inline constexpr char kDefaultBaseShellUrl[] =
    "fuchsia-pkg://fuchsia.com/dev_base_shell#meta/dev_base_shell.cmx";
inline constexpr char kDefaultSessionShellUrl[] =
    "fuchsia-pkg://fuchsia.com/ermine_session_shell#meta/"
    "ermine_session_shell.cmx";
inline constexpr char kDefaultStoryShellUrl[] =
    "fuchsia-pkg://fuchsia.com/mondrian#meta/mondrian.cmx";
inline constexpr char kBaseShell[] = "base_shell";
inline constexpr char kSessionShells[] = "session_shells";
inline constexpr char kStoryShellUrl[] = "story_shell_url";
inline constexpr char kUrl[] = "url";
inline constexpr char kKeepAliveAfterLogin[] = "keep_alive_after_login";

// Cloud provider constants. These will be deprecated.
inline constexpr char kNoCloudProviderForLedger[] =
    "no_cloud_provider_for_ledger";
inline constexpr char kUseCloudProviderFromEnvironment[] =
    "use_cloud_provider_from_environment";

}  // namespace modular_config

#endif  // PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_CONSTANTS_H_