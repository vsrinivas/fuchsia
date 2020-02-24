// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_TESTAPP_FAKE_TIMEKEEPER_H_
#define SRC_COBALT_BIN_TESTAPP_FAKE_TIMEKEEPER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "fuchsia/time/cpp/fidl.h"
#include "fuchsia/time/cpp/fidl_test_base.h"
#include "src/lib/syslog/cpp/logger.h"

namespace cobalt::testapp {

// A Fake for the Timekeeper service that always returns the clock is accurate.
class FakeTimekeeper : public ::fuchsia::time::testing::Utc_TestBase {
 public:
  FakeTimekeeper() {
    context_ = sys::ComponentContext::Create();
    context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
  }

  explicit FakeTimekeeper(sys::ComponentContext* context) {
    context->outgoing()->AddPublicService(bindings_.GetHandler(this));
  }

  void NotImplemented_(const std::string& name) override {
    FX_LOGS(ERROR) << name << " is not implemented";
  }

  void WatchState(WatchStateCallback callback) override {
    FX_LOGS(INFO) << "Fake clock is always accurate.";
    fuchsia::time::UtcState state;
    state.set_source(fuchsia::time::UtcSource::EXTERNAL);
    state.set_timestamp(123);
    callback(std::move(state));
  }

 private:
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<::fuchsia::time::Utc> bindings_;
};

}  // namespace cobalt::testapp

#endif  // SRC_COBALT_BIN_TESTAPP_FAKE_TIMEKEEPER_H_
