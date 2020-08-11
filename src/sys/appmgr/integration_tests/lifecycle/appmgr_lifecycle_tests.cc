// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <src/lib/files/directory.h>

#include "src/sys/appmgr/appmgr.h"
#include "src/sys/appmgr/moniker.h"

constexpr char kSysmgrUrl[] =
    "fuchsia-pkg://fuchsia.com/appmgr-lifecycle-tests#meta/test-sysmgr-bin.cmx";
constexpr char kLifecyleComponentUrl[] =
    "fuchsia-pkg://fuchsia.com/appmgr-lifecycle-tests#meta/test-lifecycle-component.cmx";
constexpr char kRootRealm[] = "app";

class AppmgrLifecycleTest : public sys::testing::TestWithEnvironment,
                            public fuchsia::sys::internal::LogConnectionListener {
 public:
  AppmgrLifecycleTest() = default;
  ~AppmgrLifecycleTest() = default;
  zx_status_t stop_callback_status_ = ZX_ERR_BAD_STATE;

  void SetUp() override {
    FX_LOGS(INFO) << "Setting up AppmgrLifecycleTest";
    std::unordered_set<component::Moniker> lifecycle_allowlist;
    lifecycle_allowlist.insert(
        component::Moniker{.url = kLifecyleComponentUrl, .realm_path = {kRootRealm}});

    fuchsia::sys::ServiceListPtr root_realm_services(new fuchsia::sys::ServiceList);

    zx::channel trace_client, trace_server;
    zx_status_t status = zx::channel::create(0, &trace_client, &trace_server);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to create tracing channel: " << status;
      return;
    }

    trace::TraceProvider trace_provider(std::move(trace_client), dispatcher());

    fuchsia::sys::LoaderPtr loader = real_services()->Connect<fuchsia::sys::Loader>();

    zx::channel appmgr_service_request;
    appmgr_services_ = sys::ServiceDirectory::CreateWithRequest(&appmgr_service_request);

    component::AppmgrArgs args{
        .pa_directory_request = appmgr_service_request.release(),
        .lifecycle_request = appmgr_lifecycle_.NewRequest().TakeChannel().release(),
        .lifecycle_allowlist = std::move(lifecycle_allowlist),
        .root_realm_services = std::move(root_realm_services),
        .environment_services = real_services(),
        .sysmgr_url = kSysmgrUrl,
        .sysmgr_args = {},
        .loader = std::optional<fuchsia::sys::LoaderPtr>(std::move(loader)),
        .run_virtual_console = false,
        .trace_server_channel = std::move(trace_server),
        .stop_callback = [this](zx_status_t status) { stop_callback_status_ = status; }};
    appmgr_ = std::make_unique<component::Appmgr>(dispatcher(), std::move(args));

    log_connector_.set_error_handler(
        [](zx_status_t status) { FX_PLOGS(INFO, status) << "Failed to connect to appmgr logs"; });

    status = appmgr_services_->Connect(log_connector_.NewRequest(),
                                       "appmgr_svc/fuchsia.sys.internal.LogConnector");
    FX_CHECK(status == ZX_OK);
    // Simulate the archivist connecting to the log listener so that appmgr will launch sysmgr
    log_connector_->TakeLogConnectionListener(log_binding_.GetHandler(this));
  }

  // |fuchsia::sys::internal::LogConnectionListener|
  void OnNewConnection(fuchsia::sys::internal::LogConnection connection) override {}

  std::unique_ptr<component::Appmgr> appmgr_;
  std::shared_ptr<sys::ServiceDirectory> appmgr_services_;
  fidl::BindingSet<fuchsia::sys::internal::LogConnectionListener> log_binding_;
  fuchsia::sys::internal::LogConnectorPtr log_connector_;
  fuchsia::process::lifecycle::LifecyclePtr appmgr_lifecycle_;
};

TEST_F(AppmgrLifecycleTest, LifecycleComponentGetsShutdownSignal) {
  // Launch TestLifecycleComponent
  zx::channel svc_request;
  auto svc_dir = sys::ServiceDirectory::CreateWithRequest(&svc_request);
  FX_CHECK(svc_request.is_valid());

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kLifecyleComponentUrl;
  launch_info.directory_request = std::move(svc_request);

  fuchsia::sys::ComponentControllerPtr controller;

  bool lifecycle_component_running = false;
  bool lifecycle_component_terminated = false;
  bool appmgr_terminated = false;

  controller.events().OnDirectoryReady = [&] {
    FX_LOGS(INFO) << "TestLifecycleComponent launch complete.";
    lifecycle_component_running = true;
  };
  controller.events().OnTerminated = [&](int64_t return_code,
                                         fuchsia::sys::TerminationReason reason) {
    FX_LOGS(INFO) << "TestLifecycleComponent termination complete.";

    lifecycle_component_terminated = true;
  };

  appmgr_->RootRealm()->CreateComponent(std::move(launch_info), controller.NewRequest());
  RunLoopUntil([&lifecycle_component_running] { return lifecycle_component_running; });

  appmgr_lifecycle_.set_error_handler([&](zx_status_t status) {
    FX_PLOGS(INFO, status) << "Appmgr Lifecycle channel closed.";
    ASSERT_EQ(status, ZX_OK);
    appmgr_terminated = true;
  });

  appmgr_lifecycle_->Stop();
  RunLoopUntil([&] { return lifecycle_component_terminated && appmgr_terminated; });
  ASSERT_EQ(ZX_OK, stop_callback_status_);
}

// Test that appmgr terminates if none of the components in the allowlist expose
// the lifecycle protocol.
TEST_F(AppmgrLifecycleTest, LifecycleNoShutdownComponents) {
  bool appmgr_terminated = false;
  appmgr_lifecycle_.set_error_handler([&](zx_status_t status) {
    FX_PLOGS(INFO, status) << "Appmgr Lifecycle channel closed.";
    ASSERT_EQ(status, ZX_OK);
    appmgr_terminated = true;
  });

  appmgr_lifecycle_->Stop();
  RunLoopUntil([&] { return appmgr_terminated; });
  ASSERT_EQ(ZX_OK, stop_callback_status_);
}
