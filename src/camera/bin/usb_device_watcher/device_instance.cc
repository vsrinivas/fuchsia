// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/usb_device_watcher/device_instance.h"

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <lib/sys/service/cpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include "fuchsia/io/cpp/fidl.h"
#include "lib/fit/function.h"
#include "lib/sys/cpp/service_directory.h"

namespace camera {

fpromise::result<std::unique_ptr<DeviceInstance>, zx_status_t> DeviceInstance::Create(
    fuchsia::hardware::camera::DeviceHandle camera, const fuchsia::component::RealmPtr& realm,
    async_dispatcher_t* dispatcher, const std::string& collection_name,
    const std::string& child_name, const std::string& url) {
  auto instance = std::make_unique<DeviceInstance>();
  instance->dispatcher_ = dispatcher;
  instance->name_ = child_name;

  // Bind the camera channel.
  zx_status_t status = instance->camera_.Bind(std::move(camera), instance->dispatcher_);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fpromise::error(status);
  }
  instance->camera_.set_error_handler([instance = instance.get()](zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Camera device server disconnected.";
    instance->camera_ = nullptr;
  });

  // Launch the child device.
  fuchsia::component::decl::CollectionRef collection;
  collection.name = collection_name;
  fuchsia::component::decl::Child child;
  child.set_name(child_name);
  child.set_url(url);
  child.set_startup(fuchsia::component::decl::StartupMode::LAZY);
  fuchsia::component::CreateChildArgs args;
  fuchsia::component::Realm::CreateChildCallback cb =
      [child_name](fuchsia::component::Realm_CreateChild_Result result) {
        if (result.is_err()) {
          FX_LOGS(ERROR) << "Failed to create camera device child. Result: "
                         << static_cast<long>(result.err());
          ZX_ASSERT(false);  // Should never happen.
        }
        FX_LOGS(INFO) << "Created camera device child: " << child_name;
      };
  realm->CreateChild(std::move(collection), std::move(child), std::move(args), std::move(cb));
  return fpromise::ok(std::move(instance));
}

}  // namespace camera
