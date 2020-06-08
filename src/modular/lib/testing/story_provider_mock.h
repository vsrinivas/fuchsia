// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_TESTING_STORY_PROVIDER_MOCK_H_
#define SRC_MODULAR_LIB_TESTING_STORY_PROVIDER_MOCK_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/interface_ptr_set.h>

#include <string>

#include "src/modular/lib/testing/story_controller_mock.h"

namespace modular {

class StoryProviderMock : public fuchsia::modular::StoryProvider {
 public:
  // Allows notification of watchers.
  void NotifyStoryChanged(fuchsia::modular::StoryInfo2 story_info,
                          fuchsia::modular::StoryState story_state,
                          fuchsia::modular::StoryVisibilityState story_visibility_state) {
    for (const auto& watcher : watchers_.ptrs()) {
      fuchsia::modular::StoryInfo2 story_info_clone;
      fidl::Clone(story_info, &story_info_clone);
      (*watcher)->OnChange2(std::move(story_info_clone), story_state, story_visibility_state);
    }
  }

  const StoryControllerMock& story_controller() const { return controller_mock_; }

  const std::string& last_created_story() const { return last_created_story_; }

  const std::string& deleted_story() const { return deleted_story_; }

 private:
  // |fuchsia::modular::StoryProvider|
  void GetStories2(fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher,
                   GetStories2Callback callback) override {
    std::vector<fuchsia::modular::StoryInfo2> stories;
    callback(std::move(stories));
  }

  // |fuchsia::modular::StoryProvider|
  void Watch(fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher) override {
    watchers_.AddInterfacePtr(watcher.Bind());
  }

  // |fuchsia::modular::StoryProvider|
  void GetStoryInfo2(std::string story_id, GetStoryInfo2Callback callback) override {
    callback(fuchsia::modular::StoryInfo2{});
  }

  // |fuchsia::modular::StoryProvider|
  void GetController(std::string story_id,
                     fidl::InterfaceRequest<fuchsia::modular::StoryController> story) override {
    binding_set_.AddBinding(&controller_mock_, std::move(story));
  }

  std::string last_created_story_;
  std::string deleted_story_;
  StoryControllerMock controller_mock_;
  fidl::BindingSet<fuchsia::modular::StoryController> binding_set_;
  fidl::InterfacePtrSet<fuchsia::modular::StoryProviderWatcher> watchers_;
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_TESTING_STORY_PROVIDER_MOCK_H_
