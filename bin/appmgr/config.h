// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_CONFIG_H_
#define GARNET_BIN_APPMGR_CONFIG_H_

#include <string>
#include <vector>

#include "lib/app/fidl/application_launcher.fidl.h"
#include "lib/fxl/macros.h"

namespace app {

// The configuration file should be specified as:
// {
//   "initial-apps": [
//     "app_without_args",
//     [ "app_with_args", "arg1", "arg2", "arg3" ]
//   ],
//   "path": [
//     "/system/apps"
//   ],
//   "include": [
//     "/system/data/appmgr/startup.config"
//   ]
// }

class Config {
 public:
  Config() = default;
  ~Config() = default;

  bool ReadIfExistsFrom(const std::string& config_file);

  // Gets path for finding apps on root file system.
  std::vector<std::string> TakePath();

  // Gets initial apps to launch.
  std::vector<ApplicationLaunchInfoPtr> TakeInitialApps();

 private:
  bool Parse(const std::string& string);
  bool ReadFromIfExists(const std::string& config_file);

  std::vector<std::string> path_;
  std::vector<ApplicationLaunchInfoPtr> initial_apps_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Config);
};

}  // namespace app

#endif  // GARNET_BIN_APPMGR_CONFIG_H_
