// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_VIEW_H_
#define SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_VIEW_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl_test_base.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl_test_base.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/zx/eventpair.h>

#include <memory>
#include <string>

namespace present_view::testing {

constexpr char kNonexistentViewUri[] = "file://nonexistent_view.cmx";
constexpr char kFakeViewUri[] = "file://fake_view.cmx";

// This abstract base class can stand in for a |fuchsia::ui::views::View| or a
// |fuchsia::ui::app::ViewProvider| in tests.
// Normally a component which wants to be displayed by `scenic` vends this interface.
//
// Client code should not instantiate an instance of this class directly; instead use
// |FakeUnitTestView| or |FakeIntegrationTestView|, depending on the test type.
//
// This class allows test cases to sense the internal state of the |fuchsia::ui::views::View|
// or |fuchsia::ui::app::ViewProvider|:
//   + Connection status
//   + Peer (holder of the |ViewHolderToken|) connection status
//   + The |ViewToken| provided
class FakeView : public fuchsia::ui::app::testing::ViewProvider_TestBase,
                 public fuchsia::ui::views::testing::View_TestBase {
 public:
  ~FakeView() override;

  bool bound() const { return legacy_binding_.is_bound() || binding_.is_bound(); }
  bool peer_disconnected() const { return token_peer_disconnected_; }
  bool killed() const { return killed_; }
  const fuchsia::ui::views::ViewToken& token() const { return token_; }

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair view_token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override;

  // |fuchsia::ui::app::ViewProvider|
  void CreateViewWithViewRef(zx::eventpair view_token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override;

  // |fuchsia::ui::views::View|
  void Present(fuchsia::ui::views::ViewToken view_token) override;

  // |fuchsia::ui::app::testing::ViewProvider_TestBase|
  // |fuchsia::ui::views::testing::View_TestBase|
  void NotImplemented_(const std::string& name) override;

 protected:
  FakeView();

  void BindLegacy(fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request);
  void Bind(fidl::InterfaceRequest<fuchsia::ui::views::View> request);
  void OnKill();

 private:
  std::unique_ptr<async::Wait> token_waiter_;

  fidl::Binding<fuchsia::ui::app::ViewProvider> legacy_binding_;
  fidl::Binding<fuchsia::ui::views::View> binding_;
  fuchsia::ui::views::ViewToken token_;

  bool token_peer_disconnected_ = false;
  bool killed_ = false;
};

}  // namespace present_view::testing

#endif  // SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_VIEW_H_
