// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_STORY_WATCHER_IMPL_H_
#define APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_STORY_WATCHER_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/modular/services/module/module_data.fidl.h"
#include "apps/modular/services/story/story_provider.fidl.h"
#include "apps/modular/services/story/story_controller.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"

namespace maxwell {

class StoryInfoAcquirer;
class LinkWatcherImpl;

class StoryWatcherImpl : modular::StoryWatcher, modular::StoryLinksWatcher {
 public:
  StoryWatcherImpl(StoryInfoAcquirer* const owner,
                   ContextPublisher* const publisher,
                   modular::StoryProvider* const story_provider,
                   const std::string& story_id);

  ~StoryWatcherImpl() override;

  // Used by LinkWatcherImpl.
  void DropLink(const std::string& link_key);

 private:
  // |StoryWatcher|
  void OnStateChange(modular::StoryState new_state) override;

  // |StoryWatcher|
  void OnModuleAdded(modular::ModuleDataPtr module_data) override;

  // |StoryLinksWatcher|
  void OnNewLink(modular::LinkPathPtr link_path) override;

  void WatchLink(const modular::LinkPathPtr& link_path);

  StoryInfoAcquirer* const owner_;
  ContextPublisher* const publisher_;
  modular::StoryControllerPtr story_controller_;
  const std::string story_id_;

  fidl::Binding<modular::StoryWatcher> story_watcher_binding_;
  fidl::Binding<modular::StoryLinksWatcher> story_links_watcher_binding_;

  std::map<std::string, std::unique_ptr<LinkWatcherImpl>> links_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryWatcherImpl);
};

}  // namespace maxwell

#endif  // APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_STORY_WATCHER_IMPL_H_
