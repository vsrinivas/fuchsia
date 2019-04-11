// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_
#define PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>

namespace modular {

class ModularConfigReader {
 public:
  explicit ModularConfigReader();
  ~ModularConfigReader();

  // Reads basemgr configs from startup.config and parses it into a fidl table
  fuchsia::modular::internal::BasemgrConfig GetBasemgrConfig();

  // Reads sessionmgr configs from startup.config and parses it into a fidl
  // table
  fuchsia::modular::internal::SessionmgrConfig GetSessionmgrConfig();

 private:
  // Reads startup.config into a string if the file exists. Otherwise, returns a
  // string containing two quotes, representing an empty JSON structure.
  std::string GetConfigAsString(const std::string& config_name);
};

}  // namespace modular

#endif  // PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_H_