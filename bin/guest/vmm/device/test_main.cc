// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <fbl/unique_fd.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <gtest/gtest.h>
#include <lib/fdio/util.h>
#include <lib/fxl/logging.h>
#include <zx/channel.h>
#include <zx/guest.h>
#include <zx/vmar.h>

zx_status_t hypervisor_supported() {
  fbl::unique_fd fd(open("/dev/misc/sysinfo", O_RDWR));
  if (!fd) {
    return ZX_ERR_IO;
  }

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd.release(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  zx::resource resource;
  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetHypervisorResource(
      channel.get(), &status, resource.reset_and_get_address());
  if (fidl_status != ZX_OK) {
    return fidl_status;
  } else if (status != ZX_OK) {
    return status;
  }

  zx::guest guest;
  zx::vmar vmar;
  return zx::guest::create(resource, 0, &guest, &vmar);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  zx_status_t status = hypervisor_supported();
  if (status == ZX_ERR_NOT_SUPPORTED) {
    FXL_LOG(INFO) << "Hypervisor is not supported";
    return ZX_OK;
  } else if (status != ZX_OK) {
    return status;
  }

  return RUN_ALL_TESTS();
}
