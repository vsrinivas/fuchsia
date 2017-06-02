// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "mlme.h"
#include "packet.h"

#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <ddktl/protocol/wlan.h>
#include <magenta/compiler.h>
#include <mx/channel.h>
#include <mx/port.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/slab_allocator.h>
#include <mxtl/unique_ptr.h>

#include <mutex>
#include <thread>

typedef struct mx_port_packet mx_port_packet_t;

namespace wlan {

class Device;
using WlanBaseDevice = ddk::Device<Device, ddk::Unbindable, ddk::Ioctlable>;

class Device : public WlanBaseDevice,
               public ddk::EthmacProtocol<Device>,
               public ddk::WlanmacIfc<Device> {
  public:
    Device(mx_device_t* device, wlanmac_protocol_t* wlanmac_ops);
    ~Device();

    mx_status_t Bind();

    void DdkUnbind();
    void DdkRelease();
    mx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual);

    void WlanmacStatus(uint32_t status);
    void WlanmacRecv(uint32_t flags, const void* data, size_t length, wlan_rx_info_t* info);

    mx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info);
    mx_status_t EthmacStart(mxtl::unique_ptr<ddk::EthmacIfcProxy> proxy) __TA_EXCLUDES(lock_);
    void EthmacStop() __TA_EXCLUDES(lock_);
    void EthmacSend(uint32_t options, void* data, size_t length);

  private:
    mx_status_t QueuePacket(const void* data, size_t length, Packet::Source src)
        __TA_EXCLUDES(packet_queue_lock_);

    void MainLoop();
    bool ProcessChannelPacketLocked(const mx_port_packet_t& pkt) __TA_REQUIRES(lock_);
    mx_status_t RegisterChannelWaitLocked() __TA_REQUIRES(lock_);
    mx_status_t SendShutdown();

    mx_status_t GetChannel(mx::channel* out) __TA_EXCLUDES(lock_);
    void ResetChannelLocked() __TA_REQUIRES(lock_);

    mx_device_t* parent_;
    std::mutex lock_;
    std::thread work_thread_;
    mx::port port_;

    Mlme mlme_ __TA_GUARDED(lock_);

    mx::channel channel_ __TA_GUARDED(lock_);

    // TODO(tkilbourn): evaluate whether an instance or a static slab allocator makes more sense
    static constexpr size_t kNumSlabs = 2;
    mxtl::SlabAllocator<BufferAllocatorTraits> buffer_alloc_;

    std::mutex packet_queue_lock_;
    mxtl::DoublyLinkedList<mxtl::unique_ptr<Packet>> packet_queue_
        __TA_GUARDED(packet_queue_lock_);
};

}  // namespace wlan
