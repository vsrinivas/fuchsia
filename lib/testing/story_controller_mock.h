// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_TESTING_STORY_CONTROLLER_MOCK_H_
#define APPS_MODULAR_LIB_TESTING_STORY_CONTROLLER_MOCK_H_

#include <string>

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/story/fidl/story_controller.fidl.h"
#include "lib/surface/fidl/surface.fidl.h"

namespace modular {

class StoryControllerMock : public StoryController {
 public:
  std::string last_added_module() const { return last_added_module_; }

 private:
  // |StoryController|
  void GetInfo(const GetInfoCallback& callback) override {
    auto info = StoryInfo::New();
    info->id = "wow";
    info->url = "wow";
    info->extra.mark_non_null();
    callback(std::move(info), modular::StoryState::INITIAL);
  }

  // |StoryController|
  void SetInfoExtra(const fidl::String& name,
                    const fidl::String& value,
                    const SetInfoExtraCallback& callback) override {}

  // |StoryController|
  void AddModule(fidl::Array<fidl::String> module_path,
                 const fidl::String& module_name,
                 const fidl::String& module_url,
                 const fidl::String& link_name,
                 SurfaceRelationPtr surface_relation) override {
    last_added_module_ = module_url;
  }

  // |StoryController|
  void Start(fidl::InterfaceRequest<mozart::ViewOwner> request) override {}

  // |StoryController|
  void Stop(const StopCallback& done) override {}

  // |StoryController|
  void Watch(fidl::InterfaceHandle<StoryWatcher> watcher) override {}

  // |StoryController|
  void GetActiveModules(fidl::InterfaceHandle<StoryModulesWatcher> watcher,
                        const GetActiveModulesCallback& callback) override {}

  // |StoryController|
  void GetModules(const GetModulesCallback& callback) override {}

  // |StoryController|
  void GetModuleController(
      fidl::Array<fidl::String> module_path,
      fidl::InterfaceRequest<ModuleController> request) override {}

  // |StoryController|
  void GetActiveLinks(fidl::InterfaceHandle<StoryLinksWatcher> watcher,
                      const GetActiveLinksCallback& callback) override {}

  // |StoryController|
  void GetLink(fidl::Array<fidl::String> module_path,
               const fidl::String& name,
               fidl::InterfaceRequest<Link> request) override {}

  std::string last_added_module_;
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_TESTING_STORY_PROVIDER_MOCK_H_
