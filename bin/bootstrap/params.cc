// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/bootstrap/params.h"

#include "apps/modular/src/bootstrap/config.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"

namespace bootstrap {
namespace {

constexpr const char kDefaultServicesConfigFile[] =
    "/system/data/bootstrap/services.config";
constexpr const char kDefaultAppsConfigFile[] =
    "/system/data/bootstrap/apps.config";
constexpr const char kDefaultLabel[] = "boot";

}  // namespace

bool Params::Setup(const ftl::CommandLine& command_line) {
  // --no-config / --config=<config-file>
  if (!command_line.HasOption("no-config")) {
    std::string config_file;
    if (!command_line.GetOptionValue("services", &config_file))
      config_file = kDefaultServicesConfigFile;
    if (!config_file.empty()) {
      FTL_LOG(INFO) << "Loading services from " << config_file;
      Config config;
      if (config.ReadFrom(config_file)) {
        services_ = config.TakeServices();
      } else {
        FTL_LOG(WARNING) << "Could not parse " << config_file;
    }
    }

    if (!command_line.GetOptionValue("apps", &config_file))
      config_file = kDefaultAppsConfigFile;
    if (!config_file.empty()) {
      FTL_LOG(INFO) << "Loading apps from " << config_file;
      Config config;
      if (config.ReadFrom(config_file)) {
        apps_ = config.TakeApps();
      } else {
        FTL_LOG(WARNING) << "Could not parse " << config_file;
      }
    }
  }

  // --reg=<service name>@<app url>,...
  std::string option;
  if (command_line.GetOptionValue("reg", &option)) {
    for (auto reg : ftl::SplitString(option, ",", ftl::kTrimWhitespace,
                                     ftl::kSplitWantNonEmpty)) {
      auto split = ftl::SplitString(reg, "@", ftl::kTrimWhitespace,
                                    ftl::kSplitWantNonEmpty);
      if (split.size() != 2) {
        FTL_LOG(ERROR) << "Invalid --reg argument";
        return false;
      }
      auto launch_info = app::ApplicationLaunchInfo::New();
      launch_info->url = split[1].ToString();
      services_.emplace(split[0].ToString(), std::move(launch_info));
    }
  }

  // --label=<name>
  if (!command_line.GetOptionValue("label", &label_) || label_.empty()) {
    label_ = kDefaultLabel;
  }

  // positional args
  const auto& positional_args = command_line.positional_args();
  if (positional_args.empty()) {
    FTL_LOG(ERROR) << "Must specify application to run or '-' if none";
    return false;
  }

  if (positional_args[0] != "-") {
    auto app = app::ApplicationLaunchInfo::New();
    app->url = positional_args[0];
    for (size_t i = 1; i < positional_args.size(); ++i)
      app->arguments.push_back(positional_args[i]);
    apps_.push_back(std::move(app));
  } else if (positional_args.size() > 1) {
    FTL_LOG(ERROR) << "Found excess arguments after '-'";
    return false;
  }
  return true;
}

}  // namespace bootstrap
