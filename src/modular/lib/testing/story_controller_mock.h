// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_TESTING_STORY_CONTROLLER_MOCK_H_
#define SRC_MODULAR_LIB_TESTING_STORY_CONTROLLER_MOCK_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_ptr_set.h>

#include <string>

namespace modular {

class StoryControllerMock : public fuchsia::modular::StoryController {
 public:
  StoryControllerMock() {}

 private:
  // |fuchsia::modular::StoryController|
  void GetInfo2(GetInfo2Callback callback) override {
    fuchsia::modular::StoryInfo2 info;
    info.set_id("wow");
    callback(std::move(info), fuchsia::modular::StoryState::STOPPED);
  }

  // |fuchsia::modular::StoryController|
  void RequestStart() override { FX_NOTIMPLEMENTED(); }

  // |fuchsia::modular::StoryController|
  void Stop(StopCallback done) override { FX_NOTIMPLEMENTED(); }

  // |fuchsia::modular::StoryController|
  void Watch(fidl::InterfaceHandle<fuchsia::modular::StoryWatcher> watcher) override {
    FX_NOTIMPLEMENTED();
  }

  // |fuchsia::modular::StoryController|
  void GetModuleController(
      std::vector<std::string> module_path,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController> request) override {
    FX_NOTIMPLEMENTED();
  }

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryControllerMock);
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_TESTING_STORY_CONTROLLER_MOCK_H_
