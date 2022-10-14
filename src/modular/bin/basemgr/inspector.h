// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_INSPECTOR_H_
#define SRC_MODULAR_BIN_BASEMGR_INSPECTOR_H_

#include <lib/inspect/cpp/inspector.h>
#include <zircon/time.h>

#include "src/modular/lib/modular_config/modular_config.h"

namespace modular {

class BasemgrInspector {
 public:
  explicit BasemgrInspector(inspect::Inspector* inspector);

  // Adds the Modular `config` to the root of the inspect tree as a JSON string.
  void AddConfig(const fuchsia::modular::session::ModularConfig& config);

  // Create a child node that will be used to track eager children restarts.
  // Should only be called once.
  inspect::Node CreateChildRestartTrackerNode();

 private:
  inspect::Inspector* inspector_;
  inspect::ValueList static_values_;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_INSPECTOR_H_
