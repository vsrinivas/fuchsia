// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>
#include <ddktl/device.h>
#include <ddktl/protocol/test.h>
#include <ddktl/protocol/wlan.h>
#include <fbl/unique_ptr.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <mutex>

namespace wlan {
namespace testing {

class Device;
using TestBaseDevice = ddk::Device<Device, ddk::Unbindable, ddk::Ioctlable>;

class Device : public TestBaseDevice, public ddk::WlanmacProtocol<Device> {
   public:
    Device(zx_device_t* device, test_protocol_t* test_proto);

    zx_status_t Bind();

    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual);

    zx_status_t WlanmacQuery(uint32_t options, ethmac_info_t* info);
    void WlanmacStop();
    zx_status_t WlanmacStart(fbl::unique_ptr<ddk::WlanmacIfcProxy> proxy);
    void WlanmacTx(uint32_t options, const void* data, size_t length);
    zx_status_t WlanmacSetChannel(uint32_t options, wlan_channel_t* chan);

   private:
    ddk::TestProtocolProxy test_proxy_;

    std::mutex lock_;
    fbl::unique_ptr<ddk::WlanmacIfcProxy> wlanmac_proxy_ __TA_GUARDED(lock_);
};

}  // namespace testing
}  // namespace wlan
