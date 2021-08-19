// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_INSPECTOR_IMPL_H_
#define SRC_MODULAR_BIN_BASEMGR_INSPECTOR_IMPL_H_

#include <lib/inspect/cpp/inspector.h>
#include <zircon/time.h>

#include "src/modular/bin/basemgr/bounded_inspect_list_node.h"
#include "src/modular/lib/modular_config/modular_config.h"

namespace modular {

// Name of the inspect node that contains timestamps of when the session was started.
inline constexpr char kInspectSessionStartedAtNodeName[] = "session_started_at";
// Name of an inspect property that contains a timestamp.
inline constexpr char kInspectTimePropertyName[] = "@time";
// The maximum number of entries in the `session_started_at` list.
inline constexpr size_t kInspectSessionStartedAtCapacity = 100;

class BasemgrInspector {
 public:
  explicit BasemgrInspector(inspect::Inspector* inspector);

  // Adds the Modular `config` to the root of the inspect tree as a JSON string.
  void AddConfig(const fuchsia::modular::session::ModularConfig& config);

  // Adds a timestamp that records when a session was started to the inspect tree.
  //
  // Only the last |kSessionStartedAtCapacity| entries are stored.
  void AddSessionStartedAt(zx_time_t timestamp);

 private:
  inspect::Inspector* inspector_;
  inspect::ValueList static_values_;
  BoundedInspectListNode session_started_at_list_;
};

}  // namespace modular

#endif
