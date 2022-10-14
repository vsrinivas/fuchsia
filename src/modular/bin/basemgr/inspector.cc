// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/inspector.h"

#include <lib/syslog/cpp/macros.h>

#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular {

namespace {
constexpr char kChildRestartTrackerName[] = "eager_children_restarts";
}

BasemgrInspector::BasemgrInspector(inspect::Inspector* inspector) : inspector_(inspector) {
  FX_DCHECK(inspector_);
}

void BasemgrInspector::AddConfig(const fuchsia::modular::session::ModularConfig& config) {
  auto config_json = modular::ConfigToJsonString(config);
  inspector_->GetRoot().CreateString(modular_config::kInspectConfig, config_json, &static_values_);
}

inspect::Node BasemgrInspector::CreateChildRestartTrackerNode() {
  return inspector_->GetRoot().CreateChild(kChildRestartTrackerName);
}

}  // namespace modular
