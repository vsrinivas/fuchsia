// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKES_FAKE_SCENIC_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKES_FAKE_SCENIC_H_

#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "garnet/bin/media/media_player/test/fakes/fake_session.h"
#include "garnet/bin/media/media_player/test/fakes/fake_view_manager.h"
#include "lib/fidl/cpp/binding.h"

namespace media_player {
namespace test {

// Implements ViewManager for testing.
class FakeScenic : public ::fuchsia::ui::scenic::Scenic {
 public:
  FakeScenic();

  ~FakeScenic() override;

  FakeSession& session() { return fake_session_; }

  FakeViewManager& view_manager() { return fake_view_manager_; }

  // Binds this scenic.
  void Bind(fidl::InterfaceRequest<::fuchsia::ui::scenic::Scenic> request);

  // Scenic implementation.
  void CreateSession(
      ::fidl::InterfaceRequest<::fuchsia::ui::scenic::Session> session,
      ::fidl::InterfaceHandle<::fuchsia::ui::scenic::SessionListener> listener)
      override;

  void GetDisplayInfo(GetDisplayInfoCallback callback) override;

  void GetDisplayOwnershipEvent(
      GetDisplayOwnershipEventCallback callback) override;

  void TakeScreenshot(TakeScreenshotCallback callback) override;

 private:
  async_dispatcher_t* dispatcher_;
  fidl::Binding<::fuchsia::ui::scenic::Scenic> binding_;
  FakeSession fake_session_;
  FakeViewManager fake_view_manager_;
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKES_FAKE_SCENIC_H_
