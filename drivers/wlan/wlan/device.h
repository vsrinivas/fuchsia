// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device_interface.h"
#include "mlme.h"
#include "packet.h"

#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <ddktl/protocol/wlan.h>
#include <magenta/compiler.h>
#include <mx/channel.h>
#include <mx/port.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>
#include <fbl/unique_ptr.h>

#include <mutex>
#include <thread>

typedef struct mx_port_packet mx_port_packet_t;

namespace wlan {

class Timer;

class Device;
using WlanBaseDevice = ddk::Device<Device, ddk::Unbindable, ddk::Ioctlable>;

class Device : public WlanBaseDevice,
               public ddk::EthmacProtocol<Device>,
               public ddk::WlanmacIfc<Device>,
               public DeviceInterface {
  public:
    Device(mx_device_t* device, wlanmac_protocol_t* wlanmac_proto);
    ~Device();

    mx_status_t Bind();

    // ddk::Device methods
    void DdkUnbind();
    void DdkRelease();
    mx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual);

    // ddk::WlanmacIfc methods
    void WlanmacStatus(uint32_t status);
    void WlanmacRecv(uint32_t flags, const void* data, size_t length, wlan_rx_info_t* info);

    // ddk::EthmacProtocol methods
    mx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info);
    mx_status_t EthmacStart(fbl::unique_ptr<ddk::EthmacIfcProxy> proxy) __TA_EXCLUDES(lock_);
    void EthmacStop() __TA_EXCLUDES(lock_);
    void EthmacSend(uint32_t options, void* data, size_t length);

    // DeviceInterface methods
    mx_status_t GetTimer(uint64_t id, fbl::unique_ptr<Timer>* timer) override final;
    mx_status_t SendEthernet(fbl::unique_ptr<Packet> packet) override final;
    mx_status_t SendWlan(fbl::unique_ptr<Packet> packet) override final;
    mx_status_t SendService(fbl::unique_ptr<Packet> packet) override final;
    mx_status_t SetChannel(wlan_channel_t chan) override final;
    mx_status_t SetStatus(uint32_t status) override final;
    fbl::RefPtr<DeviceState> GetState() override final;

  private:
    enum class DevicePacket : uint64_t {
        kShutdown,
        kPacketQueued,
    };

    fbl::unique_ptr<Packet> PreparePacket(const void* data, size_t length, Packet::Peer peer);
    template <typename T>
    fbl::unique_ptr<Packet> PreparePacket(const void* data, size_t length, Packet::Peer peer,
                                           const T& ctrl_data) {
        auto packet = PreparePacket(data, length, peer);
        if (packet != nullptr) {
            packet->CopyCtrlFrom(ctrl_data);
        }
        return packet;
    }

    mx_status_t QueuePacket(fbl::unique_ptr<Packet> packet) __TA_EXCLUDES(packet_queue_lock_);

    void MainLoop();
    void ProcessChannelPacketLocked(const mx_port_packet_t& pkt) __TA_REQUIRES(lock_);
    mx_status_t RegisterChannelWaitLocked() __TA_REQUIRES(lock_);
    mx_status_t QueueDevicePortPacket(DevicePacket id);

    mx_status_t GetChannel(mx::channel* out) __TA_EXCLUDES(lock_);
    void SetStatusLocked(uint32_t status);

    ddk::WlanmacProtocolProxy wlanmac_proxy_;
    fbl::unique_ptr<ddk::EthmacIfcProxy> ethmac_proxy_;

    ethmac_info_t ethmac_info_ = {};
    fbl::RefPtr<DeviceState> state_;

    std::mutex lock_;
    std::thread work_thread_;
    mx::port port_;

    Mlme mlme_ __TA_GUARDED(lock_);

    bool dead_ __TA_GUARDED(lock_) = false;
    mx::channel channel_ __TA_GUARDED(lock_);

    std::mutex packet_queue_lock_;
    PacketQueue packet_queue_ __TA_GUARDED(packet_queue_lock_);
};

}  // namespace wlan
