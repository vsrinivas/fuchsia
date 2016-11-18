// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_APPLICATION_MANAGER_CONFIG_H_
#define MOJO_APPLICATION_MANAGER_CONFIG_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "apps/modular/services/application/application_launcher.fidl.h"
#include "lib/ftl/macros.h"

namespace modular {

// The configuration file should be specified as:
// {
//   "initial-apps": [
//     "file:///system/apps/app_without_args",
//     [ "file:///system/apps/app_with_args", "arg1", "arg2", "arg3" ]
//   ],
//   "path": [
//     "/system/apps"
//   ],
//   "include": [
//     "/system/data/application_manager/startup.config"
//   ]
// }

class Config {
 public:
  Config();
  ~Config();

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

  FTL_DISALLOW_COPY_AND_ASSIGN(Config);
};

}  // namespace modular

#endif  // MOJO_APPLICATION_MANAGER_CONFIG_H_
