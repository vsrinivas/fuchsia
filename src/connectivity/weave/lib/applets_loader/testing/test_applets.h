// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_TESTING_TEST_APPLETS_H_
#define SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_TESTING_TEST_APPLETS_H_

#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <string_view>

#include "src/connectivity/weave/applets/test_applets/test_applets.h"

namespace weavestack::applets::testing {

static constexpr char kTestAppletsModuleName[] = "test_applets.so";

// Opens the 'extension' interface to the test_applets module. This is an auxiliary ABI in addition
// to the Fuchsia Applets ABI that allows the behavior of the test_applets module to be controlled
// by tests.
//
// To use this correctly, you must also have included //src/media/weave/applets/test_applets as a
// loadable_module in the test_package that is linking against this library.
//
// See //src/media/weave/lib/applets_loader:weave_applets_loader_unittests as an example.
std::shared_ptr<TestAppletsModuleExt> OpenTestAppletsExt();

class TestAppletBuilder {
 public:
  explicit TestAppletBuilder(std::shared_ptr<TestAppletsModuleExt> module)
      : module_(std::move(module)) {}

  ~TestAppletBuilder() {
    if (module_) {
      zx_status_t status = Build();
      if (status != ZX_OK) {
        FX_PLOGS(FATAL, status) << "Failed to add weave applet";
      }
    }
  }

  TestAppletBuilder& WithSpec(TestAppletSpec spec) {
    spec_ = spec;
    return *this;
  }

  zx_status_t Build() {
    if (!module_) {
      return ZX_ERR_BAD_STATE;
    }
    zx_status_t status = module_->set_applet(spec_);
    module_ = nullptr;
    return status;
  }

 private:
  TestAppletSpec spec_ = {
      .trait_sources =
          {
              .traits = nullptr,
              .count = 0,
          },
      .trait_sinks =
          {
              .traits = nullptr,
              .count = 0,
          },
  };
  std::shared_ptr<TestAppletsModuleExt> module_;
};

class TestAppletsModule {
 public:
  static TestAppletsModule Open() { return TestAppletsModule(OpenTestAppletsExt()); }

  explicit TestAppletsModule(std::shared_ptr<TestAppletsModuleExt> module)
      : module_(std::move(module)) {}

  // Disallow copy/move.
  TestAppletsModule(const TestAppletsModule&) = delete;
  TestAppletsModule& operator=(const TestAppletsModule&) = delete;
  TestAppletsModule(TestAppletsModule&& o) = delete;
  TestAppletsModule& operator=(TestAppletsModule&& o) = delete;

  // Sets the new applet for the library. Must be called while the number of active applet
  // instances is zero.
  TestAppletBuilder SetApplet() const { return TestAppletBuilder(module_); }

  // Returns the number of active effect instances owned by this module.
  uint32_t InstanceCount() const { return module_->num_instances(); }

 private:
  std::shared_ptr<TestAppletsModuleExt> module_;
};

}  // namespace weavestack::applets::testing

#endif  // SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_LOADER_TESTING_TEST_APPLETS_H_
