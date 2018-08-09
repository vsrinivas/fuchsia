// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wlantap-phy.h"

#include <ddk/debug.h>
#include <fuchsia/wlan/device/c/fidl.h>
#include <fuchsia/wlan/device/cpp/fidl.h>
#include <fuchsia/wlan/tap/c/fidl.h>
#include <wlan/async/dispatcher.h>
#include <wlan/protocol/mac.h>
#include <wlan/protocol/phy-impl.h>
#include <zircon/status.h>
#include <array>

#include "utils.h"
#include "wlantap-mac.h"

namespace wlan {

namespace wlantap = ::fuchsia::wlan::tap;

namespace {

template <typename T>
zx_status_t EncodeFidlMessage(uint32_t ordinal, T* message, fidl::Encoder* encoder) {
    encoder->Reset(ordinal);
    encoder->Alloc(fidl::CodingTraits<T>::encoded_size);
    message->Encode(encoder, sizeof(fidl_message_header_t));
    auto encoded = encoder->GetMessage();
    const char* err = nullptr;
    zx_status_t status =
        fidl_validate(T::FidlType, encoded.bytes().data() + sizeof(fidl_message_header_t),
                      encoded.bytes().actual() - sizeof(fidl_message_header_t), 0, &err);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: FIDL validation failed: %s (%d)\n", __func__, err, status);
        return status;
    }
    return ZX_OK;
}

template <typename T>
zx_status_t SendFidlMessage(uint32_t ordinal, T* message, fidl::Encoder* encoder,
                            const zx::channel& channel) {
    zx_status_t status = EncodeFidlMessage(ordinal, message, encoder);
    if (status != ZX_OK) { return status; }
    auto m = encoder->GetMessage();
    status = channel.write(0, m.bytes().data(), m.bytes().actual(), nullptr, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: write to channel failed: %d\n", __func__, status);
        return status;
    }
    return ZX_OK;
}

template <typename T, size_t N>::fidl::Array<T, N> ToFidlArray(const T (&c_array)[N]) {
    ::fidl::Array<T, N> ret;
    std::copy_n(&c_array[0], N, ret.begin());
    return ret;
};

struct EventSender {
    EventSender(const zx::channel& channel) : encoder_(0), channel_(channel) {}

    void SendTxEvent(uint16_t wlanmac_id, wlan_tx_packet_t* pkt) {
        tx_args_.wlanmac_id = wlanmac_id;
        ConvertTxInfo(pkt->info, &tx_args_.packet.info);
        auto& data = tx_args_.packet.data;
        data->clear();
        auto head = static_cast<const uint8_t*>(pkt->packet_head->data);
        std::copy_n(head, pkt->packet_head->len, std::back_inserter(*data));
        if (pkt->packet_tail != nullptr) {
            auto tail = static_cast<const uint8_t*>(pkt->packet_tail->data);
            std::copy_n(tail + pkt->tail_offset, pkt->packet_tail->len - pkt->tail_offset,
                        std::back_inserter(*data));
        }
        Send(EventOrdinal::Tx, &tx_args_);
    }

    void SendSetChannelEvent(uint16_t wlanmac_id, wlan_channel_t* channel) {
        wlantap::SetChannelArgs args = {.wlanmac_id = wlanmac_id,
                                        .chan = {.primary = channel->primary,
                                                 .cbw = channel->cbw,
                                                 .secondary80 = channel->secondary80}};
        Send(EventOrdinal::SetChannel, &args);
    }

    void SendConfigureBssEvent(uint16_t wlanmac_id, wlan_bss_config_t* config) {
        wlantap::ConfigureBssArgs args = {.wlanmac_id = wlanmac_id,
                                          .config = {.bss_type = config->bss_type,
                                                     .bssid = ToFidlArray(config->bssid),
                                                     .remote = config->remote}};
        Send(EventOrdinal::ConfigureBss, &args);
    }

    void SendSetKeyEvent(uint16_t wlanmac_id, wlan_key_config_t* config) {
        set_key_args_.wlanmac_id = wlanmac_id;
        set_key_args_.config.protection = config->protection;
        set_key_args_.config.cipher_oui = ToFidlArray(config->cipher_oui);
        set_key_args_.config.cipher_type = config->cipher_type;
        set_key_args_.config.key_type = config->key_type;
        set_key_args_.config.peer_addr = ToFidlArray(config->peer_addr);
        set_key_args_.config.key_idx = config->key_idx;
        auto& key = set_key_args_.config.key;
        key->clear();
        key->reserve(config->key_len);
        std::copy_n(config->key, config->key_len, std::back_inserter(*key));
        Send(EventOrdinal::SetKey, &set_key_args_);
    }

    void SendWlanmacStartEvent(uint16_t wlanmac_id) {
        wlantap::WlanmacStartArgs args = {.wlanmac_id = wlanmac_id};
        Send(EventOrdinal::WlanmacStart, &args);
    }

   private:
    enum class EventOrdinal : uint32_t {
        Tx = fuchsia_wlan_tap_WlantapPhyTxOrdinal,
        SetChannel = fuchsia_wlan_tap_WlantapPhySetChannelOrdinal,
        ConfigureBss = fuchsia_wlan_tap_WlantapPhyConfigureBssOrdinal,
        // TODO: ConfigureBeacon
        SetKey = fuchsia_wlan_tap_WlantapPhySetKeyOrdinal,
        WlanmacStart = fuchsia_wlan_tap_WlantapPhyWlanmacStartOrdinal
    };

    template <typename T> void Send(EventOrdinal ordinal, T* message) {
        zx_status_t status =
            SendFidlMessage(static_cast<uint32_t>(ordinal), message, &encoder_, channel_);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: failed to send FIDL message: %d\n", __func__, status);
        }
    }

    static void ConvertTxInfo(const wlan_tx_info_t& in, wlantap::WlanTxInfo* out) {
        out->tx_flags = in.tx_flags;
        out->valid_fields = in.valid_fields;
        out->phy = in.phy;
        out->cbw = in.cbw;
        out->data_rate = in.data_rate;
        out->mcs = in.mcs;
    }

    fidl::Encoder encoder_;
    const zx::channel& channel_;
    // Messages that require memory allocation
    wlantap::TxArgs tx_args_;
    wlantap::SetKeyArgs set_key_args_;
};

template <typename T, size_t MAX_COUNT> class DevicePool {
   public:
    template <class F> zx_status_t TryCreateNew(F factory, uint16_t* out_id) {
        for (size_t id = 0; id < MAX_COUNT; ++id) {
            if (pool_[id] == nullptr) {
                T* dev = nullptr;
                zx_status_t status = factory(id, &dev);
                if (status != ZX_OK) { return status; }
                pool_[id] = dev;
                *out_id = id;
                return ZX_OK;
            }
        }
        return ZX_ERR_NO_RESOURCES;
    }

    T* Get(uint16_t id) {
        if (id >= MAX_COUNT) { return nullptr; }
        return pool_[id];
    }

    T* Release(uint16_t id) {
        if (id >= MAX_COUNT) { return nullptr; }
        T* ret = pool_[id];
        pool_[id] = nullptr;
        return ret;
    }

    void ReleaseAll() { std::fill(pool_.begin(), pool_.end(), nullptr); }

   private:
    std::array<T*, MAX_COUNT> pool_{};
};

constexpr size_t kMaxMacDevices = 4;

struct WlantapPhy : wlantap::WlantapPhy, WlantapMac::Listener {
    WlantapPhy(zx_device_t* device, zx::channel user_channel,
               std::unique_ptr<wlantap::WlantapPhyConfig> phy_config, async_dispatcher_t* loop)
        : phy_config_(std::move(phy_config)),
          loop_(loop),
          user_channel_binding_(this, std::move(user_channel), loop),
          event_sender_(user_channel_binding_.channel()) {
        user_channel_binding_.set_error_handler([this] {
            zxlogf(INFO, "wlantap phy: unbinding device because the channel was closed\n");
            // Remove the device if the client closed the channel
            user_channel_binding_.set_error_handler(nullptr);
            Unbind();
            zxlogf(INFO, "wlantap phy: done unbinding\n");
        });
    }

    static void DdkUnbind(void* ctx) {
        zxlogf(INFO, "wlantap phy: unbinding device per request from DDK\n");
        auto self = static_cast<WlantapPhy*>(ctx);
        self->Unbind();
        zxlogf(INFO, "wlantap phy: done unbinding\n");
    }

    static void DdkRelease(void* ctx) {
        zxlogf(INFO, "wlantap phy: DdkRelease\n");
        delete static_cast<WlantapPhy*>(ctx);
        zxlogf(INFO, "wlantap phy: DdkRelease done\n");
    }

    void Unbind() {
        user_channel_binding_.Unbind();
        // Flush any remaining tasks in the event loop before destroying the interfaces
        ::async::PostTask(loop_,[this] {
            {
                std::lock_guard<std::mutex> guard(wlanmac_lock_);
                wlanmac_devices_.ReleaseAll();
            }
            device_remove(device_);
        });
    }

    // wlanphy-impl DDK interface

    zx_status_t Query(wlanphy_info_t* info) {
        zxlogf(INFO, "wlantap phy: received a 'Query' DDK request\n");
        zx_status_t status = ConvertPhyInfo(&info->wlan_info, phy_config_->phy_info);
        zxlogf(INFO, "wlantap phy: responded to 'Query' with status %s\n",
               zx_status_get_string(status));
        return status;
    }

    template <typename V, typename T> static bool contains(const V& v, const T& t) {
        return std::find(v.cbegin(), v.cend(), t) != v.cend();
    }

    zx_status_t CreateIface(uint16_t role, uint16_t* id) {
        zxlogf(INFO, "wlantap phy: received a 'CreateIface' DDK request\n");
        wlan_device::MacRole dev_role = ConvertMacRole(role);
        if (!contains(*phy_config_->phy_info.mac_roles, dev_role)) {
            zxlogf(ERROR, "wlantap phy: CreateIface: role not supported\n");
            return ZX_ERR_NOT_SUPPORTED;
        }
        std::lock_guard<std::mutex> guard(wlanmac_lock_);
        zx_status_t status = wlanmac_devices_.TryCreateNew(
            [&] (uint16_t id, WlantapMac** out_dev) {
                return CreateWlantapMac(device_, dev_role, phy_config_.get(), id, this, out_dev);
            }, id);
        if (status != ZX_OK) {
            zxlogf(ERROR,
                   "wlantap phy: CreateIface: maximum number of interfaces already reached\n");
            return ZX_ERR_NO_RESOURCES;
        }
        zxlogf(INFO, "wlantap phy: CreateIface: success\n");
        return ZX_OK;
    }

    zx_status_t DestroyIface(uint16_t id) {
        zxlogf(INFO, "wlantap phy: received a 'DestroyIface' DDK request\n");
        std::lock_guard<std::mutex> guard(wlanmac_lock_);
        WlantapMac* wlanmac = wlanmac_devices_.Release(id);
        if (wlanmac == nullptr) {
            zxlogf(ERROR, "wlantap phy: DestroyIface: invalid iface id\n");
            return ZX_ERR_INVALID_ARGS;
        }
        wlanmac->RemoveDevice();
        zxlogf(ERROR, "wlantap phy: DestroyIface: done\n");
        return ZX_OK;
    }

    // wlantap::WlantapPhy impl

    virtual void Rx(uint16_t wlanmac_id, ::fidl::VectorPtr<uint8_t> data,
                    wlantap::WlanRxInfo info) override {
        zxlogf(INFO, "wlantap phy: Rx(%zu bytes)\n", data->size());
        std::lock_guard<std::mutex> guard(wlanmac_lock_);
        if (WlantapMac* wlanmac = wlanmac_devices_.Get(wlanmac_id)) { wlanmac->Rx(data, info); }
        zxlogf(INFO, "wlantap phy: Rx done\n");
    }

    virtual void Status(uint16_t wlanmac_id, uint32_t st) override {
        zxlogf(INFO, "wlantap phy: Status(%u)\n", st);
        std::lock_guard<std::mutex> guard(wlanmac_lock_);
        if (WlantapMac* wlanmac = wlanmac_devices_.Get(wlanmac_id)) { wlanmac->Status(st); }
        zxlogf(INFO, "wlantap phy: Status done\n");
    }

    // WlantapMac::Listener impl

    virtual void WlantapMacStart(uint16_t wlanmac_id) override {
        zxlogf(INFO, "wlantap phy: WlantapMacStart id=%u\n", wlanmac_id);
        std::lock_guard<std::mutex> guard(event_sender_lock_);
        event_sender_.SendWlanmacStartEvent(wlanmac_id);
        zxlogf(INFO, "wlantap phy: WlantapMacStart done\n");
    }

    virtual void WlantapMacStop(uint16_t wlanmac_id) override {
        zxlogf(INFO, "wlantap phy: WlantapMacStop\n");
    }

    virtual void WlantapMacQueueTx(uint16_t wlanmac_id, wlan_tx_packet_t* pkt) override {
        zxlogf(INFO, "wlantap phy: WlantapMacQueueTx id=%u\n", wlanmac_id);
        std::lock_guard<std::mutex> guard(event_sender_lock_);
        event_sender_.SendTxEvent(wlanmac_id, pkt);
        zxlogf(INFO, "wlantap phy: WlantapMacQueueTx done\n");
    }

    virtual void WlantapMacSetChannel(uint16_t wlanmac_id, wlan_channel_t* channel) override {
        zxlogf(INFO, "wlantap phy: WlantapMacSetChannel id=%u\n", wlanmac_id);
        std::lock_guard<std::mutex> guard(event_sender_lock_);
        event_sender_.SendSetChannelEvent(wlanmac_id, channel);
        zxlogf(INFO, "wlantap phy: WlantapMacSetChannel done\n");
    }

    virtual void WlantapMacConfigureBss(uint16_t wlanmac_id, wlan_bss_config_t* config) override {
        zxlogf(INFO, "wlantap phy: WlantapMacConfigureBss id=%u\n", wlanmac_id);
        std::lock_guard<std::mutex> guard(event_sender_lock_);
        event_sender_.SendConfigureBssEvent(wlanmac_id, config);
        zxlogf(INFO, "wlantap phy: WlantapMacConfigureBss done\n");
    }

    virtual void WlantapMacSetKey(uint16_t wlanmac_id, wlan_key_config_t* key_config) override {
        zxlogf(INFO, "wlantap phy: WlantapMacSetKey id=%u\n", wlanmac_id);
        std::lock_guard<std::mutex> guard(event_sender_lock_);
        event_sender_.SendSetKeyEvent(wlanmac_id, key_config);
        zxlogf(INFO, "wlantap phy: WlantapMacSetKey done\n");
    }

    zx_device_t* device_;
    const std::unique_ptr<const wlantap::WlantapPhyConfig> phy_config_;
    async_dispatcher_t* loop_;
    fidl::Binding<wlantap::WlantapPhy> user_channel_binding_;
    std::mutex wlanmac_lock_;
    DevicePool<WlantapMac, kMaxMacDevices> wlanmac_devices_ __TA_GUARDED(wlanmac_lock_);
    std::mutex event_sender_lock_;
    EventSender event_sender_ __TA_GUARDED(event_sender_lock_);
};

}  // namespace

#define DEV(c) static_cast<WlantapPhy*>(c)
static wlanphy_impl_protocol_ops_t wlanphy_impl_ops = {
    .query = [](void* ctx, wlanphy_info_t* info) -> zx_status_t {
        return DEV(ctx)->Query(info);
    },
    .create_iface = [](void* ctx, uint16_t mac_role, uint16_t* id) -> zx_status_t {
        return DEV(ctx)->CreateIface(mac_role, id);
    },
    .destroy_iface = [](void* ctx, uint16_t id) -> zx_status_t {
        return DEV(ctx)->DestroyIface(id);
    }
};
#undef DEV

zx_status_t CreatePhy(zx_device_t* wlantapctl, zx::channel user_channel,
                      std::unique_ptr<wlantap::WlantapPhyConfig> config, async_dispatcher_t* loop) {
    zxlogf(INFO, "wlantap: creating phy\n");
    auto phy =
        std::make_unique<WlantapPhy>(wlantapctl, std::move(user_channel), std::move(config), loop);
    static zx_protocol_device_t device_ops = {.version = DEVICE_OPS_VERSION,
                                              .unbind = &WlantapPhy::DdkUnbind,
                                              .release = &WlantapPhy::DdkRelease};
    device_add_args_t args = {.version = DEVICE_ADD_ARGS_VERSION,
                              .name = phy->phy_config_->name->c_str(),
                              .ctx = phy.get(),
                              .ops = &device_ops,
                              .proto_id = ZX_PROTOCOL_WLANPHY_IMPL,
                              .proto_ops = &wlanphy_impl_ops};
    zx_status_t status = device_add(wlantapctl, &args, &phy->device_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "wlantap: %s: could not add device: %d\n", __func__, status);
        return status;
    }
    // Transfer ownership to devmgr
    phy.release();
    zxlogf(INFO, "wlantap: phy successfully created\n");
    return ZX_OK;
}

}  // namespace wlan
