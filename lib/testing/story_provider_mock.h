// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_STORY_PROVIDER_MOCK_H_
#define PERIDOT_LIB_TESTING_STORY_PROVIDER_MOCK_H_

#include <string>

#include <fuchsia/cpp/modular.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "peridot/lib/testing/story_controller_mock.h"

namespace modular {

class StoryProviderMock : public StoryProvider {
 public:
  // Allows notification of watchers.
  void NotifyStoryChanged(modular::StoryInfo story_info,
                          modular::StoryState story_state) {
    for (const auto& watcher : watchers_.ptrs()) {
      modular::StoryInfo story_info_clone;
      fidl::Clone(story_info, &story_info_clone);
      (*watcher)->OnChange(std::move(story_info_clone), story_state);
    }
  }

  const modular::StoryControllerMock& story_controller() const {
    return controller_mock_;
  }
  std::string last_created_story() const { return last_created_story_; }

 private:
  // |StoryProvider|
  void CreateStory(fidl::StringPtr url,
                   CreateStoryCallback callback) override {
    last_created_story_ = url;
    callback("foo");
  }

  // |StoryProvider|
  void CreateStoryWithInfo(
      fidl::StringPtr url,
      fidl::VectorPtr<StoryInfoExtraEntry> extra_info,
      fidl::StringPtr json,
      CreateStoryWithInfoCallback callback) override {
    last_created_story_ = url;
    callback("foo");
  }

  // |StoryProvider|
  void Watch(
      fidl::InterfaceHandle<modular::StoryProviderWatcher> watcher) override {
    watchers_.AddInterfacePtr(watcher.Bind());
  }

  // |StoryProvider|
  void DeleteStory(fidl::StringPtr story_id,
                   DeleteStoryCallback callback) override {
    callback();
  }

  // |StoryProvider|
  void GetStoryInfo(fidl::StringPtr story_id,
                    GetStoryInfoCallback callback) override {
    callback(nullptr);
  }

  // |StoryProvider|
  void GetController(
      fidl::StringPtr story_id,
      fidl::InterfaceRequest<modular::StoryController> story) override {
    binding_set_.AddBinding(&controller_mock_, std::move(story));
  }

  // |StoryProvider|
  void PreviousStories(PreviousStoriesCallback callback) override {
    callback(fidl::VectorPtr<modular::StoryInfo>::New(0));
  }

  // |StoryProvider|
  void RunningStories(RunningStoriesCallback callback) override {
    callback(fidl::VectorPtr<fidl::StringPtr>::New(0));
  }

  // |StoryProvider|
  void Duplicate(fidl::InterfaceRequest<StoryProvider> request) override {
    FXL_LOG(FATAL) << "StoryProviderMock::Duplicate() not implemented.";
  }

  // |StoryProvider|
  void GetLinkPeer(fidl::StringPtr story_id,
                   fidl::VectorPtr<fidl::StringPtr> module_path,
                   fidl::StringPtr link_path,
                   fidl::InterfaceRequest<Link> request) override {
    FXL_LOG(FATAL) << "StoryProviderMock::GetLinkPeer() not implemented.";
  }

  std::string last_created_story_;
  modular::StoryControllerMock controller_mock_;
  fidl::BindingSet<modular::StoryController> binding_set_;
  fidl::InterfacePtrSet<modular::StoryProviderWatcher> watchers_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_STORY_PROVIDER_MOCK_H_
