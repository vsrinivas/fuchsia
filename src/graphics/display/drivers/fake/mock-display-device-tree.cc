// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock-display-device-tree.h"

#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>

#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/graphics/display/drivers/fake/fake-display.h"

namespace display {

MockDisplayDeviceTree::MockDisplayDeviceTree(std::shared_ptr<zx_device> mock_root,
                                             std::unique_ptr<SysmemDeviceWrapper> sysmem,
                                             bool start_vsync)
    : mock_root_(mock_root),
      sysmem_(std::move(sysmem)),
      outgoing_(component::OutgoingDirectory::Create(pdev_loop_.dispatcher())) {
  pdev_fidl_.UseFakeBti();
  pdev_banjo_.UseFakeBti();
  mock_root_->SetMetadata(SYSMEM_METADATA_TYPE, &sysmem_metadata_, sizeof(sysmem_metadata_));

  // Protocols for sysmem
  fidl::ClientEnd<fuchsia_io::Directory> client = SetUpPDevFidlServer();
  mock_root_->AddFidlService(fuchsia_hardware_platform_device::Service::Name, std::move(client));
  pdev_loop_.StartThread("pdev-server-thread");

  if (auto result = sysmem_->Bind(); result != ZX_OK) {
    ZX_PANIC("sysmem_.Bind() return status was not ZX_OK. Error: %s.",
             zx_status_get_string(result));
  }
  sysmem_driver::Device* sysmem_device =
      mock_root_->GetLatestChild()->GetDeviceContext<sysmem_driver::Device>();
  auto sysmem_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::DriverConnector>();
  fidl::BindServer(sysmem_loop_.dispatcher(), std::move(sysmem_endpoints->server), sysmem_device);
  sysmem_loop_.StartThread("sysmem-server-thread");
  sysmem_client_ =
      fidl::WireSyncClient<fuchsia_sysmem::DriverConnector>(std::move(sysmem_endpoints->client));

  // Fragment for fake-display
  mock_root_->AddProtocol(ZX_PROTOCOL_PDEV, pdev_banjo_.proto()->ops, pdev_banjo_.proto()->ctx,
                          "pdev");
  mock_root_->AddProtocol(ZX_PROTOCOL_SYSMEM, sysmem_->proto()->ops, sysmem_->proto()->ctx,
                          "sysmem");

  display_ = new fake_display::FakeDisplay(mock_root_.get());
  if (auto status = display_->Bind(start_vsync); status != ZX_OK) {
    ZX_PANIC("display_->Bind(start_vsync) return status was not ZX_OK. Error: %s.",
             zx_status_get_string(status));
  }
  zx_device_t* mock_display = mock_root_->GetLatestChild();

  // Protocols for display controller.
  mock_display->AddProtocol(ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL, display_->dcimpl_proto()->ops,
                            display_->dcimpl_proto()->ctx);
  mock_display->AddProtocol(ZX_PROTOCOL_DISPLAY_CAPTURE_IMPL, display_->capture_proto()->ops,
                            display_->capture_proto()->ctx);
  mock_display->AddProtocol(ZX_PROTOCOL_DISPLAY_CLAMP_RGB_IMPL,
                            display_->clamp_rgbimpl_proto()->ops,
                            display_->clamp_rgbimpl_proto()->ctx);

  std::unique_ptr<display::Controller> c(new Controller(mock_display));
  // Save a copy for test cases.
  controller_ = c.get();
  if (auto status = c->Bind(&c); status != ZX_OK) {
    ZX_PANIC("c->Bind(&c) return status was not ZX_OK. Error: %s.", zx_status_get_string(status));
  }

  auto display_endpoints = fidl::CreateEndpoints<fuchsia_hardware_display::Provider>();
  fidl::BindServer(display_loop_.dispatcher(), std::move(display_endpoints->server), controller_);
  display_loop_.StartThread("display-server-thread");
  display_provider_client_ = fidl::WireSyncClient<fuchsia_hardware_display::Provider>(
      std::move(display_endpoints->client));
}

MockDisplayDeviceTree::~MockDisplayDeviceTree() {
  // AsyncShutdown() must be called before ~MockDisplayDeviceTree().
  ZX_ASSERT(shutdown_);
}

fidl::ClientEnd<fuchsia_io::Directory> MockDisplayDeviceTree::SetUpPDevFidlServer() {
  component::ServiceInstanceHandler handler;
  fuchsia_hardware_platform_device::Service::Handler service(&handler);

  auto device_handler = [this](fidl::ServerEnd<fuchsia_hardware_platform_device::Device> request) {
    fidl::BindServer(pdev_loop_.dispatcher(), std::move(request), &pdev_fidl_);
  };
  auto service_result = service.add_device(device_handler);
  ZX_ASSERT((service_result.is_ok()));
  service_result =
      outgoing_.AddService<fuchsia_hardware_platform_device::Service>(std::move(handler));
  ZX_ASSERT(service_result.is_ok());

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ZX_ASSERT(endpoints.is_ok());
  ZX_ASSERT(outgoing_.Serve(std::move(endpoints->server)).is_ok());

  return std::move(endpoints->client);
}

zx::unowned_channel MockDisplayDeviceTree::display_client() {
  return display_provider_client_.client_end().borrow().channel();
}

fidl::UnownedClientEnd<fuchsia_sysmem::DriverConnector> MockDisplayDeviceTree::sysmem_client() {
  return sysmem_client_.client_end().borrow();
}

void MockDisplayDeviceTree::AsyncShutdown() {
  if (shutdown_) {
    // AsyncShutdown() was already called.
    return;
  }
  shutdown_ = true;

  display_->DdkChildPreRelease(controller_);
  controller_->DdkAsyncRemove();
  display_->DdkAsyncRemove();
  mock_ddk::ReleaseFlaggedDevices(mock_root_.get());
}

}  // namespace display
