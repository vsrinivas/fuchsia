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
    // ddk/device
    void Unbind();
    mx_status_t Release();

    // ddk/protocol/ethernet
    mx_status_t Query(uint32_t options, ethmac_info_t* info);
    mx_status_t Start(ethmac_ifc_t* ifc, void* cookie) __TA_EXCLUDES(lock_);
    void Stop() __TA_EXCLUDES(lock_);
    void Send(uint32_t options, void* data, size_t length);

    // ddk/protocol/wlan
    void MacStatus(uint32_t status);
    void Recv(void* data, size_t length, uint32_t flags);

    enum MsgKey : uint64_t {
        kShutdown = 1,
    };
    struct loop_message {
        explicit loop_message(MsgKey key, uint32_t extra = 0);

        mx_packet_header_t hdr;
        mx_packet_user_t data;
    };
    static_assert(std::is_standard_layout<loop_message>::value,
            "loop_message must have standard layout");
    static_assert(sizeof(loop_message) <= sizeof(mx_port_packet_t),
            "loop_message must fit in an mx_port_packet_t");

    void MainLoop();
    mx_status_t SendShutdown();

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
};

}  // namespace wlan
