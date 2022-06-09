// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_
#define SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_

#include <fuchsia/modular/session/cpp/fidl.h>
#include <lib/fpromise/result.h>

#include <fbl/unique_fd.h>

namespace modular {

// Parse Modular configuration from JSON into a FIDL table.
//
// Returns either the parsed configuration or an error string.
fpromise::result<fuchsia::modular::session::ModularConfig, std::string> ParseConfig(
    std::string_view config_json);

// Returns the default Modular configuration.
fuchsia::modular::session::ModularConfig DefaultConfig();

// Returns the Modular configuration as a JSON string.
std::string ConfigToJsonString(const fuchsia::modular::session::ModularConfig& config);

// A utility for reading Modular configuration from a directory.
//
// Use |ModularConfigReader::CreateFromNamespace()| to read modular
// configuration from this component's incoming namespace.
class ModularConfigReader {
 public:
  // Looks for the modular config file by searching |root_dir| for the
  // following paths, in order, within the incoming namespace until it finds a
  // path that exists.
  //
  //  /config_override/data/startup.config
  //  /pkg/data/startup.config
  //  /config/data/startup.config
  //
  // If one doesn't exist, uses defaults.
  explicit ModularConfigReader(fbl::unique_fd root_dir);

  ~ModularConfigReader() = default;

  // Returns a ModularConfigReader which sources the config file from the
  // incoming namespace.
  static ModularConfigReader CreateFromNamespace();

  // Returns the path to the config file in the /config/data directory.
  static std::string GetConfigDataConfigPath();

  // Returns the path to the overridden config file.
  static std::string GetOverriddenConfigPath();

  // Returns the path to the config file in the current package.
  static std::string GetPackagedConfigPath();

  // Returns true if a configuration file exists at the overridden config path.
  bool OverriddenConfigExists();

  // Returns true if a configuration file exists in the current package.
  bool PackagedConfigExists();

  // Returns the parsed `basemgr` section of the config.
  fuchsia::modular::session::BasemgrConfig GetBasemgrConfig() const;

  // Returns the parsed `sessionmgr` section of the config.
  fuchsia::modular::session::SessionmgrConfig GetSessionmgrConfig() const;

  // Returns the parsed config.
  fuchsia::modular::session::ModularConfig GetConfig() const;

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

  fbl::unique_fd root_dir_;
  fuchsia::modular::session::SessionmgrConfig sessionmgr_config_;
  fuchsia::modular::session::BasemgrConfig basemgr_config_;
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_
