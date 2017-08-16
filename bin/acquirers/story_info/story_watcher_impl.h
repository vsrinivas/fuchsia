// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_STORY_WATCHER_IMPL_H_
#define APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_STORY_WATCHER_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include "apps/maxwell/lib/async/future_value.h"
#include "apps/maxwell/services/context/context_writer.fidl.h"
#include "apps/modular/services/module/module_data.fidl.h"
#include "apps/modular/services/story/story_controller.fidl.h"
#include "apps/modular/services/story/story_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"

namespace maxwell {

class StoryInfoAcquirer;
class LinkWatcherImpl;

class StoryWatcherImpl : modular::StoryWatcher, modular::StoryLinksWatcher {
 public:
  StoryWatcherImpl(StoryInfoAcquirer* const owner,
                   ContextWriter* const writer,
                   modular::StoryProvider* const story_provider,
                   const std::string& story_id);

  ~StoryWatcherImpl() override;

  // Used by LinkWatcherImpl.
  void DropLink(const std::string& link_key);

  // Used by |owner_|.
  void OnStoryStateChange(modular::StoryInfoPtr info, modular::StoryState state);
  void OnFocusChange(bool focused);

 private:
  // |StoryWatcher|
  void OnStateChange(modular::StoryState new_state) override;

  // |StoryWatcher|
  void OnModuleAdded(modular::ModuleDataPtr module_data) override;

  // |StoryLinksWatcher|
  void OnNewLink(modular::LinkPathPtr link_path) override;

  void WatchLink(const modular::LinkPathPtr& link_path);

  void UpdateContext();

  StoryInfoAcquirer* const owner_;
  ContextWriter* const writer_;
  modular::StoryControllerPtr story_controller_;
  const std::string story_id_;
  FutureValue<fidl::String> context_value_id_;
  ContextMetadataPtr metadata_;

  fidl::Binding<modular::StoryWatcher> story_watcher_binding_;
  fidl::Binding<modular::StoryLinksWatcher> story_links_watcher_binding_;

  std::map<std::string, std::unique_ptr<LinkWatcherImpl>> links_;
  // serialized module path -> context value id.
  std::map<std::string, FutureValue<fidl::String>> module_value_ids_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryWatcherImpl);
};

}  // namespace maxwell

#endif  // APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_STORY_WATCHER_IMPL_H_
