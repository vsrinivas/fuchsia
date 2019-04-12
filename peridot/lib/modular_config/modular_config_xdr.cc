// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/modular_config/modular_config_xdr.h"

#include "peridot/lib/modular_config/modular_config_constants.h"

namespace modular {
namespace {

void XdrBaseShellConfig(
    XdrContext* const xdr,
    fuchsia::modular::internal::BaseShellConfig* const data) {
  bool has_url = false;
  if (data->has_app_config()) {
    has_url = data->app_config().has_url();
  }
  xdr->FieldWithDefault(modular_config::kUrl,
                        data->mutable_app_config()->mutable_url(), has_url,
                        std::string(modular_config::kDefaultBaseShellUrl));
  FXL_LOG(INFO) << data->app_config().url();
  data->mutable_app_config()->set_args(std::vector<std::string>());

  bool has_keep_alive_after_login = data->has_keep_alive_after_login();
  xdr->FieldWithDefault(modular_config::kKeepAliveAfterLogin,
                        data->mutable_keep_alive_after_login(),
                        has_keep_alive_after_login, false);
}

fuchsia::ui::policy::DisplayUsage GetDisplayUsageFromString(std::string usage) {
  if (usage == modular_config::kHandheld) {
    return fuchsia::ui::policy::DisplayUsage::kHandheld;
  } else if (usage == modular_config::kClose) {
    return fuchsia::ui::policy::DisplayUsage::kClose;
  } else if (usage == modular_config::kNear) {
    return fuchsia::ui::policy::DisplayUsage::kNear;
  } else if (usage == modular_config::kMidrange) {
    return fuchsia::ui::policy::DisplayUsage::kMidrange;
  } else if (usage == modular_config::kFar) {
    return fuchsia::ui::policy::DisplayUsage::kFar;
  }

  FXL_LOG(WARNING) << "Unknown display usage string: " << usage;
  return fuchsia::ui::policy::DisplayUsage::kUnknown;
}

std::string GetDisplayUsageAsString(fuchsia::ui::policy::DisplayUsage usage) {
  switch (usage) {
    case fuchsia::ui::policy::DisplayUsage::kUnknown:
      return modular_config::kUnknown;
    case fuchsia::ui::policy::DisplayUsage::kHandheld:
      return modular_config::kHandheld;
    case fuchsia::ui::policy::DisplayUsage::kClose:
      return modular_config::kClose;
    case fuchsia::ui::policy::DisplayUsage::kNear:
      return modular_config::kNear;
    case fuchsia::ui::policy::DisplayUsage::kMidrange:
      return modular_config::kMidrange;
    case fuchsia::ui::policy::DisplayUsage::kFar:
      return modular_config::kFar;
  }
}

void XdrSessionShellMapEntry(
    XdrContext* const xdr,
    fuchsia::modular::internal::SessionShellMapEntry* const data) {
  bool has_name = data->has_name();
  xdr->FieldWithDefault(modular_config::kUrl, data->mutable_name(), has_name,
                        std::string(modular_config::kDefaultSessionShellUrl));
  data->mutable_config()->mutable_app_config()->set_url(data->name());
  data->mutable_config()->mutable_app_config()->set_args(
      std::vector<std::string>());

  bool has_display_usage = false;
  bool has_screen_height = false;
  bool has_screen_width = false;
  if (data->has_config()) {
    has_display_usage = data->config().has_display_usage();
    has_screen_height = data->config().has_screen_height();
    has_screen_width = data->config().has_screen_width();
  }

  std::string display_usage_str = modular_config::kUnknown;
  if (has_display_usage) {
    display_usage_str = GetDisplayUsageAsString(data->config().display_usage());
  }

  // We need to manually parse any field in JSON that is a represented as a fidl
  // enum because XDR expects a number, rather than a string, for enums.
  // If writing, this will set the value of "display_usage" in JSON as the
  // value of |display_usage|. If reading, this will read the value of
  // "display_usage" into |display_usage|.
  std::string display_usage;
  xdr->FieldWithDefault(modular_config::kDisplayUsage, &display_usage, false,
                        display_usage_str);

  // This is only used when reading. We want to set the value read into
  // |display_usage| into |data|.
  auto display_usage_fidl = GetDisplayUsageFromString(display_usage);
  data->mutable_config()->set_display_usage(display_usage_fidl);

  xdr->FieldWithDefault(modular_config::kScreenHeight,
                        data->mutable_config()->mutable_screen_height(),
                        has_screen_height, static_cast<float>(0));

  xdr->FieldWithDefault(modular_config::kScreenWidth,
                        data->mutable_config()->mutable_screen_width(),
                        has_screen_width, static_cast<float>(0));
}

std::vector<fuchsia::modular::internal::SessionShellMapEntry>
GetDefaultSessionShellMap() {
  fuchsia::modular::internal::SessionShellConfig config;
  config.mutable_app_config()->set_url(modular_config::kDefaultSessionShellUrl);
  config.mutable_app_config()->set_args(std::vector<std::string>());
  config.set_display_usage(fuchsia::ui::policy::DisplayUsage::kUnknown);
  config.set_screen_height(0);
  config.set_screen_width(0);

  fuchsia::modular::internal::SessionShellMapEntry map_entry;
  map_entry.set_name(modular_config::kDefaultSessionShellUrl);
  map_entry.set_config(std::move(config));

  std::vector<fuchsia::modular::internal::SessionShellMapEntry>
      session_shell_map(1);
  session_shell_map.at(0) = std::move(map_entry);

  return session_shell_map;
}

fuchsia::modular::internal::BaseShellConfig GetDefaultBaseShellConfig() {
  fuchsia::modular::internal::BaseShellConfig base_shell_config;
  base_shell_config.mutable_app_config()->set_url(
      modular_config::kDefaultBaseShellUrl);
  base_shell_config.mutable_app_config()->set_args(std::vector<std::string>());
  base_shell_config.set_keep_alive_after_login(false);

  return base_shell_config;
}

fuchsia::modular::internal::CloudProvider GetCloudProviderFromString(
    std::string provider) {
  if (provider == modular_config::kFromEnvironment) {
    return fuchsia::modular::internal::CloudProvider::FROM_ENVIRONMENT;
  } else if (provider == modular_config::kNone) {
    return fuchsia::modular::internal::CloudProvider::NONE;
  }

  return fuchsia::modular::internal::CloudProvider::LET_LEDGER_DECIDE;
}

std::string GetCloudProviderAsString(
    fuchsia::modular::internal::CloudProvider provider) {
  switch (provider) {
    case fuchsia::modular::internal::CloudProvider::LET_LEDGER_DECIDE:
      return modular_config::kLetLedgerDecide;
    case fuchsia::modular::internal::CloudProvider::FROM_ENVIRONMENT:
      return modular_config::kFromEnvironment;
    case fuchsia::modular::internal::CloudProvider::NONE:
      return modular_config::kNone;
  }
}

}  // namespace

void XdrBasemgrConfig_v1(
    XdrContext* const xdr,
    fuchsia::modular::internal::BasemgrConfig* const data) {
  bool has_enable_cobalt = data->has_enable_cobalt();
  xdr->FieldWithDefault(modular_config::kEnableCobalt,
                        data->mutable_enable_cobalt(), has_enable_cobalt, true);

  bool has_enable_presenter = data->has_enable_presenter();
  xdr->FieldWithDefault(modular_config::kEnablePresenter,
                        data->mutable_enable_presenter(), has_enable_presenter,
                        false);

  bool has_test = data->has_test();
  xdr->FieldWithDefault(modular_config::kTest, data->mutable_test(), has_test,
                        false);

  bool has_use_minfs = data->has_use_minfs();
  xdr->FieldWithDefault(modular_config::kUseMinfs, data->mutable_use_minfs(),
                        has_use_minfs, true);

  bool has_use_session_shell_for_story_shell_factory =
      data->has_use_session_shell_for_story_shell_factory();
  xdr->FieldWithDefault(
      modular_config::kUseSessionShellForStoryShellFactory,
      data->mutable_use_session_shell_for_story_shell_factory(),
      has_use_session_shell_for_story_shell_factory, false);

  // If no base shell is specified, all fields will be populated from the
  // default |base_shell_config|. Otherwise, the filter |XdrBaseShellConfig|
  // will fill in individual fields with default values.
  auto base_shell_config = GetDefaultBaseShellConfig();
  bool has_base_shell = data->has_base_shell();
  xdr->FieldWithDefault(modular_config::kBaseShell, data->mutable_base_shell(),
                        XdrBaseShellConfig, has_base_shell,
                        std::move(base_shell_config));

  // If no session shells are specified, a default session shell will be
  // added to |data->session_shell_map|. Otherwise, the filter
  // |XdrSessionShellMapEntry| will fill in individual fields of each session
  // shell.
  auto session_shell_config = GetDefaultSessionShellMap();
  bool has_session_shell_map = data->has_session_shell_map();
  xdr->FieldWithDefault(modular_config::kSessionShells,
                        data->mutable_session_shell_map(),
                        XdrSessionShellMapEntry, has_session_shell_map,
                        std::move(session_shell_config));

  bool has_story_shell_url = false;
  if (data->has_story_shell()) {
    if (data->story_shell().has_app_config()) {
      has_story_shell_url = data->story_shell().app_config().has_url();
    }
  }
  xdr->FieldWithDefault(
      modular_config::kStoryShellUrl,
      data->mutable_story_shell()->mutable_app_config()->mutable_url(),
      has_story_shell_url, std::string(modular_config::kDefaultStoryShellUrl));
  data->mutable_story_shell()->mutable_app_config()->set_args(
      std::vector<std::string>());
}

void XdrSessionmgrConfig_v1(
    XdrContext* const xdr,
    fuchsia::modular::internal::SessionmgrConfig* const data) {
  bool has_cloud_provider = data->has_cloud_provider();

  std::string cloud_provider_str = modular_config::kLetLedgerDecide;
  if (has_cloud_provider) {
    cloud_provider_str = GetCloudProviderAsString(data->cloud_provider());
  }

  // We need to manually parse any field in JSON that is a represented as a
  // fidl enum because XDR expects a number, rather than a string, for enums.
  // If writing, this will set the value of "cloud_provider" in JSON as the
  // value of |cloud_provider|. If reading, this will read the value of
  // "cloud_provider" into |cloud_provider|.
  std::string cloud_provider;
  xdr->FieldWithDefault(modular_config::kCloudProvider, &cloud_provider,
                        has_cloud_provider, cloud_provider_str);

  // This is only used when reading. We want to set the value read into
  // |cloud_provider| into |data|.
  auto cloud_provider_fidl = GetCloudProviderFromString(cloud_provider);
  data->set_cloud_provider(cloud_provider_fidl);

  bool has_enable_cobalt = data->has_enable_cobalt();
  xdr->FieldWithDefault(modular_config::kEnableCobalt,
                        data->mutable_enable_cobalt(), has_enable_cobalt, true);

  bool has_enable_story_shell_preload = data->has_enable_story_shell_preload();
  xdr->FieldWithDefault(modular_config::kEnableStoryShellPreload,
                        data->mutable_enable_story_shell_preload(),
                        has_enable_story_shell_preload, true);

  bool has_use_memfs_for_ledger = data->has_use_memfs_for_ledger();
  xdr->FieldWithDefault(modular_config::kUseMemfsForLedger,
                        data->mutable_use_memfs_for_ledger(),
                        has_use_memfs_for_ledger, false);

  std::vector<std::string> default_agents;
  bool has_startup_agents = data->has_startup_agents();
  xdr->FieldWithDefault(modular_config::kStartupAgents,
                        data->mutable_startup_agents(), has_startup_agents,
                        default_agents);

  bool has_session_agents = data->has_session_agents();
  xdr->FieldWithDefault(modular_config::kSessionAgents,
                        data->mutable_session_agents(), has_session_agents,
                        default_agents);
}

}  // namespace modular