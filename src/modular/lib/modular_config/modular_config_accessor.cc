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
