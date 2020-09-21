// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_SESSION_H_
#define SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_SESSION_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/ui/scenic/cpp/resources.h>

namespace root_presenter {
namespace testing {

class FakeSession : public fuchsia::ui::scenic::Session {
 public:
  FakeSession();

  ~FakeSession() override;

  //  void NotImplemented_(const std::string& name) final {}

  // Binds the session.
  void Bind(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> request,
            fuchsia::ui::scenic::SessionListenerPtr listener);

  // Session implementation.
  void Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) override;

  void Present(uint64_t presentation_time, std::vector<zx::event> acquire_fences,
               std::vector<zx::event> release_fences, PresentCallback callback) override;
  void Present(uint64_t presentation_time, PresentCallback callback);

  void RequestPresentationTimes(zx_duration_t request_prediction_span,
                                RequestPresentationTimesCallback callback) override;
  void Present2(fuchsia::ui::scenic::Present2Args args, Present2Callback callback) override;

  void SetDebugName(std::string debug_name) override {}

  void RegisterBufferCollection(
      uint32_t buffer_collection_id,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override {}

  void DeregisterBufferCollection(uint32_t buffer_collection_id) override {}

  // Test method
  bool PresentWasCalled() { return presents_called_ > 0; }
  int PresentsCalled() { return presents_called_; }

  // Test method
  auto GetFirstCommand() {
    return last_cmds_.size() > 0 ? std::make_optional(std::move(last_cmds_.front().gfx()))
                                 : std::nullopt;
  }

 private:
  int presents_called_ = 0;
  std::vector<fuchsia::ui::scenic::Command> last_cmds_;
  fidl::Binding<fuchsia::ui::scenic::Session> binding_;
  fuchsia::ui::scenic::SessionListenerPtr listener_;
};

}  // namespace testing
}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_SESSION_H_
