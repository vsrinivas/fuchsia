// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/bin/vulkan_loader/magma_device.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>
#include <lib/fit/thread_checker.h>
#include <lib/syslog/cpp/macros.h>

#include "src/graphics/bin/vulkan_loader/app.h"

// static
std::unique_ptr<MagmaDevice> MagmaDevice::Create(LoaderApp* app, int dir_fd, std::string name,
                                                 inspect::Node* parent) {
  std::unique_ptr<MagmaDevice> device(new MagmaDevice(app));
  if (!device->Initialize(dir_fd, name, parent))
    return nullptr;
  return device;
}

bool MagmaDevice::Initialize(int dir_fd, std::string name, inspect::Node* parent) {
  FIT_DCHECK_IS_THREAD_VALID(main_thread_);
  node() = parent->CreateChild("magma-" + name);
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
  status = fdio_open_at(dir_handle, name.c_str(), fuchsia::io::OPEN_RIGHT_READABLE,
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

  device_->GetIcdList([this, name, pending_action_token = std::move(pending_action_token)](
                          std::vector<fuchsia::gpu::magma::IcdInfo> icd_info) mutable {
    FIT_DCHECK_IS_THREAD_VALID(main_thread_);
    uint32_t i = 0;
    for (auto& icd : icd_info) {
      if (!icd.has_component_url()) {
        FX_LOGS(ERROR) << "ICD missing component URL";
        continue;
      }
      if (!icd.has_flags()) {
        FX_LOGS(ERROR) << "ICD missing flags";
        continue;
      }
      IcdData data;
      data.node = node().CreateChild(std::to_string(i++));
      data.node.CreateString("component_url", icd.component_url(), &data.values);
      data.node.CreateUint("flags", static_cast<uint32_t>(icd.flags()), &data.values);
      if (icd.flags().SUPPORTS_VULKAN) {
        icd_list_.Add(app()->CreateIcdComponent(icd.component_url()));
      }

      icds().push_back(std::move(data));
    }
  });
  return true;
}
