// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wlantap-phy.h"
#include "wlantap-mac.h"

#include <array>
#include <ddk/debug.h>
#include <fuchsia/cpp/wlan_device.h>
#include <wlan/async/dispatcher.h>
#include <wlan/protocol/ioctl.h>
#include <wlan/protocol/phy.h>

namespace wlan {
namespace wlantap {
namespace {


template<typename T, size_t MAX_COUNT>
class DevicePool {
  public:
    template<class F>
    zx_status_t TryCreateNew(F factory, uint16_t* out_id) {
        for (size_t id = 0; id < MAX_COUNT; ++id) {
            if (pool_[id] == nullptr) {
                T* dev = nullptr;
                zx_status_t status = factory(id, &dev);
                if (status != ZX_OK) {
                    return status;
                }
                pool_[id] = dev;
                *out_id = id;
                return ZX_OK;
            }
        }
        return ZX_ERR_NO_RESOURCES;
    }

    T* Get(uint16_t id) {
        if (id >= MAX_COUNT) {
            return nullptr;
        }
        return pool_[id];
    }

    T* Release(uint16_t id) {
        if (id >= MAX_COUNT) {
            return nullptr;
        }
        T* ret = pool_[id];
        pool_[id] = nullptr;
        return ret;
    }

    void ReleaseAll() {
        std::fill(pool_.begin(), pool_.end(), nullptr);
    }

  private:
    std::array<T*, MAX_COUNT> pool_ {};
};

constexpr size_t kMaxMacDevices = 4;

struct WlantapPhy : wlan_device::Phy, ::wlantap::WlantapPhy, WlantapMac::Listener {
    WlantapPhy(zx_device_t* device, zx::channel user_channel,
               std::unique_ptr<::wlantap::WlantapPhyConfig> phy_config,
               async_t* loop)
      : phy_config_(std::move(phy_config)),
        phy_dispatcher_(loop),
        user_channel_binding_(this, std::move(user_channel), loop)
    {
        user_channel_binding_.set_error_handler([this] {
            // Remove the device if the client closed the channel
            user_channel_binding_.set_error_handler(nullptr);
            Unbind();
        });
    }

    static void DdkUnbind(void* ctx) {
        auto self = static_cast<WlantapPhy*>(ctx);
        self->Unbind();
    }

    static void DdkRelease(void* ctx) {
        delete static_cast<WlantapPhy*>(ctx);
    }

    void Unbind() {
        // This is somewhat hacky. We rely on the fact that the dispatcher's
        // and user_channel_binding events run on the same thread.
        // So when the dispatcher's shutdown callback is executed, we know
        // that there can't be any more calls via user_channel_binding either.
        user_channel_binding_.Unbind();
        phy_dispatcher_.InitiateShutdown([this] {
            {
                std::lock_guard<std::mutex> guard(wlanmac_lock_);
                wlanmac_devices_.ReleaseAll();
            }
            device_remove(device_);
        });
    }

    static zx_status_t DdkIoctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                void* out_buf, size_t out_len, size_t* out_actual) {
        auto& self = *static_cast<WlantapPhy*>(ctx);
        switch (op) {
            case IOCTL_WLANPHY_CONNECT:
                zxlogf(INFO, "wlantap phy ioctl: connect\n");
                return self.IoctlConnect(in_buf, in_len);
            default:
                zxlogf(ERROR, "wlantap phy ioctl: unknown (%u)\n", op);
                return ZX_ERR_NOT_SUPPORTED;
        }
    }

    zx_status_t IoctlConnect(const void* in_buf, size_t in_len) {
        if (in_buf == nullptr || in_len < sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        phy_dispatcher_.AddBinding(zx::channel(*static_cast<const zx_handle_t*>(in_buf)), this);
        return ZX_OK;
    }

    // wlan_device::Phy impl

    virtual void Query(QueryCallback callback) override {
        wlan_device::QueryResponse response;
        response.status = phy_config_->phy_info.Clone(&response.info);
        callback(std::move(response));
    }

    template<typename V, typename T>
    static bool contains(const V& v, const T& t) {
        return std::find(v.cbegin(), v.cend(), t) != v.cend();
    }

    virtual void CreateIface(wlan_device::CreateIfaceRequest req,
                             CreateIfaceCallback callback) override {
        wlan_device::CreateIfaceResponse response;
        if (!contains(*phy_config_->phy_info.mac_roles, req.role)) {
            response.status = ZX_ERR_NOT_SUPPORTED;
            callback(std::move(response));
            return;
        }
        std::lock_guard<std::mutex> guard(wlanmac_lock_);
        zx_status_t status = wlanmac_devices_.TryCreateNew(
            [&] (uint16_t id, WlantapMac** out_dev) {
                return CreateWlantapMac(device_, req, phy_config_.get(), id, this, out_dev);
            }, &response.info.id);
        if (status != ZX_OK) {
            response.status = ZX_ERR_NO_RESOURCES;
            callback(std::move(response));
            return;
        }
        callback(std::move(response));
    }

    virtual void DestroyIface(wlan_device::DestroyIfaceRequest req,
                              DestroyIfaceCallback callback) override {
        wlan_device::DestroyIfaceResponse response;
        std::lock_guard<std::mutex> guard(wlanmac_lock_);
        WlantapMac* wlanmac = wlanmac_devices_.Release(req.id);
        if (wlanmac == nullptr) {
            response.status = ZX_ERR_INVALID_ARGS;
        } else {
            wlanmac->RemoveDevice();
            response.status = ZX_OK;
        }
        callback(std::move(response));
    }

    // wlantap::WlantapPhy impl

    virtual void Rx(uint16_t wlanmac_id, ::fidl::VectorPtr<uint8_t> data,
                    ::wlantap::WlanRxInfo info) override {
        std::lock_guard<std::mutex> guard(wlanmac_lock_);
        if (WlantapMac* wlanmac = wlanmac_devices_.Get(wlanmac_id)) {
            wlanmac->Rx(data, info);
        }
    }

    virtual void Status(uint16_t wlanmac_id, uint32_t st) override {
        std::lock_guard<std::mutex> guard(wlanmac_lock_);
        if (WlantapMac* wlanmac = wlanmac_devices_.Get(wlanmac_id)) {
            wlanmac->Status(st);
        }
    }

    // WlantapMac::Listener impl

    virtual void WlantapMacStart(uint16_t wlanmac_id) override {
        //TODO: send to the client
        printf("WlantapMacStart id=%u\n", wlanmac_id);
    }

    virtual void WlantapMacStop(uint16_t wlanmac_id) override {
        //TODO: send to the client
        printf("WlantapMacStop id=%u\n", wlanmac_id);
    }

    virtual void WlantapMacQueueTx(uint16_t wlanmac_id, wlan_tx_packet_t* pkt) override {
        //TODO: send to the client
        printf("WlantapMacQueueTx id=%u\n", wlanmac_id);
    }

    virtual void WlantapMacSetChannel(uint16_t wlanmac_id, wlan_channel_t* channel) override {
        //TODO: send to the client
        printf("WlantapMacSetChannel id=%u\n", wlanmac_id);
    }

    virtual void WlantapMacConfigureBss(uint16_t wlanmac_id, wlan_bss_config_t* config) override {
        //TODO: send to the client
        printf("WlantapMacConfigureBss id=%u\n", wlanmac_id);
    }

    virtual void WlantapMacSetKey(uint16_t wlanmac_id, wlan_key_config_t* key_config) override {
        //TODO: send to the client
        printf("WlantapMacSetKey id=%u\n", wlanmac_id);
    }

    zx_device_t* device_;
    const std::unique_ptr<const ::wlantap::WlantapPhyConfig> phy_config_;
    wlan::async::Dispatcher<wlan_device::Phy> phy_dispatcher_;
    fidl::Binding<::wlantap::WlantapPhy> user_channel_binding_;
    std::mutex wlanmac_lock_;
    DevicePool<WlantapMac, kMaxMacDevices> wlanmac_devices_ __TA_GUARDED(wlanmac_lock_);
};

} // namespace

zx_status_t CreatePhy(zx_device_t* wlantapctl, zx::channel user_channel,
                      std::unique_ptr<::wlantap::WlantapPhyConfig> config, async_t* loop) {
    auto phy = std::make_unique<WlantapPhy>(wlantapctl, std::move(user_channel),
                                            std::move(config), loop);
    static zx_protocol_device_t device_ops = {
        .version = DEVICE_OPS_VERSION,
        .unbind = &WlantapPhy::DdkUnbind,
        .release = &WlantapPhy::DdkRelease,
        .ioctl = &WlantapPhy::DdkIoctl
    };
    static wlanphy_protocol_ops_t proto_ops = {};
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = phy->phy_config_->name->c_str(),
        .ctx = phy.get(),
        .ops = &device_ops,
        .proto_id = ZX_PROTOCOL_WLANPHY,
        .proto_ops = &proto_ops
    };
    zx_status_t status = device_add(wlantapctl, &args, &phy->device_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not add device: %d\n", __func__, status);
        return status;
    }
    // Transfer ownership to devmgr
    phy.release();
    return ZX_OK;
}

} // namespace wlantap
} // namespace wlan
