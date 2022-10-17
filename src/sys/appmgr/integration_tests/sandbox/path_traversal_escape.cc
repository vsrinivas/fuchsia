// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.sys/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/sys/component/cpp/service_client.h>

#include <gtest/gtest.h>

// Attempt to instantiate a CFv1 component with a non-canonical path entry in
// the sandbox section. This should fail with kInternalError.
TEST(NamespaceTest, PathTraversalEscapeFails) {
  auto svc = component::OpenServiceRoot();
  ASSERT_TRUE(svc.is_ok());

  auto launcher_end = component::ConnectAt<fuchsia_sys::Launcher>(*svc);
  ASSERT_EQ(ZX_OK, launcher_end.status_value());

  constexpr std::string_view child_url =
      "fuchsia-pkg://fuchsia.com/path-traversal-escape#meta/path-traversal-escape-child.cmx";
  fuchsia_sys::wire::LaunchInfo launch_info{
      .url = fidl::StringView::FromExternal(child_url),
  };

  zx::result component_controller_endpoints =
      fidl::CreateEndpoints<fuchsia_sys::ComponentController>();
  ASSERT_EQ(ZX_OK, component_controller_endpoints.status_value());
  auto [component_controller_client, component_controller_server] =
      std::move(*component_controller_endpoints);

  fidl::WireSyncClient launcher{std::move(*launcher_end)};
  fidl::WireResult create_component_result =
      launcher->CreateComponent(std::move(launch_info), std::move(component_controller_server));
  ASSERT_EQ(ZX_OK, create_component_result.status());

  class EventHandler : public fidl::WireSyncEventHandler<fuchsia_sys::ComponentController> {
   public:
    void OnDirectoryReady(
        ::fidl::WireEvent<::fuchsia_sys::ComponentController::OnDirectoryReady>* event) final {
      FAIL();
    }

    void OnTerminated(
        ::fidl::WireEvent<::fuchsia_sys::ComponentController::OnTerminated>* event) final {
      EXPECT_EQ(event->termination_reason, fuchsia_sys::TerminationReason::kInternalError);
    }
  };

  EventHandler handler;
  ASSERT_EQ(ZX_OK, handler.HandleOneEvent(component_controller_client).status());
}
