// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/launcher/launcher.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/maxwell/src/integration/test_suggestion_listener.h"
#include "apps/maxwell/src/integration/test.h"

TEST_F(MaxwellTestBase, Launcher) {
  auto launcher_services =
      StartServiceProvider("file:///system/apps/maxwell_launcher");
  maxwell::LauncherPtr launcher =
      modular::ConnectToService<maxwell::Launcher>(launcher_services.get());
  fidl::InterfaceHandle<modular::StoryProvider> story_provider_handle;
  fidl::InterfaceHandle<modular::FocusController> focus_controller_handle;

  // This is a convenient way to bind these handles to make them useable,
  // although they won't be backed by a real implementation.
  //
  // If any clients of the Launcher make use of either interface they won't
  // get a response because FIDL will buffer the requests indefinitely.
  story_provider_handle.NewRequest();
  focus_controller_handle.NewRequest();
  launcher->Initialize(std::move(story_provider_handle),
                       std::move(focus_controller_handle));

  maxwell::SuggestionProviderPtr client =
      modular::ConnectToService<maxwell::SuggestionProvider>(
          launcher_services.get());
  TestSuggestionListener listener;
  fidl::InterfaceHandle<maxwell::Listener> listener_handle;
  fidl::Binding<maxwell::Listener> binding(&listener, &listener_handle);
  maxwell::NextControllerPtr ctl;

  client->SubscribeToNext(std::move(listener_handle), ctl.NewRequest());

  ctl->SetResultCount(10);
  // TODO(afergan): Test this again once we are using the focus acquirer agent.
  // ASYNC_EQ(1, listener.suggestion_count())
}
