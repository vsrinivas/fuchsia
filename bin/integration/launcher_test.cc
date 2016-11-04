// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/maxwell/src/integration/test_suggestion_listener.h"
#include "apps/maxwell/src/integration/test.h"

TEST_F(MaxwellTestBase, Launcher) {
  maxwell::suggestion::SuggestionProviderPtr client =
      ConnectToService<maxwell::suggestion::SuggestionProvider>(
          "file:///system/apps/maxwell_launcher");
  TestSuggestionListener listener;
  fidl::InterfaceHandle<maxwell::suggestion::Listener> listener_handle;
  fidl::Binding<maxwell::suggestion::Listener> binding(&listener,
                                                       &listener_handle);
  maxwell::suggestion::NextControllerPtr ctl;

  client->SubscribeToNext(std::move(listener_handle), GetProxy(&ctl));

  ctl->SetResultCount(10);
  ASYNC_EQ(1, listener.suggestion_count())
}
