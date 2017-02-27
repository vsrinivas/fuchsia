// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_CONFIG_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_CONFIG_H_

#include <string>
#include <unordered_map>
#include <utility>

#include "application/services/application_launcher.fidl.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/ftl/macros.h"

namespace view_manager {

class Config {
 public:
  using AssociatesVector = std::vector<app::ApplicationLaunchInfoPtr>;

  Config();
  ~Config();

  bool ReadFrom(const std::string& config_file);

  bool Parse(const std::string& string);

  AssociatesVector TakeAssociates() { return std::move(associates_); }

 private:
  app::ApplicationLaunchInfoPtr GetLaunchInfo(const modular::JsonValue& value);

  AssociatesVector associates_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Config);
};

}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_CONFIG_H_
