// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harvester.h"

#include <fcntl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/async-testutils/dispatcher_stub.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include "dockyard_proxy_fake.h"
#include "gtest/gtest.h"

namespace {

zx_status_t get_root_resource(zx_handle_t* root_resource) {
  const char* sysinfo = "/dev/misc/sysinfo";
  int fd = open(sysinfo, O_RDWR);
  if (fd < 0) {
    return ZX_ERR_NOT_FOUND;
  }

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetRootResource(
      channel.get(), &status, root_resource);
  if (fidl_status != ZX_OK) {
    return fidl_status;
  } else if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

class AsyncDispatcherFake : public async::DispatcherStub {
 public:
  zx::time Now() override { return current_time_; }
  void SetTime(zx::time t) { current_time_ = t; }

 private:
  zx::time current_time_;
};

TEST(SystemMonitorHarvesterCpuSampleCollection, True) {
  std::unique_ptr<harvester::DockyardProxyFake> dockyard_proxy =
      std::make_unique<harvester::DockyardProxyFake>();

  zx_handle_t root_resource;
  zx_status_t ret = get_root_resource(&root_resource);
  EXPECT_EQ(ret, ZX_OK);

  AsyncDispatcherFake dispatcher;

  harvester::Harvester test(zx::msec(4000), root_resource, &dispatcher,
                            std::move(dockyard_proxy));

  test.GatherData();
}

}  // namespace
