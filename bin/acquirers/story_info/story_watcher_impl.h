// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_ACQUIRERS_STORY_INFO_STORY_WATCHER_IMPL_H_
#define PERIDOT_BIN_ACQUIRERS_STORY_INFO_STORY_WATCHER_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include <fuchsia/cpp/modular.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"

namespace maxwell {

class StoryInfoAcquirer;
class LinkWatcherImpl;

class StoryWatcherImpl : modular::StoryWatcher, modular::StoryLinksWatcher {
 public:
  StoryWatcherImpl(StoryInfoAcquirer* const owner,
                   modular::ContextWriter* const writer,
                   modular::StoryProvider* const story_provider,
                   const std::string& story_id);

  ~StoryWatcherImpl() override;

  // Used by LinkWatcherImpl.
  void DropLink(const std::string& link_key);

  // Used by |owner_|.
  void OnStoryStateChange(modular::StoryInfo info, modular::StoryState state);
  void OnFocusChange(bool focused);

 private:
  // |StoryWatcher|
  void OnStateChange(modular::StoryState new_state) override;

  // |StoryWatcher|
  void OnModuleAdded(modular::ModuleData module_data) override;

  // |StoryLinksWatcher|
  void OnNewLink(modular::LinkPath link_path) override;

  void WatchLink(modular::LinkPath link_path);

  StoryInfoAcquirer* const owner_;
  modular::ContextWriter* const writer_;
  modular::StoryControllerPtr story_controller_;
  const std::string story_id_;
  modular::ContextValueWriterPtr context_value_;
  modular::ContextMetadata context_metadata_;

  fidl::Binding<modular::StoryWatcher> story_watcher_binding_;
  fidl::Binding<modular::StoryLinksWatcher> story_links_watcher_binding_;

  std::map<std::string, std::unique_ptr<LinkWatcherImpl>> links_;
  // serialized module path -> context value.
  std::map<std::string, modular::ContextValueWriterPtr> module_values_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryWatcherImpl);
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_ACQUIRERS_STORY_INFO_STORY_WATCHER_IMPL_H_
