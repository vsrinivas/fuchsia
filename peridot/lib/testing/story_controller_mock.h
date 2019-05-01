// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_STORY_CONTROLLER_MOCK_H_
#define PERIDOT_LIB_TESTING_STORY_CONTROLLER_MOCK_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_ptr_set.h>

#include <string>

namespace modular {

class StoryControllerMock : public fuchsia::modular::StoryController {
 public:
  StoryControllerMock() {}

  struct GetLinkCall {
    fuchsia::modular::LinkPath link_path;
  };
  std::vector<GetLinkCall> get_link_calls;

 private:
  // |fuchsia::modular::StoryController|
  void GetInfo(GetInfoCallback callback) override {
    fuchsia::modular::StoryInfo info;
    info.id = "wow";
    info.url = "wow";
    callback(std::move(info), fuchsia::modular::StoryState::STOPPED);
  }

  // |fuchsia::modular::StoryController|
  void RequestStart() override { FXL_NOTIMPLEMENTED(); }

  // |fuchsia::modular::StoryController|
  void Stop(StopCallback done) override { FXL_NOTIMPLEMENTED(); }

  // |fuchsia::modular::StoryController|
  void TakeAndLoadSnapshot(fuchsia::ui::views::ViewToken view_token,
                           TakeAndLoadSnapshotCallback done) override {
    FXL_NOTIMPLEMENTED();
  }

  // |fuchsia::modular::StoryController|
  void Watch(
      fidl::InterfaceHandle<fuchsia::modular::StoryWatcher> watcher) override {
    FXL_NOTIMPLEMENTED();
  }

  // |fuchsia::modular::StoryController|
  void GetActiveModules(GetActiveModulesCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  // |fuchsia::modular::StoryController|
  void GetModules(GetModulesCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  // |fuchsia::modular::StoryController|
  void GetModuleController(
      std::vector<std::string> module_path,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController> request)
      override {
    FXL_NOTIMPLEMENTED();
  }

  // |fuchsia::modular::StoryController|
  void GetLink(
      fuchsia::modular::LinkPath link_path,
      fidl::InterfaceRequest<fuchsia::modular::Link> request) override {
    GetLinkCall call{std::move(link_path)};
    get_link_calls.push_back(std::move(call));
  }

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryControllerMock);
};

}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_STORY_CONTROLLER_MOCK_H_
