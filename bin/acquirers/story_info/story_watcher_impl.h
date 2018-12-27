// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_ACQUIRERS_STORY_INFO_STORY_WATCHER_IMPL_H_
#define PERIDOT_BIN_ACQUIRERS_STORY_INFO_STORY_WATCHER_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/macros.h>

namespace maxwell {

class StoryInfoAcquirer;
class LinkWatcherImpl;

class StoryWatcherImpl : fuchsia::modular::StoryWatcher,
                         fuchsia::modular::StoryLinksWatcher {
 public:
  StoryWatcherImpl(StoryInfoAcquirer* const owner,
                   fuchsia::modular::ContextWriter* const writer,
                   fuchsia::modular::StoryProvider* const story_provider,
                   const std::string& story_id);

  ~StoryWatcherImpl() override;

  // Used by LinkWatcherImpl.
  void DropLink(const std::string& link_key);

  // Used by |owner_|.
  void OnStoryStateChange(fuchsia::modular::StoryInfo info,
                          fuchsia::modular::StoryState state);
  void OnFocusChange(bool focused);

 private:
  struct ContextModuleMetadata {
    fuchsia::modular::ContextMetadata metadata;
    fuchsia::modular::ContextValueWriterPtr value_writer;
  };

  // |fuchsia::modular::StoryWatcher|
  void OnStateChange(fuchsia::modular::StoryState new_state) override;

  // |fuchsia::modular::StoryWatcher|
  void OnModuleAdded(fuchsia::modular::ModuleData module_data) override;

  // |fuchsia::modular::StoryWatcher|
  void OnModuleFocused(std::vector<std::string> module_path) override;

  // |fuchsia::modular::StoryLinksWatcher|
  void OnNewLink(fuchsia::modular::LinkPath link_path) override;

  void WatchLink(fuchsia::modular::LinkPath link_path);

  void UpdateModuleFocus(ContextModuleMetadata* data, bool focused);

  StoryInfoAcquirer* const owner_;
  fuchsia::modular::ContextWriter* const writer_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  const std::string story_id_;
  fuchsia::modular::ContextValueWriterPtr context_value_;
  fuchsia::modular::ContextMetadata context_metadata_;

  fidl::Binding<fuchsia::modular::StoryWatcher> story_watcher_binding_;
  fidl::Binding<fuchsia::modular::StoryLinksWatcher>
      story_links_watcher_binding_;

  std::map<std::string, std::unique_ptr<LinkWatcherImpl>> links_;
  // serialized module path -> context value.
  std::map<std::string, ContextModuleMetadata> module_values_;

  std::string last_module_focus_key_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryWatcherImpl);
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_ACQUIRERS_STORY_INFO_STORY_WATCHER_IMPL_H_
