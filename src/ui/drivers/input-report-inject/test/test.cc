// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/input/inject/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/input2/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <unistd.h>

#include "src/ui/lib/hid-input-report/fidl.h"

namespace fuchsia_input_report = ::fuchsia::input::report;
namespace fuchsia_input_inject = ::fuchsia::input::inject;

constexpr char kDevmgrPkgUrl[] =
    "fuchsia-pkg://fuchsia.com/input-inject-devmgr#meta/input-inject-devmgr.cmx";
constexpr char kIsolatedDevmgrServiceName[] = "fuchsia.input.InputInjectDevmgr";

class InputInjectTest : public sys::testing::TestWithEnvironment {
 public:
  void SetUp() override;

  void ConnectToFile(const char* path, zx::channel* chan) {
    zx::channel c1;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, chan));
    ASSERT_EQ(ZX_OK, fdio_service_connect_at(devfs_dir_.channel().get(), path, c1.release()));
  }

  void ConnectToInject(zx::channel* chan) { ConnectToFile("misc/InputReportInject", chan); }

  fidl::InterfacePtr<fuchsia::sys::ComponentController> ctrl_;
  fidl::InterfaceHandle<fuchsia::io::Directory> devfs_dir_;
};

void InputInjectTest::SetUp() {
  auto ctx = sys::ComponentContext::Create();
  fidl::InterfacePtr<fuchsia::sys::Launcher> launcher;
  ctx->svc()->Connect(launcher.NewRequest());

  zx::channel req;
  auto services = sys::ServiceDirectory::CreateWithRequest(&req);

  fuchsia::sys::LaunchInfo info;
  info.directory_request = std::move(req);
  info.url = kDevmgrPkgUrl;

  launcher->CreateComponent(std::move(info), ctrl_.NewRequest());
  ctrl_.set_error_handler([](zx_status_t err) { ASSERT_TRUE(false); });

  fuchsia::io::DirectorySyncPtr devfs_dir;
  services->Connect(devfs_dir.NewRequest(), kIsolatedDevmgrServiceName);
  fuchsia::io::NodeInfo node_info;
  zx_status_t status = devfs_dir->Describe(&node_info);
  ASSERT_EQ(ZX_OK, status);

  devfs_dir_ = devfs_dir.Unbind();
}

TEST_F(InputInjectTest, MouseTest) {
  zx_status_t status;
  zx::channel chan;
  ASSERT_NO_FATAL_FAILURE(ConnectToInject(&chan));
  auto inject_client = ::fuchsia::input::inject::FakeInputReportDevice_SyncProxy(std::move(chan));

  // Create the Fake Input Device.
  ::fuchsia::input::report::DeviceDescriptor descriptor;
  {
    ::fuchsia::input::report::MouseInputDescriptor mouse_input;
    ::fuchsia::input::report::Axis axis;
    axis.range.min = -126;
    axis.range.max = 126;
    axis.unit = ::fuchsia::input::report::Unit::DISTANCE;
    mouse_input.set_movement_x(axis);
    mouse_input.set_movement_y(axis);
    mouse_input.set_buttons(std::vector<uint8_t>({1, 2}));

    ::fuchsia::input::report::MouseDescriptor mouse;
    mouse.set_input(std::move(mouse_input));
    descriptor.set_mouse(std::move(mouse));
  }

  {
    fuchsia::input::inject::FakeInputReportDevice_MakeDevice_Result result;

    ::fuchsia::input::report::DeviceDescriptor copy_descriptor;
    ASSERT_EQ(ZX_OK, descriptor.Clone(&copy_descriptor));
    status = inject_client.MakeDevice(std::move(copy_descriptor), &result);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_FALSE(result.is_err());
  }

  ASSERT_NO_FATAL_FAILURE(ConnectToFile("class/input-report/000", &chan));
  auto input_client = ::fuchsia::input::report::InputDevice_SyncProxy(std::move(chan));

  // Check that the descriptor matches the one we sent.
  {
    fuchsia::input::report::DeviceDescriptor input_descriptor;
    ASSERT_EQ(ZX_OK, input_client.GetDescriptor(&input_descriptor));

    ASSERT_TRUE(fidl::Equals(descriptor, input_descriptor));
  }

  // Make a report.
  ::fuchsia::input::report::InputReport report;
  {
    ::fuchsia::input::report::MouseInputReport mouse_report;
    mouse_report.set_movement_x(100);
    mouse_report.set_movement_y(-100);
    mouse_report.set_pressed_buttons(std::vector<uint8_t>({1}));
    report.set_mouse(std::move(mouse_report));
  }

  // Send a report.
  {
    std::vector<::fuchsia::input::report::InputReport> report_vector(1);
    ASSERT_EQ(ZX_OK, report.Clone(&report_vector[0]));

    fuchsia::input::inject::FakeInputReportDevice_SendInputReports_Result result;
    ASSERT_EQ(ZX_OK, inject_client.SendInputReports(std::move(report_vector), &result));
    ASSERT_EQ(ZX_OK, status);
    ASSERT_FALSE(result.is_err());
  }

  // Check the report matches the one we sent.
  {
    std::vector<::fuchsia::input::report::InputReport> returned_reports;
    ASSERT_EQ(ZX_OK, input_client.GetReports(&returned_reports));
    ASSERT_EQ(1U, returned_reports.size());

    ASSERT_TRUE(fidl::Equals(returned_reports[0], report));
  }
}
