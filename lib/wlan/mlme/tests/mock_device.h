// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/clock.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>

#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <gtest/gtest.h>
#include <unordered_set>

namespace wlan {
namespace {

// TODO(hahnr): Support for failing various device calls.
struct MockDevice : public DeviceInterface {
   public:
    MockDevice() { state = fbl::AdoptRef(new DeviceState); }

    // DeviceInterface implementation.

    zx_status_t GetTimer(uint64_t id, fbl::unique_ptr<Timer>* timer) override final {
        *timer = CreateTimer(id);
        return ZX_OK;
    }

    fbl::unique_ptr<Timer> CreateTimer(uint64_t id) {
        return fbl::make_unique<TestTimer>(id, &clock_);
    }

    zx_status_t SendEthernet(fbl::unique_ptr<Packet> packet) override final {
        eth_queue.Enqueue(fbl::move(packet));
        return ZX_OK;
    }

    zx_status_t SendWlan(fbl::unique_ptr<Packet> packet) override final {
        wlan_queue.Enqueue(fbl::move(packet));
        return ZX_OK;
    }

    zx_status_t SendService(fbl::unique_ptr<Packet> packet) override final {
        svc_queue.Enqueue(fbl::move(packet));
        return ZX_OK;
    }

    zx_status_t SetChannel(wlan_channel_t chan) override final {
        state->set_channel(chan);
        return ZX_OK;
    }

    zx_status_t SetStatus(uint32_t status) override final {
        state->set_online(status == 1);
        return ZX_OK;
    }

    zx_status_t ConfigureBss(wlan_bss_config_t* cfg) override final {
        if (!cfg) {
            bss_cfg.reset();
        } else {
            // Copy config which might get freed by the MLME before the result was verified.
            bss_cfg.reset(new wlan_bss_config_t);
            memcpy(bss_cfg.get(), cfg, sizeof(wlan_bss_config_t));
        }
        return ZX_OK;
    }

    zx_status_t ConfigureBeacon(fbl::unique_ptr<Packet> packet) override final {
        beacon = fbl::move(packet);
        return ZX_OK;
    }

    zx_status_t EnableBeaconing(bool enable) override final {
        beaconing_enabled = enable;
        return ZX_OK;
    }

    zx_status_t SetKey(wlan_key_config_t* cfg) override final {
        if (!cfg) {
            key_cfg.reset();
        } else {
            // Copy config which might get freed by the MLME before the result was verified.
            key_cfg.reset(new wlan_key_config_t);
            memcpy(key_cfg.get(), cfg, sizeof(wlan_key_config_t));
        }
        return ZX_OK;
    }

    zx_status_t StartHwScan(const wlan_hw_scan_config_t* scan_config) override {
        return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::RefPtr<DeviceState> GetState() override final { return state; }

    const wlanmac_info_t& GetWlanInfo() const override final { return wlanmac_info; }

    // Convenience methods.

    void AdvanceTime(zx::duration duration) { clock_.Set(zx::time() + duration); }

    void SetTime(zx::time time) { clock_.Set(time); }

    zx::time GetTime() { return clock_.Now(); }

    wlan_channel_t GetChannel() { return state->channel(); }

    uint16_t GetChannelNumber() { return state->channel().primary; }

    template <typename T> zx_status_t GetQueuedServiceMsg(uint32_t ordinal, T* out) {
        EXPECT_EQ(1u, svc_queue.size());
        auto packet = svc_queue.Dequeue();
        return DeserializeServiceMsg<T>(*packet, ordinal, out);
    }

    fbl::RefPtr<DeviceState> state;
    wlanmac_info_t wlanmac_info;
    PacketQueue eth_queue;
    PacketQueue wlan_queue;
    PacketQueue svc_queue;
    fbl::unique_ptr<wlan_bss_config_t> bss_cfg;
    fbl::unique_ptr<wlan_key_config_t> key_cfg;
    fbl::unique_ptr<Packet> beacon;
    bool beaconing_enabled;

   private:
    TestClock clock_;
};

}  // namespace
}  // namespace wlan
