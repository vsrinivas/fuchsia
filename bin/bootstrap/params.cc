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

constexpr const char kDefaultConfigFile[] =
    "/system/data/bootstrap/services.config";
constexpr const char kDefaultLabel[] = "boot";

}  // namespace

bool Params::Setup(const ftl::CommandLine& command_line) {
  // --no-config / --config=<config-file>
  if (!command_line.HasOption("no-config")) {
    std::string config_file;
    if (!command_line.GetOptionValue("config", &config_file))
      config_file = kDefaultConfigFile;
    if (!config_file.empty()) {
      FTL_LOG(INFO) << "Loading configuration file from " << config_file;
      Config config;
      if (!config.ReadFrom(config_file)) {
        FTL_LOG(ERROR) << "Could not parse config file";
        return false;
      }
      services_ = config.TakeServices();
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
      auto launch_info = modular::ApplicationLaunchInfo::New();
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
    initial_launch_ = modular::ApplicationLaunchInfo::New();
    initial_launch_->url = positional_args[0];
    for (size_t i = 1; i < positional_args.size(); ++i)
      initial_launch_->arguments.push_back(positional_args[i]);
  } else if (positional_args.size() > 1) {
    FTL_LOG(ERROR) << "Found excess arguments after '-'";
    return false;
  }
  return true;
}

}  // namespace bootstrap
