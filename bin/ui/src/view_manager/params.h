// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_PARAMS_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_PARAMS_H_

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "application/services/application_launcher.fidl.h"
#include "lib/ftl/command_line.h"

namespace view_manager {

class Params {
 public:
  using AssociatesVector = std::vector<app::ApplicationLaunchInfoPtr>;

  bool Setup(const ftl::CommandLine& command_line);

  AssociatesVector TakeAssociates() { return std::move(associates_); }

  bool use_composer2() const { return use_composer2_; }

 private:
  AssociatesVector associates_;
  bool use_composer2_ = false;
};

}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_PARAMS_H_
