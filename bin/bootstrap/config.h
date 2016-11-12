// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_BOOTSTRAP_CONFIG_H_
#define APPS_MODULAR_SRC_BOOTSTRAP_CONFIG_H_

#include <string>
#include <unordered_map>
#include <utility>

#include "apps/modular/services/application/application_launcher.fidl.h"
#include "lib/ftl/macros.h"

namespace bootstrap {

// The configuration file should be specified as:
// {
//   "services": {
//     "service-name-1": "file:///system/apps/app_without_args",
//     "service-name-2": [
//        "file:///system/apps/app_with_args", "arg1", "arg2", "arg3"
//     ]
//   }
// }
//
// TODO(jeffbrown): Support chaining multiple configuration files together
// via imports.

class Config {
 public:
  using ServiceMap =
      std::unordered_map<std::string, modular::ApplicationLaunchInfoPtr>;

  Config();
  ~Config();

  bool ReadFrom(const std::string& config_file);
  bool Parse(const std::string& string);
  ServiceMap TakeServices() { return std::move(services_); }

 private:
  ServiceMap services_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Config);
};

}  // namespace bootstrap

#endif  // APPS_MODULAR_SRC_BOOTSTRAP_CONFIG_H_
