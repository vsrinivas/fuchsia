// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_ACCESSOR_H_
#define SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_ACCESSOR_H_

#include <fuchsia/modular/session/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <fbl/macros.h>

namespace modular {

// Set with the |auto_login_to_guest| build flag.
#ifdef AUTO_LOGIN_TO_GUEST
constexpr bool kUseStableSessionId = true;
#else
constexpr bool kUseStableSessionId = false;
#endif

class ModularConfigAccessor {
 public:
  explicit ModularConfigAccessor(fuchsia::modular::session::ModularConfig config);
  ModularConfigAccessor(ModularConfigAccessor&& other) = default;
  ModularConfigAccessor& operator=(ModularConfigAccessor&& other) = default;

  const fuchsia::modular::session::ModularConfig& config() const { return config_; }
  const fuchsia::modular::session::BasemgrConfig& basemgr_config() const {
    FX_DCHECK(config_.has_basemgr_config());
    return config_.basemgr_config();
  }
  const fuchsia::modular::session::SessionmgrConfig& sessionmgr_config() const {
    FX_DCHECK(config_.has_sessionmgr_config());
    return config_.sessionmgr_config();
  }
  const fuchsia::modular::session::AppConfig& story_shell_app_config() const {
    FX_DCHECK(basemgr_config().has_story_shell());
    FX_DCHECK(basemgr_config().story_shell().has_app_config());
    return basemgr_config().story_shell().app_config();
  }
  bool use_session_shell_for_story_shell_factory() const {
    FX_DCHECK(basemgr_config().has_use_session_shell_for_story_shell_factory());
    return basemgr_config().use_session_shell_for_story_shell_factory();
  }

  const fuchsia::modular::session::AppConfig& session_shell_app_config() const;
  bool use_random_session_id() const;
  bool enable_cobalt() const {
    return sessionmgr_config().has_enable_cobalt() && sessionmgr_config().enable_cobalt();
  }

  // Returns the ModularConfig serialized as a JSON string.
  std::string GetConfigAsJsonString() const;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ModularConfigAccessor);

 private:
  fuchsia::modular::session::ModularConfig config_;
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_ACCESSOR_H_
