// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/bin/vulkan_loader/goldfish_device.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>
#include <lib/syslog/cpp/macros.h>

#include "src/graphics/bin/vulkan_loader/app.h"

// static
std::unique_ptr<GoldfishDevice> GoldfishDevice::Create(LoaderApp* app, int dir_fd, std::string name,
                                                       inspect::Node* parent) {
  std::unique_ptr<GoldfishDevice> device(new GoldfishDevice(app));
  if (!device->Initialize(dir_fd, name, parent))
    return nullptr;
  return device;
}

bool GoldfishDevice::Initialize(int dir_fd, std::string name, inspect::Node* parent) {
  node() = parent->CreateChild("goldfish-" + name);
  icd_list_.Initialize(&node());
  auto pending_action_token = app()->GetPendingActionToken();
  fdio_t* dir_fdio = fdio_unsafe_fd_to_io(dir_fd);
  if (!dir_fdio) {
    FX_LOGS(ERROR) << "Failed to get fdio_t";
    return false;
  }
  zx_handle_t dir_handle;
  dir_handle = fdio_unsafe_borrow_channel(dir_fdio);
  if (!dir_handle) {
    FX_LOGS(ERROR) << "Failed to borrow channel";
    return false;
  }

  zx_status_t status;
  status = fdio_service_connect_at(dir_handle, name.c_str(),
                                   device_.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to connect to service";
    return false;
  }
  fdio_unsafe_release(dir_fdio);
  device_.set_error_handler([this](zx_status_t status) {
    // Deletes |this|.
    app()->RemoveDevice(this);
  });

  IcdData data;
  data.node = node().CreateChild("0");
  std::string component_url = "fuchsia-pkg://fuchsia.com/libvulkan_goldfish#meta/vulkan.cm";
  data.node.CreateString("component_url", component_url, &data.values);

  icd_list_.Add(app()->CreateIcdComponent(component_url));
  icds().push_back(std::move(data));
  return true;
}
