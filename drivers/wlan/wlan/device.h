// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "proxy_helpers.h"

#include <ddk/driver.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/channel.h>
#include <lib/zx/port.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/dispatcher.h>
#include <wlan/mlme/packet.h>
#include <zircon/compiler.h>

#include <mutex>
#include <thread>

typedef struct zx_port_packet zx_port_packet_t;

namespace wlan {

class Timer;

class Device : public DeviceInterface {
   public:
    Device(zx_device_t* device, wlanmac_protocol_t wlanmac_proto);
    ~Device();

    zx_status_t Bind();

    // ddk device methods
    void WlanUnbind();
    void WlanRelease();
    zx_status_t WlanIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                          size_t out_len, size_t* out_actual);
    void EthUnbind();
    void EthRelease();

    // ddk wlanmac_ifc_t methods
    void WlanmacStatus(uint32_t status);
    void WlanmacRecv(uint32_t flags, const void* data, size_t length, wlan_rx_info_t* info);
    void WlanmacCompleteTx(wlan_tx_packet_t* pkt, zx_status_t status);
    void WlanmacIndication(uint32_t ind);

    // ddk ethmac_protocol_ops methods
    zx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info);
    zx_status_t EthmacStart(ethmac_ifc_t* ifc, void* cookie) __TA_EXCLUDES(lock_);
    void EthmacStop() __TA_EXCLUDES(lock_);
    zx_status_t EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf);
    zx_status_t EthmacSetParam(uint32_t param, int32_t value, void* data);

    // DeviceInterface methods
    zx_status_t GetTimer(uint64_t id, fbl::unique_ptr<Timer>* timer) override final;
    zx_status_t SendEthernet(fbl::unique_ptr<Packet> packet) override final;
    zx_status_t SendWlan(fbl::unique_ptr<Packet> packet) override final;
    zx_status_t SendService(fbl::unique_ptr<Packet> packet) override final;
    zx_status_t SetChannel(wlan_channel_t chan) override final;
    zx_status_t SetStatus(uint32_t status) override final;
    zx_status_t ConfigureBss(wlan_bss_config_t* cfg) override final;
    zx_status_t EnableBeaconing(bool enabled) override final;
    zx_status_t ConfigureBeacon(fbl::unique_ptr<Packet> beacon) override final;
    zx_status_t SetKey(wlan_key_config_t* key_config) override final;
    fbl::RefPtr<DeviceState> GetState() override final;
    const wlanmac_info_t& GetWlanInfo() const override final;

   private:
    enum class DevicePacket : uint64_t {
        kShutdown,
        kPacketQueued,
        kIndication,
    };

    zx_status_t AddWlanDevice();
    zx_status_t AddEthDevice();

    fbl::unique_ptr<Packet> PreparePacket(const void* data, size_t length, Packet::Peer peer);
    template <typename T>
    fbl::unique_ptr<Packet> PreparePacket(const void* data, size_t length, Packet::Peer peer,
                                          const T& ctrl_data) {
        auto packet = PreparePacket(data, length, peer);
        if (packet != nullptr) { packet->CopyCtrlFrom(ctrl_data); }
        return packet;
    }

    zx_status_t QueuePacket(fbl::unique_ptr<Packet> packet) __TA_EXCLUDES(packet_queue_lock_);

    void MainLoop();
    void ProcessChannelPacketLocked(const zx_port_packet_t& pkt) __TA_REQUIRES(lock_);
    zx_status_t RegisterChannelWaitLocked() __TA_REQUIRES(lock_);
    zx_status_t QueueDevicePortPacket(DevicePacket id, uint32_t status = 0);

    zx_status_t GetChannel(zx::channel* out) __TA_EXCLUDES(lock_);

    void SetStatusLocked(uint32_t status);

    zx_device_t* parent_;
    zx_device_t* zxdev_;
    zx_device_t* ethdev_;

    WlanmacProxy wlanmac_proxy_;
    fbl::unique_ptr<EthmacIfcProxy> ethmac_proxy_;

    wlanmac_info_t wlanmac_info_ = {};
    fbl::RefPtr<DeviceState> state_;

    std::mutex lock_;
    std::thread work_thread_;
    zx::port port_;

    fbl::unique_ptr<Dispatcher> dispatcher_ __TA_GUARDED(lock_);

    bool dead_ __TA_GUARDED(lock_) = false;
    zx::channel channel_ __TA_GUARDED(lock_);

    std::mutex packet_queue_lock_;
    PacketQueue packet_queue_ __TA_GUARDED(packet_queue_lock_);
};

zx_status_t ValidateWlanMacInfo(const wlanmac_info& wlanmac_info);

}  // namespace wlan
