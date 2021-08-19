// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/inspector.h"

#include <lib/syslog/cpp/macros.h>

#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular {

BasemgrInspector::BasemgrInspector(inspect::Inspector* inspector)
    : inspector_(inspector),
      session_started_at_list_(/*capacity=*/kInspectSessionStartedAtCapacity) {
  FX_DCHECK(inspector_);

  session_started_at_list_.AttachInspect(inspector->GetRoot(), kInspectSessionStartedAtNodeName);
}

void BasemgrInspector::AddConfig(const fuchsia::modular::session::ModularConfig& config) {
  auto config_json = modular::ConfigToJsonString(config);
  inspector_->GetRoot().CreateString(modular_config::kInspectConfig, config_json, &static_values_);
}

void BasemgrInspector::AddSessionStartedAt(zx_time_t timestamp) {
  auto& item = session_started_at_list_.CreateItem();
  item.node.CreateInt(kInspectTimePropertyName, timestamp, &item.values);
}

}  // namespace modular
