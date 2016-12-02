// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_TESTING_STORY_PROVIDER_MOCK_H_
#define APPS_MODULAR_LIB_TESTING_STORY_PROVIDER_MOCK_H_

#include <string>

#include "apps/modular/services/user/story_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"

namespace modular {

class StoryProviderMock : public StoryProvider {
 public:
  // Allows notification of watchers.
  void NotifyStoryChanged(modular::StoryInfoPtr story_info) {
    watchers_.ForAllPtrs([&story_info](modular::StoryProviderWatcher* watcher) {
      watcher->OnChange(story_info->Clone());
    });
  }

  std::string last_created_story() const { return last_created_story_; }

 private:
  // |StoryProvider|
  void CreateStory(
      const fidl::String& url,
      fidl::InterfaceRequest<modular::StoryController> story) override {
    last_created_story_ = url;
  }

  // |StoryProvider|
  void CreateStoryWithInfo(
      const fidl::String& url,
      fidl::Map<fidl::String, fidl::String> extra_info,
      fidl::Map<fidl::String, document_store::DocumentPtr> root_docs,
      fidl::InterfaceRequest<modular::StoryController> story) override {
    last_created_story_ = url;
  }

  // |StoryProvider|
  void Watch(
      fidl::InterfaceHandle<modular::StoryProviderWatcher> watcher) override {
    watchers_.AddInterfacePtr(
        modular::StoryProviderWatcherPtr::Create(std::move(watcher)));
  }

  // |StoryProvider|
  void DeleteStory(const fidl::String& story_id,
                   const DeleteStoryCallback& callback) override {}

  // |StoryProvider|
  void GetStoryInfo(const fidl::String& story_id,
                    const GetStoryInfoCallback& callback) override {}

  // |StoryProvider|
  void ResumeStory(
      const fidl::String& story_id,
      fidl::InterfaceRequest<modular::StoryController> story) override {}

  // |StoryProvider|
  void PreviousStories(const PreviousStoriesCallback& callback) override {}

  std::string last_created_story_;
  fidl::InterfacePtrSet<modular::StoryProviderWatcher> watchers_;
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_TESTING_STORY_PROVIDER_MOCK_H_
