// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-device.h"

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>

#include <zircon/status.h>

#include <cstdio>
#include <future>
#include <memory>
#include <thread>

extern "C" zx_status_t qmi_fake_bind(void* ctx, zx_device_t* device) {

  test_protocol_t proto;
  auto status = device_get_protocol(device, ZX_PROTOCOL_TEST, &proto);

  if (status != ZX_OK) {
    std::printf("qmi_fake_bind: failed protocol: %s\n",
                zx_status_get_string(status));
    return status;
  }

  auto dev = std::make_unique<qmi_fake::Device>(device);
  status = dev->Bind();
  std::printf("%s\n", __func__);
  if (status != ZX_OK) {
    std::printf("qmi_fake_bind: could not bind: %d\n", status);
  } else {
    dev.release();
  }

  return status;
}
