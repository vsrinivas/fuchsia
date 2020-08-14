// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_introspection.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "harvester.h"

namespace harvester {

void GatherIntrospection::Gather() {
  // TODO(fxbug.dev/223): Use lib inspect to get inspection data (rather than
  // the test/placeholder used here).
  std::string fake_json_data = "{ \"test\": 5 }";
  DockyardProxyStatus status = Dockyard().SendInspectJson(
      "inspect:/hub/fake/234/faux.Inspect", fake_json_data);
  if (status != DockyardProxyStatus::OK) {
    FX_LOGS(ERROR) << DockyardErrorString("SendInspectJson", status)
                   << " Inspection data will be missing";
  }
}

}  // namespace harvester
