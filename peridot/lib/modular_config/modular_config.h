// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_
#define PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_

#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <src/lib/files/unique_fd.h>

#include "lib/json/json_parser.h"

namespace modular {

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
  explicit ModularConfigReader(fxl::UniqueFD dir_fd);

  // Parses |config| into modular configs. If |config| cannot be parsed,
  // defaults will be used.
  explicit ModularConfigReader(std::string config, std::string config_path);
  ~ModularConfigReader();

  // Returns a ModularConfigReader which sources the config file from the
  // incoming namespace.
  static ModularConfigReader CreateFromNamespace();

  // Parses |doc| into |basemgr_config_| and |sessionmgr_config_|.
  void ParseConfig(json::JSONParser json_parser, rapidjson::Document doc,
                   std::string config_path);

  // Returns the parsed `basemgr` section of the config.
  fuchsia::modular::session::BasemgrConfig GetBasemgrConfig() const;

  // Returns the parsed `sessionmgr` section of the config.
  fuchsia::modular::session::SessionmgrConfig GetSessionmgrConfig() const;

  // Returns a SessionmgrConfig with all default values
  fuchsia::modular::session::SessionmgrConfig GetDefaultSessionmgrConfig()
      const;

 private:
  fuchsia::modular::session::SessionmgrConfig sessionmgr_config_;
  fuchsia::modular::session::BasemgrConfig basemgr_config_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_
