// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>
#include <ddk/protocol/wlan.h>
#include <magenta/compiler.h>
#include <magenta/types.h>

#include <mutex>

namespace wlan {
namespace testing {

class Device {
  public:
    Device(mx_driver_t* driver, mx_device_t* device, test_protocol_t* test_ops);

    mx_status_t Bind();

  private:
    // ddk/device protocol
    void Unbind();
    mx_status_t Release();
    ssize_t Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len);

    // ddk/wlan protocol
    mx_status_t Query(uint32_t options, ethmac_info_t* info);
    void Stop();
    mx_status_t Start(wlanmac_ifc_t* ifc, void* cookie);
    void Send(uint32_t options, void* data, size_t length);
    mx_status_t SetChannel(uint32_t options, wlan_channel_t* chan);

    mx_driver_t* driver_;
    mx_device_t* test_device_;
    test_protocol_t* test_ops_;

    mx_device_t* device_;
    mx_protocol_device_t device_ops_ = {};
    wlanmac_protocol_t wlanmac_ops_ = {};

    std::mutex lock_;
    wlanmac_ifc_t* ifc_ __TA_GUARDED(lock_) = nullptr;
    void* cookie_ __TA_GUARDED(lock_) = nullptr;
};

}  // namespace testing
}  // namespace wlan
