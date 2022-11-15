// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock-display-device-tree.h"

#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/graphics/display/drivers/fake/fake-display.h"

namespace display {

#define ZXLOG(level, fmt, ...) zxlogf(level, "[%s:%u]: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

MockDisplayDeviceTree::MockDisplayDeviceTree(std::shared_ptr<zx_device> mock_root,
                                             std::unique_ptr<SysmemDeviceWrapper> sysmem,
                                             bool start_vsync)
    : mock_root_(mock_root), sysmem_(std::move(sysmem)) {
  pdev_.UseFakeBti();
  mock_root_->SetMetadata(SYSMEM_METADATA_TYPE, &sysmem_metadata_, sizeof(sysmem_metadata_));

  // Protocols for sysmem
  mock_root_->AddProtocol(ZX_PROTOCOL_PDEV, pdev_.proto()->ops, pdev_.proto()->ctx);

  if (auto result = sysmem_->Bind(); result != ZX_OK) {
    ZXLOG(ERROR, "sysmem_.Bind() return status was not ZX_OK. Error: %s.",
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
  mock_root_->AddProtocol(ZX_PROTOCOL_PDEV, pdev_.proto()->ops, pdev_.proto()->ctx, "pdev");
  mock_root_->AddProtocol(ZX_PROTOCOL_SYSMEM, sysmem_->proto()->ops, sysmem_->proto()->ctx,
                          "sysmem");

  display_ = new fake_display::FakeDisplay(mock_root_.get());
  if (auto status = display_->Bind(start_vsync); status != ZX_OK) {
    ZXLOG(ERROR, "display_->Bind(start_vsync) return status was not ZX_OK. Error: %s.",
          zx_status_get_string(status));
    return;
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
    ZXLOG(ERROR, "c->Bind(&c) return status was not ZX_OK. Error: %s.",
          zx_status_get_string(status));
    return;
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
