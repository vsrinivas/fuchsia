// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_
#define SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_

#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fit/result.h>

#include <fbl/unique_fd.h>
#include <rapidjson/reader.h>

#include "src/lib/json_parser/json_parser.h"

namespace modular {

// Parse Modular configuration from JSON into a FIDL table.
//
// Returns either the parsed configuration or an error string.
fit::result<fuchsia::modular::session::ModularConfig, std::string> ParseConfig(
    std::string_view config_json);

// Returns the default Modular configuration.
fuchsia::modular::session::ModularConfig DefaultConfig();

// Returns the Modular configuration as a JSON string.
std::string ConfigToJsonString(const fuchsia::modular::session::ModularConfig& config);

// A utility for parsing a modular configuration file.
//
// Use |ModularConfigReader::CreateFromNamespace()| to read modular
// configuration from this component's incoming namespace.
class ModularConfigReader {
 public:
  // Looks for the modular config file by searching |root_fd| for the
  // following paths, in order, within the incoming namespace until it finds a
  // path that exists.
  //
  //  /config_override/data/startup.config
  //  /config/data/startup.config
  //
  // If one doesn't exist, uses defaults.
  explicit ModularConfigReader(fbl::unique_fd dir_fd);

  // Parses |config| into modular configs. If |config| cannot be parsed,
  // defaults will be used.
  explicit ModularConfigReader(std::string config);

  ~ModularConfigReader() = default;

  // Returns a ModularConfigReader which sources the config file from the
  // incoming namespace.
  static ModularConfigReader CreateFromNamespace();

  // Returns the overridden config path.
  static std::string GetOverriddenConfigPath();

  // Returns true if configurations exist at the overridden config path.
  static bool OverriddenConfigExists();

  // Returns the parsed `basemgr` section of the config.
  fuchsia::modular::session::BasemgrConfig GetBasemgrConfig() const;

  // Returns the parsed `sessionmgr` section of the config.
  fuchsia::modular::session::SessionmgrConfig GetSessionmgrConfig() const;

  // Returns the given configuration as a JSON formatted string.
  static std::string GetConfigAsString(
      fuchsia::modular::session::BasemgrConfig* basemgr_config,
      fuchsia::modular::session::SessionmgrConfig* sessionmgr_config);

 private:
  // Parses |config| into |basemgr_config_| and |sessionmgr_config_|.
  //
  // |config| The configuration as a JSON string.
  //
  // |config_path| The filesystem path to the config file, if it was read from a file.
  //    This is only used for logging error messages.
  void ParseConfig(const std::string& config, const std::string& config_path);

  // Sets |basemgr_config_| and |sessionmgr_config_| to default values.
  void UseDefaults();

  fuchsia::modular::session::SessionmgrConfig sessionmgr_config_;
  fuchsia::modular::session::BasemgrConfig basemgr_config_;
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_
