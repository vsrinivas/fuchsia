// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_H_
#define SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/svc/cpp/services.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "lib/sys/cpp/testing/scoped_child.h"
#include "src/cobalt/bin/testapp/cobalt_testapp_logger.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/macros.h"
#include "third_party/cobalt/src/public/lib/clock_interfaces.h"

namespace cobalt::testapp {

constexpr size_t kEventAggregatorBackfillDays = 2;

class SystemClock : public util::SystemClockInterface {
  std::chrono::system_clock::time_point now() override { return std::chrono::system_clock::now(); }
};
class CobaltTestApp {
 public:
  CobaltTestApp(bool use_network, bool test_for_prober)
      : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
        logger_(use_network, &cobalt_controller_),
        use_network_(use_network),
        test_for_prober_(test_for_prober) {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    clock_ = std::make_unique<SystemClock>();
    if (test_for_prober) {
      FX_LOGS(INFO) << "Running the Cobalt test app in prober mode";
    }
  }

  // Runs all of the tests. Returns true if they all pass.
  bool RunTests();

 private:
  // Starts and connects to the cobalt fidl service using the provided variant file.
  sys::testing::ScopedChild Connect(const std::string &variant);

  void SetChannel(const std::string &current_channel);
  bool DoDebugMetricTest();
  bool DoLocalAggregationTests(size_t backfill_days, const std::string &variant);

  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::cobalt::ControllerSyncPtr cobalt_controller_;
  fuchsia::cobalt::SystemDataUpdaterSyncPtr system_data_updater_;
  CobaltTestAppLogger logger_;
  bool use_network_;
  bool test_for_prober_;
  std::unique_ptr<util::SystemClockInterface> clock_;

  // ==================================================================================
  // TODO(fxbug.dev/83362): Hack to deal with slow destructor.
  std::vector<std::thread> scoped_child_destructors_;
  void DropChild(sys::testing::ScopedChild child) {
    scoped_child_destructors_.emplace_back(std::thread([child = std::move(child)] {}));
  }

 public:
  ~CobaltTestApp() {
    for (std::thread &t : scoped_child_destructors_) {
      t.join();
    }
  }
  // End: Hack to deal with slow destructor.
  // ==================================================================================

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltTestApp);
};

}  // namespace cobalt::testapp

#endif  // SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_H_
