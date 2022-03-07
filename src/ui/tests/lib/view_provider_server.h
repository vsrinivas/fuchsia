// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTS_LIB_VIEW_PROVIDER_SERVER_H_
#define SRC_UI_TESTS_LIB_VIEW_PROVIDER_SERVER_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

// This is an in-process server for the `fuchsia.ui.app.ViewProvider` API for this
// test.  It is required for this test to be able to define and set up its view
// as the root view in Scenic's scene graph.  The implementation does little more
// than to provide correct wiring of the FIDL API.  The test that uses it is
// expected to provide a closure via SetCreateView2Callback, which will get invoked
// when a message is received.
//
// Only Flatland methods are implemented, others will cause the server to crash
// the test deliberately.
class ViewProviderServer : public fuchsia::ui::app::ViewProvider,
                           public sys::testing::LocalComponent {
 public:
  explicit ViewProviderServer(async_dispatcher_t* dispatcher);

  // Start serving `ViewProvider` for the stream that arrives via `request`.
  void Bind(fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request);
  // Set this callback to direct where an incoming message from `CreateView2` will
  // get forwarded to.
  void SetCreateView2Callback(std::function<void(fuchsia::ui::app::CreateView2Args)> callback);

  // LocalComponent::Start
  void Start(std::unique_ptr<sys::testing::LocalComponentHandles> local_handles) override;

  // Gfx protocol is not implemented.
  void CreateView(
      ::zx::eventpair token,
      ::fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
      ::fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override;

  // Gfx protocol is not implemented.
  void CreateViewWithViewRef(zx::eventpair token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override;

  // Implements server-side `fuchsia.ui.app.ViewProvider/CreateView2`
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override;

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::vector<std::unique_ptr<sys::testing::LocalComponentHandles>> local_handles_;
  fidl::BindingSet<fuchsia::ui::app::ViewProvider> bindings_;

  std::function<void(fuchsia::ui::app::CreateView2Args)> create_view2_callback_ = nullptr;
};

#endif  // SRC_UI_TESTS_LIB_VIEW_PROVIDER_SERVER_H_
