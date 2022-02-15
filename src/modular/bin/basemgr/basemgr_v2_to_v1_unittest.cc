// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>

#include "src/modular/bin/basemgr/basemgr_impl_test_fixture.h"

namespace modular {

class BasemgrV2ToV1Test : public BasemgrImplTestFixture {};

// Tests that basemgr can proxy a FIDL service from a CFv2 component to
// CFv1 sessionmgr via its |v2_services_for_sessionmgr|.
TEST_F(BasemgrV2ToV1Test, EchoServerIsUsed) {
  FakeSessionmgr sessionmgr{fake_launcher_};

  CreateBasemgrImpl(DefaultConfig());

  auto config_buf = BufferFromString(modular::ConfigToJsonString(DefaultConfig()));

  // Launch the session
  auto session_launcher = GetSessionLauncher();
  session_launcher->LaunchSessionmgr(std::move(config_buf));

  // sessionmgr should be started and initialized.
  RunLoopUntil([&]() { return sessionmgr.initialized(); });

  // sessionmgr should have received the service in
  // |v2_services_for_sessionmgr|
  auto& services = sessionmgr.v2_services_for_sessionmgr().value();
  ASSERT_EQ(1u, services.names.size());

  // Connect to a service that was designated in this test component's CML as a
  // "svc_for_v1_sessionmgr", and made available via sessionmgr's
  // |v2_services_for_sessionmgr|.
  //
  // This test uses one of the fuchsia.git example Echo services.
  //
  // NOTE: Beware, there are multiple echo service implementations, and the FIDL
  // and component paths vary. Make sure all fully-qualified names of both the
  // FIDL service protocol and the component are consistent across this test
  // component's BUILD.gn, CML, #includes, and C++ namespaces and identifiers.
  sys::ServiceDirectory service_dir{std::move(services.host_directory)};
  auto echo = service_dir.Connect<fuchsia::examples::Echo>();
  zx_status_t status = ZX_OK;
  echo.set_error_handler([&](zx_status_t error) { status = error; });

  // NOTE: Be careful not to exceed the fidl-declared MAX_STRING_LENGTH for the
  // message parameter.
  const std::string message = "hello from echo... echo...";

  std::string ret_msg;
  echo->EchoString(message, [&](fidl::StringPtr retval) { ret_msg = retval.value_or(""); });

  RunLoopUntil([&] { return ret_msg == message || status != ZX_OK; });

  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "FIDL request failed";
  }

  basemgr_impl_->Terminate();
  RunLoopUntil([&]() { return did_shut_down_; });
}

}  // namespace modular
