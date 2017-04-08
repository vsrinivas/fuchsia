// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/wlan.h>
#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <ddktl/protocol/wlan.h>
#include <magenta/compiler.h>
#include <magenta/syscalls/port.h>
#include <mx/channel.h>
#include <mx/port.h>
#include <mxtl/unique_ptr.h>

#include <mutex>
#include <thread>

namespace wlan {

class Device;
using WlanBaseDevice = ddk::Device<Device, ddk::Unbindable, ddk::Ioctlable>;

class Device : public WlanBaseDevice,
               public ddk::EthmacProtocol<Device>,
               public ddk::WlanmacIfc<Device> {
  public:
    Device(mx_driver_t* driver, mx_device_t* device, wlanmac_protocol_t* wlanmac_ops);
    ~Device();

    mx_status_t Bind();

    void DdkUnbind();
    void DdkRelease();
    mx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual);

    void WlanmacStatus(uint32_t status);
    void WlanmacRecv(void* data, size_t length, uint32_t flags);

    mx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info);
    mx_status_t EthmacStart(mxtl::unique_ptr<ddk::EthmacIfcProxy> proxy) __TA_EXCLUDES(lock_);
    void EthmacStop() __TA_EXCLUDES(lock_);
    void EthmacSend(uint32_t options, void* data, size_t length);

  private:
    enum MsgKey : uint64_t {
        kShutdown = 1,
    };

    struct LoopMessage {
        explicit LoopMessage(MsgKey key, uint32_t extra = 0);

        mx_packet_header_t hdr;
        mx_packet_user_t data;
    };
    static_assert(std::is_standard_layout<LoopMessage>::value,
            "wlan::Device::LoopMessage must have standard layout");
    static_assert(sizeof(LoopMessage) <= sizeof(mx_port_packet_t),
            "wlan::Device::LoopMessage must fit in an mx_port_packet_t");

    void MainLoop();
    void ProcessChannelPacketLocked(const mx_port_packet_t& pkt) __TA_REQUIRES(lock_);
    mx_status_t RegisterChannelWaitLocked() __TA_REQUIRES(lock_);
    mx_status_t SendShutdown();

    mx_status_t GetChannel(mx::channel* out) __TA_EXCLUDES(lock_);

    std::mutex lock_;
    std::thread work_thread_;
    mx::port port_;

    ddk::WlanmacProtocolProxy wlanmac_proxy_;
    mxtl::unique_ptr<ddk::EthmacIfcProxy> ethmac_proxy_ __TA_GUARDED(lock_);

    ethmac_info_t ethmac_info_ = {};

    mx::channel channel_ __TA_GUARDED(lock_);
};

}  // namespace wlan
