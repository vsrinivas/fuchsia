// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_STORY_CONTROLLER_MOCK_H_
#define PERIDOT_LIB_TESTING_STORY_CONTROLLER_MOCK_H_

#include <string>

#include <fuchsia/cpp/modular.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_ptr_set.h"

namespace modular {

class StoryControllerMock : public StoryController {
 public:
  StoryControllerMock() {}

  std::string last_added_module() const { return last_added_module_; }

  struct GetLinkCall {
    fidl::VectorPtr<fidl::StringPtr> module_path;
    fidl::StringPtr name;
  };
  std::vector<GetLinkCall> get_link_calls;

 private:
  // |StoryController|
  void GetInfo(GetInfoCallback callback) override {
    StoryInfo info;
    info.id = "wow";
    info.url = "wow";
    callback(std::move(info), modular::StoryState::INITIAL);
  }

  // |StoryController|
  void SetInfoExtra(fidl::StringPtr name,
                    fidl::StringPtr value,
                    SetInfoExtraCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void AddModuleDeprecated(fidl::VectorPtr<fidl::StringPtr> module_path,
                           fidl::StringPtr module_name,
                           fidl::StringPtr module_url,
                           fidl::StringPtr link_name,
                           SurfaceRelationPtr surface_relation) override {
    last_added_module_ = module_url;
  }

  // |StoryController|
  void Start(fidl::InterfaceRequest<views_v1_token::ViewOwner> request) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void Stop(StopCallback done) override { FXL_NOTIMPLEMENTED(); }

  // |StoryController|
  void Watch(fidl::InterfaceHandle<StoryWatcher> watcher) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void GetActiveModules(fidl::InterfaceHandle<StoryModulesWatcher> watcher,
                        GetActiveModulesCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void GetModules(GetModulesCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void GetModuleController(
      fidl::VectorPtr<fidl::StringPtr> module_path,
      fidl::InterfaceRequest<ModuleController> request) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void GetActiveLinks(fidl::InterfaceHandle<StoryLinksWatcher> watcher,
                      GetActiveLinksCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  // |StoryController|
  void GetLink(fidl::VectorPtr<fidl::StringPtr> module_path,
               fidl::StringPtr name,
               fidl::InterfaceRequest<Link> request) override {
    GetLinkCall call{std::move(module_path), name};
    get_link_calls.push_back(std::move(call));
  }

  void AddModule(fidl::VectorPtr<fidl::StringPtr> module_path,
                 fidl::StringPtr module_name,
                 Daisy daisy,
                 SurfaceRelationPtr surface_relation) override {
    FXL_NOTIMPLEMENTED();
  }

  std::string last_added_module_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryControllerMock);
};

}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_STORY_CONTROLLER_MOCK_H_
