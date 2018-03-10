// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_STORY_CONTROLLER_MOCK_H_
#define PERIDOT_LIB_TESTING_STORY_CONTROLLER_MOCK_H_

#include <string>

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/story/fidl/story_controller.fidl.h"
#include "lib/surface/fidl/surface.fidl.h"

namespace modular {

class StoryControllerMock : public StoryController {
 public:
  StoryControllerMock() {}

  std::string last_added_module() const { return last_added_module_; }

  struct GetLinkCall {
    f1dl::Array<f1dl::String> module_path;
    f1dl::String name;
  };
  std::vector<GetLinkCall> get_link_calls;

 private:
  // |StoryController|
  void GetInfo(const GetInfoCallback& callback) override {
    auto info = StoryInfo::New();
    info->id = "wow";
    info->url = "wow";
    callback(std::move(info), modular::StoryState::INITIAL);
  }

  // |StoryController|
  void SetInfoExtra(const f1dl::String& name,
                    const f1dl::String& value,
                    const SetInfoExtraCallback& callback) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void AddModuleDeprecated(f1dl::Array<f1dl::String> module_path,
                           const f1dl::String& module_name,
                           const f1dl::String& module_url,
                           const f1dl::String& link_name,
                           SurfaceRelationPtr surface_relation) override {
    last_added_module_ = module_url;
  }

  // |StoryController|
  void Start(f1dl::InterfaceRequest<mozart::ViewOwner> request) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void Stop(const StopCallback& done) override { FXL_NOTIMPLEMENTED(); }

  // |StoryController|
  void Watch(f1dl::InterfaceHandle<StoryWatcher> watcher) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void GetActiveModules(f1dl::InterfaceHandle<StoryModulesWatcher> watcher,
                        const GetActiveModulesCallback& callback) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void GetModules(const GetModulesCallback& callback) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void GetModuleController(
      f1dl::Array<f1dl::String> module_path,
      f1dl::InterfaceRequest<ModuleController> request) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void GetActiveLinks(f1dl::InterfaceHandle<StoryLinksWatcher> watcher,
                      const GetActiveLinksCallback& callback) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void GetLink(f1dl::Array<f1dl::String> module_path,
               const f1dl::String& name,
               f1dl::InterfaceRequest<Link> request) override {
    GetLinkCall call{std::move(module_path), name};
    get_link_calls.push_back(std::move(call));
  }

  void AddModule(f1dl::Array<f1dl::String> module_path,
                 const f1dl::String& module_name,
                 DaisyPtr daisy,
                 SurfaceRelationPtr surface_relation) override {
    FXL_NOTIMPLEMENTED();
  }

  std::string last_added_module_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryControllerMock);
};

}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_STORY_CONTROLLER_MOCK_H_
