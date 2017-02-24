// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_BOOTSTRAP_PARAMS_H_
#define APPS_MODULAR_SRC_BOOTSTRAP_PARAMS_H_

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "application/services/application_launcher.fidl.h"
#include "lib/ftl/command_line.h"

namespace bootstrap {

class Params {
 public:
  using ServiceMap =
      std::unordered_map<std::string, app::ApplicationLaunchInfoPtr>;
  using AppVector = std::vector<app::ApplicationLaunchInfoPtr>;

  bool Setup(const ftl::CommandLine& command_line);

  std::string label() const { return label_; }

  ServiceMap TakeServices() { return std::move(services_); }
  AppVector TakeApps() { return std::move(apps_); }

 private:
  ServiceMap services_;
  AppVector apps_;
  std::string label_;
};

}  // namespace bootstrap

#endif  // APPS_MODULAR_SRC_BOOTSTRAP_PARAMS_H_
