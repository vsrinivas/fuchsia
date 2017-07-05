// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "application/lib/svc/service_namespace.h"
#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/src/acquirers/story_info/initializer.fidl.h"
#include "apps/modular/services/agent/agent.fidl.h"
#include "apps/modular/services/story/story_provider.fidl.h"
#include "apps/modular/services/story/story_controller.fidl.h"
#include "apps/modular/services/user/focus.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"

namespace maxwell {

class StoryWatcherImpl;

// This class pulls info about Stories from Framework and stores it in
// the Context service as follows (note all values are JSON-encoded):
//
// /story/focused_id = ID string of currently focused story, or null
// /story/visible_count = number of visible stories
// /story/visible_ids = array of story IDs that are visible
//
// /story/id/<id>/url = URL of root Module
// /story/id/<id>/state = modular.StoryState enum as string
// /story/id/<id>/deleted = true or false
class StoryInfoAcquirer : public modular::Agent,
                          public modular::VisibleStoriesWatcher,
                          public modular::StoryProviderWatcher,
                          public modular::FocusWatcher,
                          public StoryInfoInitializer {
 public:
  StoryInfoAcquirer();
  ~StoryInfoAcquirer() override;

  // Used by StoryWatcherImpl.
  void DropStoryWatcher(const std::string& story_id);

 private:
  // |StoryInfoInitializer|
  void Initialize(fidl::InterfaceHandle<modular::StoryProvider> story_provider,
                  fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
                  fidl::InterfaceHandle<modular::VisibleStoriesProvider>
                      visible_stories_provider) override;

  // |Agent|
  void Initialize(fidl::InterfaceHandle<modular::AgentContext> agent_context,
                  const InitializeCallback& callback) override;

  // |Agent|
  void Connect(const fidl::String& requestor_url,
               fidl::InterfaceRequest<app::ServiceProvider> services) override;

  // |Agent|
  void RunTask(const fidl::String& task_id,
               const RunTaskCallback& callback) override;

  // |Agent|
  void Stop(const StopCallback& callback) override;

  // |FocusWatcher|
  void OnFocusChange(modular::FocusInfoPtr info) override;

  // |VisibleStoriesWatcher|
  void OnVisibleStoriesChange(fidl::Array<fidl::String> ids) override;

  // |StoryProviderWatcher|
  void OnChange(modular::StoryInfoPtr info, modular::StoryState state) override;
  void OnDelete(const fidl::String& story_id) override;

  ContextPublisherPtr context_publisher_;
  modular::StoryProviderPtr story_provider_;
  modular::FocusProviderPtr focus_provider_;

  fidl::Binding<StoryInfoInitializer> initializer_binding_;
  fidl::Binding<modular::VisibleStoriesWatcher>
      visible_stories_watcher_binding_;
  fidl::Binding<modular::StoryProviderWatcher> story_provider_watcher_binding_;
  fidl::Binding<modular::FocusWatcher> focus_watcher_binding_;

  // A collection of all active stories we watch. Keys are story IDs, Values are
  // the StoryWatcher instances.
  std::map<std::string, std::unique_ptr<StoryWatcherImpl>> stories_;

  app::ServiceNamespace agent_services_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryInfoAcquirer);
};

std::string CreateKey(const std::string suffix);
std::string CreateKey(const std::string& story_id, const std::string suffix);

}  // namespace maxwell
