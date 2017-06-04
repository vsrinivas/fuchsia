// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/params.h"

#include "apps/mozart/src/view_manager/config.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"

namespace view_manager {
namespace {

constexpr const char kDefaultAssociatesConfigFile[] =
    "/system/data/view_manager_service/associates.config";

}  // namespace

bool Params::Setup(const ftl::CommandLine& command_line) {
  // --no-config / --config=<config-file>
  if (!command_line.HasOption("no-config")) {
    std::string config_file;
    if (!command_line.GetOptionValue("associates", &config_file))
      config_file = kDefaultAssociatesConfigFile;
    if (!config_file.empty()) {
      Config config;
      if (config.ReadFrom(config_file)) {
        associates_ = config.TakeAssociates();
      } else {
        FTL_LOG(WARNING) << "Could not parse " << config_file;
      }
    }
  }
  // --use-scene-manager / --use-compositor
  if (command_line.HasOption("use-scene-manager")) {
    use_scene_manager_ = true;
  }
  return true;
}

}  // namespace view_manager
