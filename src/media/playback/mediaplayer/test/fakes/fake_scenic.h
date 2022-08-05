// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_SCENIC_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_SCENIC_H_
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>

#include "lib/fidl/cpp/binding_set.h"
#include "src/media/playback/mediaplayer/test/fakes/fake_session.h"
namespace media_player {
namespace test {
// Implements Scenic for testing.
class FakeScenic : public fuchsia::ui::scenic::Scenic, public component_testing::LocalComponent {
 public:
  FakeScenic();
  ~FakeScenic() override;
  FakeSession& session() { return fake_session_; }
  void SetSysmemAllocator(fuchsia::sysmem::Allocator* sysmem_allocator) {
    fake_session_.SetSysmemAllocator(sysmem_allocator);
  }
  // Returns a request handler for binding to this fake service.
  fidl::InterfaceRequestHandler<fuchsia::ui::scenic::Scenic> GetRequestHandler() {
    return bindings_.GetHandler(this, dispatcher_);
  }
  // Binds this scenic.
  void Bind(fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> request) {
    bindings_.AddBinding(this, std::move(request));
  }
  // Scenic implementation.
  void CreateSession(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
                     fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) override;
  void GetDisplayInfo(GetDisplayInfoCallback callback) override;
  void GetDisplayOwnershipEvent(GetDisplayOwnershipEventCallback callback) override;
  void TakeScreenshot(TakeScreenshotCallback callback) override;
  void Start(std::unique_ptr<component_testing::LocalComponentHandles> handles) override {
    handles_ = std::move(handles);
    handles_->outgoing()->AddPublicService(bindings_.GetHandler(this, dispatcher_));
  }

 private:
  async_dispatcher_t* dispatcher_;
  fidl::BindingSet<fuchsia::ui::scenic::Scenic> bindings_;
  FakeSession fake_session_;
  std::unique_ptr<component_testing::LocalComponentHandles> handles_;
};
}  // namespace test
}  // namespace media_player
#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_SCENIC_H_
