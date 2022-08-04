// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/sessions.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/directory.h"
#include "src/modular/bin/basemgr/cobalt/metrics_logger.h"

namespace modular::sessions {

void ReportNewSessionToCobalt() {
  if (files::IsDirectory(kSessionDirectoryPath)) {
    FX_LOGS(INFO) << "Creating session using existing account with fixed ID.";
    LogLifetimeEvent(cobalt_registry::ModularLifetimeEventsMigratedMetricDimensionEventType::
                         CreateSessionExistingFixedAccount);
  } else {
    FX_LOGS(INFO) << "Creating session using new persistent account with fixed ID.";
    LogLifetimeEvent(cobalt_registry::ModularLifetimeEventsMigratedMetricDimensionEventType::
                         CreateSessionNewPersistentAccount);
  }
}

}  // namespace modular::sessions
