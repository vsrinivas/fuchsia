// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_PROCESS_LIFECYCLE_IMPL_H_
#define SRC_COBALT_BIN_APP_PROCESS_LIFECYCLE_IMPL_H_

#include <fuchsia/process/lifecycle/cpp/fidl.h>

#include "third_party/cobalt/src/public/cobalt_service_interface.h"

namespace cobalt {

class ProcessLifecycle : public fuchsia::process::lifecycle::Lifecycle {
 public:
  explicit ProcessLifecycle(CobaltServiceInterface* cobalt_service)
      : cobalt_service_(cobalt_service) {}

  // |fuchsia::process::lifecycle::Lifecycle|
  void Stop() override { cobalt_service_->ShutDown(); }

 private:
  CobaltServiceInterface* cobalt_service_;  // not owned
};

}  // namespace cobalt

#endif
