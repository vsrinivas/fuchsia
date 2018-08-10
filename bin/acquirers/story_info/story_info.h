// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_ACQUIRERS_STORY_INFO_STORY_INFO_H_
#define PERIDOT_BIN_ACQUIRERS_STORY_INFO_STORY_INFO_H_

#include <map>
#include <set>

#include <fuchsia/maxwell/internal/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/agent_driver.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/macros.h>
#include <lib/svc/cpp/service_namespace.h>

namespace maxwell {

class StoryWatcherImpl;

// This class pulls info about Stories from Framework and stores it in
// the Context service.
//
// It maintains a hierarchy of context values to represent:
// Stories -> Modules
//         -> Link Entities
//
// TODO(thatguy): Add Link value types to the Context engine and use them
// here. Then update the resulting published value to remove its added JSON
// structure, since it will all be represented in the metadata of the value.
class StoryInfoAcquirer
    : public fuchsia::modular::VisibleStoriesWatcher,
      public fuchsia::modular::StoryProviderWatcher,
      public fuchsia::modular::FocusWatcher,
      public fuchsia::maxwell::internal::StoryInfoInitializer {
 public:
  StoryInfoAcquirer(modular::AgentHost* agent_host);
  ~StoryInfoAcquirer() override;

  // Called by AgentDriver.
  void Connect(fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> services);

  // Called by AgentDriver.
  void RunTask(const fidl::StringPtr& task_id,
               const fuchsia::modular::Agent::RunTaskCallback& callback);

  // Called by AgentDriver.
  void Terminate(const std::function<void()>& done);

  // Used by StoryWatcherImpl.
  void DropStoryWatcher(const std::string& story_id);

 private:
  // |StoryInfoInitializer|
  void Initialize(
      fidl::InterfaceHandle<fuchsia::modular::StoryProvider> story_provider,
      fidl::InterfaceHandle<fuchsia::modular::FocusProvider> focus_provider,
      fidl::InterfaceHandle<fuchsia::modular::VisibleStoriesProvider>
          visible_stories_provider) override;

  // |fuchsia::modular::FocusWatcher|
  void OnFocusChange(fuchsia::modular::FocusInfoPtr info) override;

  // |fuchsia::modular::VisibleStoriesWatcher|
  void OnVisibleStoriesChange(fidl::VectorPtr<fidl::StringPtr> ids) override;

  // |fuchsia::modular::StoryProviderWatcher|
  void OnChange(
      fuchsia::modular::StoryInfo info, fuchsia::modular::StoryState state,
      fuchsia::modular::StoryVisibilityState visibility_state) override;
  void OnDelete(fidl::StringPtr story_id) override;

  fuchsia::modular::ContextWriterPtr context_writer_;
  fuchsia::modular::ContextReaderPtr context_reader_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  fuchsia::modular::FocusProviderPtr focus_provider_;

  fidl::Binding<fuchsia::maxwell::internal::StoryInfoInitializer>
      initializer_binding_;
  fidl::Binding<fuchsia::modular::VisibleStoriesWatcher>
      visible_stories_watcher_binding_;
  fidl::Binding<fuchsia::modular::StoryProviderWatcher>
      story_provider_watcher_binding_;
  fidl::Binding<fuchsia::modular::FocusWatcher> focus_watcher_binding_;

  // Local state.
  // story id -> context value id
  std::map<fidl::StringPtr, fidl::StringPtr> story_value_ids_;
  fidl::StringPtr focused_story_id_;
  std::set<fidl::StringPtr> visible_story_ids_;

  // A collection of all active stories we watch. Keys are story IDs, Values are
  // the fuchsia::modular::StoryWatcher instances.
  std::map<std::string, std::unique_ptr<StoryWatcherImpl>> stories_;

  component::ServiceNamespace agent_services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryInfoAcquirer);
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_ACQUIRERS_STORY_INFO_STORY_INFO_H_
