// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_config/modular_config_accessor.h"

#include <lib/syslog/cpp/macros.h>

#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular {

ModularConfigAccessor::ModularConfigAccessor(fuchsia::modular::session::ModularConfig config)
    : config_(std::move(config)) {}

bool ModularConfigAccessor::use_random_session_id() const {
  // If the |auto_login_to_guest| build flag is set, ensure stable session IDs.
  if (kUseStableSessionId) {
    FX_LOGS(INFO) << "Requesting stable session ID based on build flag";
    return false;
  }

  // Use the default of a random session ID unless the configuration requested persistence.
  // TODO(fxbug.dev/51752): Change base manager config to use a more direct declaration of persistence
  // and remove the base shell configuration entirely.
  FX_DCHECK(basemgr_config().has_base_shell());
  FX_DCHECK(basemgr_config().base_shell().has_app_config());
  const auto& app_config = basemgr_config().base_shell().app_config();
  if (app_config.has_args()) {
    const auto& args = app_config.args();

    // Use a random session ID if the args do not contain `--persist-user`.
    return std::find(args.begin(), args.end(), modular_config::kPersistUserArg) == args.end();
  }

  return true;
}

const fuchsia::modular::session::AppConfig& ModularConfigAccessor::session_shell_app_config()
    const {
  auto shell_count =
      basemgr_config().has_session_shell_map() ? basemgr_config().session_shell_map().size() : 0;
  FX_DCHECK(shell_count > 0);

  const auto& session_shell_app_config =
      basemgr_config().session_shell_map().at(0).config().app_config();
  if (shell_count > 1) {
    FX_LOGS(WARNING) << "More than one session shell config defined, using first in list: "
                     << session_shell_app_config.url();
  }

  return session_shell_app_config;
}

std::string ModularConfigAccessor::GetConfigAsJsonString() const {
  return modular::ConfigToJsonString(config_);
}

}  // namespace modular
