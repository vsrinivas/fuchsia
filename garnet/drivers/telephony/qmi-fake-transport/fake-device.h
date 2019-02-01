// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>
#include <fbl/unique_ptr.h>

#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace qmi_fake {

class Device {
 public:
  Device(zx_device_t* device);

  zx_status_t Bind();
  void Unbind();
  void Release();

  zx_status_t Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                    void* out_buf, size_t out_len, size_t* out_actual);
  zx_status_t GetProtocol(uint32_t proto_id, void* out_proto);
  zx_status_t OpenChan(zx_handle_t* chan);

 private:
  zx_handle_t hold_chan_;

  zx_device_t* parent_;
  zx_device_t* zxdev_;
};

}  // namespace qmi_fake
