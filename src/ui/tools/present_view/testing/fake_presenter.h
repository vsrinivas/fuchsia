// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_PRESENTER_H_
#define SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_PRESENTER_H_

#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl_test_base.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/zx/eventpair.h>

#include <memory>
#include <optional>
#include <string>

namespace present_view::testing {

// This fake interface can stand in for a |fuchsia::ui::policy::Presentation| in unit or integration
// tests. Normally `root_presenter` vends this interface.
//
// It allows test cases to sense the internal state of the |fuchsia::ui::policy::Presentation|:
//   + Connection status
//   + Peer (holder of the |ViewToken|) connection status
//   + The |ViewHolderToken| provided
class FakePresentation : public fuchsia::ui::policy::testing::Presentation_TestBase {
 public:
  FakePresentation(fuchsia::ui::views::ViewHolderToken view_holder_token,
                   fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request);
  ~FakePresentation() override;

  bool bound() const { return binding_.is_bound(); }
  bool peer_disconnected() const { return token_peer_disconnected_; }
  const fuchsia::ui::views::ViewHolderToken& token() const { return token_; }

  // |fuchsia::ui::policy::testing::Presentation_TestBase|
  void NotImplemented_(const std::string& name) override;

 private:
  std::unique_ptr<async::Wait> token_waiter_;

  fidl::Binding<fuchsia::ui::policy::Presentation> binding_;
  fuchsia::ui::views::ViewHolderToken token_;

  bool token_peer_disconnected_ = false;
};

// This fake interface can stand in for a |fuchsia::ui::policy::Presenter| in unit or integration
// tests. Normally `root_presenter` vends this interface.
//
// It allows test cases to sense the internal state of the |fuchsia::ui::policy::Presenter|:
//   + Connection status
//   + The |fuchsia::ui::policy::Presentation|, if any
class FakePresenter : public fuchsia::ui::policy::testing::Presenter_TestBase {
 public:
  FakePresenter();
  ~FakePresenter() override;

  bool bound() const { return binding_.is_bound(); }
  const std::optional<FakePresentation>& presentation() const { return presentation_; }

  fidl::InterfaceRequestHandler<fuchsia::ui::policy::Presenter> GetHandler();

  // |fuchsia::ui::policy::Presenter|
  void PresentView(
      fuchsia::ui::views::ViewHolderToken view_holder_token,
      fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) override;

  // |fuchsia::ui::policy::Presenter|
  void PresentOrReplaceView(
      fuchsia::ui::views::ViewHolderToken view_holder_token,
      fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) override;

  // |fuchsia::ui::policy::testing::Presenter_TestBase|
  void NotImplemented_(const std::string& name) override;

 private:
  std::optional<FakePresentation> presentation_;

  fidl::Binding<fuchsia::ui::policy::Presenter> binding_;
};

}  // namespace present_view::testing

#endif  // SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_PRESENTER_H_
