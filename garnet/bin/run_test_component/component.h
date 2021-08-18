// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_COMPONENT_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_COMPONENT_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/socket.h>
#include <zircon/assert.h>

#include <cstdio>
#include <memory>
#include <vector>

#include "garnet/bin/run_test_component/output_collector.h"

namespace run {

class Component {
 public:
  static std::unique_ptr<Component> Launch(const fuchsia::sys::LauncherPtr& launcher,
                                           fuchsia::sys::LaunchInfo launch_info,
                                           async_dispatcher_t* dispatcher);

  explicit Component(std::unique_ptr<OutputCollector> out, std::unique_ptr<OutputCollector> err,
                     fuchsia::sys::ComponentControllerPtr controller,
                     std::shared_ptr<sys::ServiceDirectory> svc);

  const std::shared_ptr<sys::ServiceDirectory>& svc() const { return svc_; }

  const fuchsia::sys::ComponentControllerPtr& controller() const { return controller_; }

  /// Signal when all output is collected.
  fpromise::promise<> SignalWhenOutputCollected();

 private:
  std::unique_ptr<OutputCollector> stdout_;
  std::unique_ptr<OutputCollector> stderr_;
  fuchsia::sys::ComponentControllerPtr controller_;
  std::shared_ptr<sys::ServiceDirectory> svc_;
};

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_COMPONENT_H_
