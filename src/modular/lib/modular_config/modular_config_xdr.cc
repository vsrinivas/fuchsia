// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_config/modular_config_xdr.h"

#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular {
namespace {

void XdrSessionShellMapEntry(XdrContext* const xdr,
                             fuchsia::modular::session::SessionShellMapEntry* const data) {
  auto* config = data->mutable_config();

  xdr->Field(modular_config::kUrl, config->mutable_app_config()->mutable_url());

  bool has_args = config->mutable_app_config()->has_args();
  xdr->FieldWithDefault(modular_config::kArgs, config->mutable_app_config()->mutable_args(),
                        has_args, std::vector<std::string>());
}

void XdrAppConfig(XdrContext* const xdr, fuchsia::modular::session::AppConfig* const data) {
  xdr->Field(modular_config::kUrl, data->mutable_url());

  bool has_args = data->has_args();
  std::vector<std::string> default_args;
  xdr->FieldWithDefault(modular_config::kArgs, data->mutable_args(), has_args, default_args);
}

void XdrComponentArgs(XdrContext* const xdr, fuchsia::modular::session::AppConfig* const data) {
  // TODO(fxbug.dev/55895): component_args should use "url" instead of "uri"
  xdr->Field(modular_config::kUri, data->mutable_url());

  bool has_args = data->has_args();
  std::vector<std::string> default_args;
  xdr->FieldWithDefault(modular_config::kArgs, data->mutable_args(), has_args, default_args);
}

void XdrAgentServiceIndexEntry(XdrContext* const xdr,
                               fuchsia::modular::session::AgentServiceIndexEntry* const data) {
  xdr->Field(modular_config::kServiceName, data->mutable_service_name());
  xdr->Field(modular_config::kAgentUrl, data->mutable_agent_url());
  if (xdr->HasField(modular_config::kExposeFrom, data->has_expose_from())) {
    xdr->Field(modular_config::kExposeFrom, data->mutable_expose_from());
  } else {
    data->clear_expose_from();
  }
}

void XdrV2ModularAgentsEntry(XdrContext* const xdr,
                             fuchsia::modular::session::V2ModularAgentsEntry* const data) {
  xdr->Field(modular_config::kServiceName, data->mutable_service_name());
  xdr->Field(modular_config::kAgentUrl, data->mutable_agent_url());
}

fuchsia::modular::session::BasemgrConfig GetDefaultBasemgrConfig() {
  rapidjson::Document doc;
  doc.SetObject();

  fuchsia::modular::session::BasemgrConfig config;
  auto ok = XdrRead(&doc, &config, XdrBasemgrConfig);
  FX_DCHECK(ok);

  return config;
}

fuchsia::modular::session::SessionmgrConfig GetDefaultSessionmgrConfig() {
  rapidjson::Document doc;
  doc.SetObject();

  fuchsia::modular::session::SessionmgrConfig config;
  auto ok = XdrRead(&doc, &config, XdrSessionmgrConfig);
  FX_DCHECK(ok);

  return config;
}

}  // namespace

void XdrModularConfig_v1(XdrContext* const xdr,
                         fuchsia::modular::session::ModularConfig* const data) {
  bool has_basemgr_config = data->has_basemgr_config();
  xdr->FieldWithDefault(modular_config::kBasemgrConfigName, data->mutable_basemgr_config(),
                        XdrBasemgrConfig_v1, has_basemgr_config, GetDefaultBasemgrConfig());

  bool has_sessionmgr_config = data->has_sessionmgr_config();
  xdr->FieldWithDefault(modular_config::kSessionmgrConfigName, data->mutable_sessionmgr_config(),
                        XdrSessionmgrConfig_v1, has_sessionmgr_config,
                        GetDefaultSessionmgrConfig());
}

void XdrBasemgrConfig_v1(XdrContext* const xdr,
                         fuchsia::modular::session::BasemgrConfig* const data) {
  bool has_enable_cobalt = data->has_enable_cobalt();
  xdr->FieldWithDefault(modular_config::kEnableCobalt, data->mutable_enable_cobalt(),
                        has_enable_cobalt, true);

  bool has_use_session_shell_for_story_shell_factory =
      data->has_use_session_shell_for_story_shell_factory();
  xdr->FieldWithDefault(modular_config::kUseSessionShellForStoryShellFactory,
                        data->mutable_use_session_shell_for_story_shell_factory(),
                        has_use_session_shell_for_story_shell_factory, false);

  std::vector<fuchsia::modular::session::SessionShellMapEntry> default_session_shell_map;
  bool has_session_shell_map = data->has_session_shell_map();
  xdr->FieldWithDefault(modular_config::kSessionShells, data->mutable_session_shell_map(),
                        XdrSessionShellMapEntry, has_session_shell_map,
                        std::move(default_session_shell_map));

  bool has_story_shell_url = false;
  if (data->has_story_shell()) {
    if (data->story_shell().has_app_config()) {
      has_story_shell_url = data->story_shell().app_config().has_url();
    }
  }
  if (xdr->HasField(modular_config::kStoryShellUrl, has_story_shell_url)) {
    xdr->Field(modular_config::kStoryShellUrl,
               data->mutable_story_shell()->mutable_app_config()->mutable_url());
    if (xdr->op() == XdrOp::FROM_JSON) {
      data->mutable_story_shell()->mutable_app_config()->set_args({});
    }
  } else {
    data->clear_story_shell();
  }

  if (xdr->HasField(modular_config::kSessionLauncher, data->has_session_launcher())) {
    xdr->Field(modular_config::kSessionLauncher, data->mutable_session_launcher(), XdrAppConfig);
  } else {
    data->clear_session_launcher();
  }
}

void XdrSessionmgrConfig_v1(XdrContext* const xdr,
                            fuchsia::modular::session::SessionmgrConfig* const data) {
  bool has_enable_cobalt = data->has_enable_cobalt();
  xdr->FieldWithDefault(modular_config::kEnableCobalt, data->mutable_enable_cobalt(),
                        has_enable_cobalt, true);

  std::vector<std::string> default_agents;
  bool has_startup_agents = data->has_startup_agents();
  xdr->FieldWithDefault(modular_config::kStartupAgents, data->mutable_startup_agents(),
                        has_startup_agents, default_agents);

  bool has_session_agents = data->has_session_agents();
  xdr->FieldWithDefault(modular_config::kSessionAgents, data->mutable_session_agents(),
                        has_session_agents, default_agents);

  bool has_restart_session_on_agent_crash = data->has_restart_session_on_agent_crash();
  xdr->FieldWithDefault(modular_config::kRestartSessionOnAgentCrash,
                        data->mutable_restart_session_on_agent_crash(),
                        has_restart_session_on_agent_crash, default_agents);

  std::vector<fuchsia::modular::session::AppConfig> default_component_args;
  bool has_component_args = data->has_component_args();
  xdr->FieldWithDefault(modular_config::kComponentArgs, data->mutable_component_args(),
                        XdrComponentArgs, has_component_args, std::move(default_component_args));

  std::vector<fuchsia::modular::session::AgentServiceIndexEntry> default_agent_service_index;
  bool has_agent_service_index = data->has_agent_service_index();
  xdr->FieldWithDefault(modular_config::kAgentServiceIndex, data->mutable_agent_service_index(),
                        XdrAgentServiceIndexEntry, has_agent_service_index,
                        std::move(default_agent_service_index));

  std::vector<fuchsia::modular::session::V2ModularAgentsEntry> default_v2_modular_agents;
  bool has_v2_modular_agents = data->has_v2_modular_agents();
  xdr->FieldWithDefault(modular_config::kV2ModularAgents, data->mutable_v2_modular_agents(),
                        XdrV2ModularAgentsEntry, has_v2_modular_agents,
                        std::move(default_v2_modular_agents));

  bool has_disable_agent_restart_on_crash = data->has_disable_agent_restart_on_crash();
  xdr->FieldWithDefault(modular_config::kDisableAgentRestartOnCrash,
                        data->mutable_disable_agent_restart_on_crash(),
                        has_disable_agent_restart_on_crash, false);

  bool has_present_mods_as_stories = data->has_present_mods_as_stories();
  xdr->FieldWithDefault(modular_config::kPresentModsAsStories,
                        data->mutable_present_mods_as_stories(), has_present_mods_as_stories,
                        false);
}

}  // namespace modular
