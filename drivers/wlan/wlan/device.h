// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/wlan.h>
#include <magenta/compiler.h>
#include <magenta/syscalls/port.h>
#include <mx/channel.h>
#include <mx/port.h>

#include <mutex>
#include <thread>

namespace wlan {

class Device {
  public:
    Device(mx_driver_t* driver, mx_device_t* device, wlanmac_protocol_t* wlanmac_ops);
    ~Device();

    mx_status_t Bind();

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

    // ddk/device
    void Unbind();
    void Release();
    mx_status_t Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len,
                      size_t* out_actual);

    // ddk/protocol/ethernet
    mx_status_t Query(uint32_t options, ethmac_info_t* info);
    mx_status_t Start(ethmac_ifc_t* ifc, void* cookie) __TA_EXCLUDES(lock_);
    void Stop() __TA_EXCLUDES(lock_);
    void Send(uint32_t options, void* data, size_t length);

    // ddk/protocol/wlan
    void MacStatus(uint32_t status);
    void Recv(void* data, size_t length, uint32_t flags);

    void MainLoop();
    void ProcessChannelPacketLocked(const mx_port_packet_t& pkt) __TA_REQUIRES(lock_);
    mx_status_t RegisterChannelWaitLocked() __TA_REQUIRES(lock_);
    mx_status_t SendShutdown();

    mx_status_t GetChannel(mx::channel* out) __TA_EXCLUDES(lock_);

    std::mutex lock_;
    std::thread work_thread_;
    mx::port port_;

    mx_driver_t* driver_;

    // Generic ddk device
    mx_device_t* device_;
    mx_protocol_device_t device_ops_ = {};

    // Wlan mac device
    mx_device_t* wlanmac_device_;
    wlanmac_protocol_t* wlanmac_ops_;
    wlanmac_ifc_t wlanmac_ifc_ = {};

    // Ethermac interface
    ethmac_protocol_t ethmac_ops_ = {};
    ethmac_info_t ethmac_info_ = {};
    ethmac_ifc_t* ethmac_ifc_ __TA_GUARDED(lock_) = nullptr;
    void* ethmac_cookie_ __TA_GUARDED(lock_) = nullptr;

    mx::channel channel_ __TA_GUARDED(lock_);
};

}  // namespace wlan
