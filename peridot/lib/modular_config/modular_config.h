// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_
#define PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>

namespace modular {

class ModularConfigReader {
 public:
  explicit ModularConfigReader();
  ~ModularConfigReader();

  // Reads basemgr configs from startup.config and parses it into a fidl table
  fuchsia::modular::session::BasemgrConfig GetBasemgrConfig();

  // Reads sessionmgr configs from startup.config and parses it into a fidl
  // table
  fuchsia::modular::session::SessionmgrConfig GetSessionmgrConfig();

  // Returns a SessionmgrConfig with all default values
  fuchsia::modular::session::SessionmgrConfig GetDefaultSessionmgrConfig();

 private:
  // Reads startup.config into a string if the file exists. Otherwise, returns
  // a string containing two quotes, representing an empty JSON structure.
  //
  // |config_name| is the field in JSON to be extracted, such as "basemgr".
  // |config_path| is the full path to the config file, such as
  // "/config/data/startup.config".
  std::string GetConfigAsString(const std::string& config_name,
                                std::string config_path);
};

}  // namespace modular

#endif  // PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_