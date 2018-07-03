// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_STORY_PROVIDER_MOCK_H_
#define PERIDOT_LIB_TESTING_STORY_PROVIDER_MOCK_H_

#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/interface_ptr_set.h>

#include "peridot/lib/testing/story_controller_mock.h"

namespace modular {

class StoryProviderMock : public fuchsia::modular::StoryProvider {
 public:
  // Allows notification of watchers.
  void NotifyStoryChanged(fuchsia::modular::StoryInfo story_info,
                          fuchsia::modular::StoryState story_state) {
    for (const auto& watcher : watchers_.ptrs()) {
      fuchsia::modular::StoryInfo story_info_clone;
      fidl::Clone(story_info, &story_info_clone);
      (*watcher)->OnChange(std::move(story_info_clone), story_state);
    }
  }

  const StoryControllerMock& story_controller() const {
    return controller_mock_;
  }

  const std::string& last_created_story() const { return last_created_story_; }

  const std::string& last_created_kind_of_story() const {
    return last_created_kind_of_story_;
  }

  const std::string& last_promoted_story() const {
    return last_promoted_story_;
  }

 private:
  // |fuchsia::modular::StoryProvider|
  void CreateStory(fidl::StringPtr url, CreateStoryCallback callback) override {
    last_created_story_ = url;
    callback("foo");
  }

  // |fuchsia::modular::StoryProvider|
  void CreateStoryWithInfo(
      fidl::StringPtr url,
      fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info,
      fidl::StringPtr json, CreateStoryWithInfoCallback callback) override {
    last_created_story_ = url;
    callback("foo");
  }

  // |fuchsia::modular::StoryProvider|
  void CreateKindOfProtoStory(CreateKindOfProtoStoryCallback callback)
      override {
    last_created_kind_of_story_ = "kindoffoo";
    callback("kindoffoo");
  }

  // |fuchsia::modular::StoryProvider|
  void Watch(fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher>
                 watcher) override {
    watchers_.AddInterfacePtr(watcher.Bind());
  }

  // |fuchsia::modular::StoryProvider|
  void WatchActivity(
      fidl::InterfaceHandle<fuchsia::modular::StoryActivityWatcher> watcher)
      override {
    activity_watchers_.AddInterfacePtr(watcher.Bind());
  }

  // |fuchsia::modular::StoryProvider|
  void DeleteStory(fidl::StringPtr story_id,
                   DeleteStoryCallback callback) override {
    callback();
  }

  // |fuchsia::modular::StoryProvider|
  void GetStoryInfo(fidl::StringPtr story_id,
                    GetStoryInfoCallback callback) override {
    callback(nullptr);
  }

  // |fuchsia::modular::StoryProvider|
  void GetController(fidl::StringPtr story_id,
                     fidl::InterfaceRequest<fuchsia::modular::StoryController>
                         story) override {
    binding_set_.AddBinding(&controller_mock_, std::move(story));
  }

  // |fuchsia::modular::StoryProvider|
  void PreviousStories(PreviousStoriesCallback callback) override {
    callback(fidl::VectorPtr<fuchsia::modular::StoryInfo>::New(0));
  }

  // |fuchsia::modular::StoryProvider|
  void RunningStories(RunningStoriesCallback callback) override {
    callback(fidl::VectorPtr<fidl::StringPtr>::New(0));
  }

  // |fuchsia::modular::StoryProvider|
  void Duplicate(fidl::InterfaceRequest<fuchsia::modular::StoryProvider>
                     request) override {
    FXL_LOG(FATAL) << "StoryProviderMock::Duplicate() not implemented.";
  }

  // |fuchsia::modular::StoryProvider|
  void GetLinkPeer(
      fidl::StringPtr story_id, fidl::VectorPtr<fidl::StringPtr> module_path,
      fidl::StringPtr link_path,
      fidl::InterfaceRequest<fuchsia::modular::Link> request) override {
    FXL_LOG(FATAL) << "StoryProviderMock::GetLinkPeer() not implemented.";
  }

  // |fuchsia::modular::StoryProvider|
  void PromoteKindOfProtoStory(
      fidl::StringPtr story_id,
      PromoteKindOfProtoStoryCallback callback) override {
    last_promoted_story_ = story_id;
    callback();
  }

  std::string last_created_story_;
  std::string last_created_kind_of_story_;
  std::string last_promoted_story_;
  StoryControllerMock controller_mock_;
  fidl::BindingSet<fuchsia::modular::StoryController> binding_set_;
  fidl::InterfacePtrSet<fuchsia::modular::StoryProviderWatcher> watchers_;
  fidl::InterfacePtrSet<fuchsia::modular::StoryActivityWatcher>
      activity_watchers_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_STORY_PROVIDER_MOCK_H_
