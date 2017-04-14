// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/src/acquirers/story_info/initializer.fidl.h"
#include "apps/modular/services/agent/agent.fidl.h"
#include "apps/modular/services/story/story_provider.fidl.h"
#include "apps/modular/services/user/focus.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "application/lib/app/service_provider_impl.h"

namespace maxwell {

// This class pulls info about Stories from Framework and stores it in
// the Context service as follows:
//
// /story/focused_id = ID of currently focused story, or null
// /story/visible_count = number of visible stories
//
// /story/id/<id>/visible = 1 or 0
// /story/id/<id>/url = URL of root Module
// /story/id/<id>/state = modular.StoryState enum as string
class StoryInfoAcquirer : public modular::Agent,
                          public modular::VisibleStoriesWatcher,
                          public modular::StoryProviderWatcher,
                          public StoryInfoInitializer {
 public:
  StoryInfoAcquirer();
  ~StoryInfoAcquirer() override;

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

  // |VisibleStoriesWatcher|
  void OnVisibleStoriesChange(fidl::Array<fidl::String> ids) override;

  // |StoryProviderWatcher|
  void OnChange(modular::StoryInfoPtr info) override;
  void OnDelete(const fidl::String& story_id) override;

  ContextPublisherPtr context_publisher_;
  modular::StoryProviderPtr story_provider_;
  modular::FocusProviderPtr focus_provider_;

  fidl::Binding<StoryInfoInitializer> initializer_binding_;
  fidl::Binding<modular::VisibleStoriesWatcher>
      visible_stories_watcher_binding_;
  fidl::Binding<modular::StoryProviderWatcher> story_provider_watcher_binding_;

  app::ServiceProviderImpl agent_services_;
};

}  // namespace maxwell