// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/wlan.h>
#include <ddktl/protocol/wlan-internal.h>
#include <zircon/assert.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>

// DDK wlan protocol support
//
// :: Proxies ::
//
// ddk::WlanmacIfcProxy and ddk::WlanmacProtocolProxy are simple wrappers around wlanmac_ifc_t and
// wlanmac_protocol_t, respectively. They do not own the pointers passed to them.
//
// :: Mixins ::
//
// ddk::WlanmacIfc and ddk::WlanmacProtocol are mixin classes that simplify writing DDK drivers that
// interact with the wlan protocol. They take care of implementing the function pointer tables and
// calling into the object that wraps them.
//
// :: Examples ::
//
// // A driver that communicates with a ZX_PROTOCOL_WLANMAC device as a wlanmac_ifc_t
// class WlanDevice;
// using WlanDeviceType = ddk::Device<WlanDevice, /* ddk mixins */>;
//
// class WlanDevice : public WlanDeviceType,
//                    public ddk::WlanmacIfc<WlanDevice> {
//   public:
//     WlanDevice(zx_device_t* parent)
//       : WlanDeviceType(driver, "my-wlan-device"),
//         parent_(parent) {}
//
//     zx_status_t Bind() {
//         wlanmac_protocol_t* ops;
//         auto status = get_device_protocol(parent_, ZX_PROTOCOL_WLANMAC,
//                                           reinterpret_cast<void**>(&ops));
//         if (status != ZX_OK) {
//             return status;
//         }
//        proxy_.reset(new ddk::WlanmacProtocolProxy(ops, parent_));
//        status = proxy_->Start(this);
//        if (status != ZX_OK) {
//            return status;
//        }
//        return device_add(ddk_device(), parent_);
//     }
//
//     void DdkRelease() {
//         // Clean up
//     }
//
//     void WlanmacStatus(uint32_t status) {
//         // Report status
//     }
//
//     void WlanmacRecv(uint32_t flags, const void* buf, size_t length,
//                      wlan_rx_info_t* info) {
//         // Receive data buffer from wlanmac device
//     }
//
//     void WlanmacCompleteTx(wlan_tx_packet_t* pkt, zx_status_t status) {
//         // Free packet buffers
//     }
//
//   private:
//     zx_device_t* parent_;
//     fbl::unique_ptr<ddk::WlanmacProtocolProxy> proxy_;
// };
//
//
// // A driver that implements a ZX_PROTOCOL_WLANMAC device
// class WlanmacDevice;
// using WlanmacDeviceType = ddk::Device<WlanmacDevice, /* ddk mixins */>;
//
// class WlanmacDevice : public WlanmacDeviceType,
//                       public ddk::WlanmacProtocol<WlanmacDevice> {
//   public:
//     WlanmacDevice(zx_device_t* parent)
//       : WlanmacDeviceType(driver, "my-wlanmac-device"),
//         parent_(parent) {}
//
//     zx_status_t Bind() {
//         return device_add(ddk_device(), parent_);
//     }
//
//     void DdkRelease() {
//         // Clean up
//     }
//
//     zx_status_t WlanmacQuery(uint32_t options, ethmac_info_t* info) {
//         // Fill out the ethmac info
//         return ZX_OK;
//     }
//
//     void WlanmacStop() {
//         // Device should stop
//     }
//
//     zx_status_t WlanmacStart(fbl::unique_ptr<ddk::WlanmacIfcProxy> proxy) {
//         // Start wlanmac operation
//         proxy_.swap(proxy);
//         return ZX_OK;
//     }
//
//     zx_status_t WlanmacQueueTx(uint32_t options, wlan_tx_packet_t* pkt) {
//         // Send the data
//     }
//
//     zx_status_t WlanmacSetChannel(uint32_t options, wlan_channel_t* chan) {
//         // Set the radio channel
//         return ZX_OK;
//     }
//
//     zx_status_t WlanmacSetBss(uint32_t options, const uint8_t mac[6], uint8_t type) {
//         // Set the bss
//         return ZX_OK;
//     }
//
//     zx_status_t WlnamacSetKey(uint32_t options, wlan_key_config_t* key_config) {
//         // Set the key and free the memory.
//         free(key_config);
//     }
//
//   private:
//     zx_device_t* parent_;
//     fbl::unique_ptr<ddk::WlanmacIfcProxy> proxy_;
// };

namespace ddk {

template <typename D>
class WlanmacIfc {
  public:
    WlanmacIfc() {
        internal::CheckWlanmacIfc<D>();
        ifc_.status = Status;
        ifc_.recv = Recv;
        ifc_.complete_tx = CompleteTx;
    }

    wlanmac_ifc_t* wlanmac_ifc() { return &ifc_; }

  private:
    static void Status(void* cookie, uint32_t status) {
        static_cast<D*>(cookie)->WlanmacStatus(status);
    }

    static void Recv(void* cookie, uint32_t flags, const void* data, size_t length,
                     wlan_rx_info_t* info) {
        static_cast<D*>(cookie)->WlanmacRecv(flags, data, length, info);
    }

    static void CompleteTx(void* cookie, wlan_tx_packet_t* pkt, zx_status_t status) {
        static_cast<D*>(cookie)->WlanmacCompleteTx(pkt, status);
    }

    wlanmac_ifc_t ifc_ = {};
};

class WlanmacIfcProxy {
  public:
    WlanmacIfcProxy(wlanmac_ifc_t* ifc, void* cookie)
      : ifc_(ifc), cookie_(cookie) {}

    void Status(uint32_t status) {
        ifc_->status(cookie_, status);
    }

    void Recv(uint32_t flags, const void* data, size_t length, wlan_rx_info_t* info) {
        ifc_->recv(cookie_, flags, data, length, info);
    }

    void CompleteTx(wlan_tx_packet_t* pkt, zx_status_t status) {
        ifc_->complete_tx(cookie_, pkt, status);
    }

  private:
    wlanmac_ifc_t* ifc_;
    void* cookie_;
};

template <typename D>
class WlanmacProtocol : public internal::base_protocol {
  public:
    WlanmacProtocol() {
        internal::CheckWlanmacProtocolSubclass<D>();
        ops_.query = Query;
        ops_.stop = Stop;
        ops_.start = Start;
        ops_.queue_tx = QueueTx;
        ops_.set_channel = SetChannel;
        ops_.set_bss = SetBss;
        ops_.set_key = SetKey;

        // Can only inherit from one base_protocol implemenation
        ZX_ASSERT(this->ddk_proto_ops_ == nullptr);
        ddk_proto_id_ = ZX_PROTOCOL_WLANMAC;
        ddk_proto_ops_ = &ops_;
    }

  private:
    static zx_status_t Query(void* ctx, uint32_t options, ethmac_info_t* info) {
        return static_cast<D*>(ctx)->WlanmacQuery(options, info);
    }

    static void Stop(void* ctx) {
        static_cast<D*>(ctx)->WlanmacStop();
    }

    static zx_status_t Start(void* ctx, wlanmac_ifc_t* ifc, void* cookie) {
        auto ifc_proxy = fbl::unique_ptr<WlanmacIfcProxy>(new WlanmacIfcProxy(ifc, cookie));
        return static_cast<D*>(ctx)->WlanmacStart(fbl::move(ifc_proxy));
    }

    static zx_status_t QueueTx(void* ctx, uint32_t options, wlan_tx_packet_t* pkt) {
        return static_cast<D*>(ctx)->WlanmacQueueTx(options, pkt);
    }

    static zx_status_t SetChannel(void* ctx, uint32_t options, wlan_channel_t* chan) {
        return static_cast<D*>(ctx)->WlanmacSetChannel(options, chan);
    }

    static zx_status_t SetBss(void* ctx, uint32_t options, const uint8_t mac[6], uint8_t type) {
        return static_cast<D*>(ctx)->WlanmacSetBss(options, mac, type);
    }

    static zx_status_t SetKey(void* ctx, uint32_t options, wlan_key_config_t* key_config) {
        return static_cast<D*>(ctx)->WlanmacSetKey(options, key_config);
    }

    wlanmac_protocol_ops_t ops_ = {};
};

class WlanmacProtocolProxy {
  public:
    WlanmacProtocolProxy(wlanmac_protocol_t* proto)
      : ops_(proto->ops), ctx_(proto->ctx) {}

    zx_status_t Query(uint32_t options, ethmac_info_t* info) {
        return ops_->query(ctx_, options, info);
    }

    template <typename D>
    zx_status_t Start(D* ifc) {
        static_assert(fbl::is_base_of<WlanmacIfc<D>, D>::value,
                      "Start must be called with a subclass of WlanmacIfc");
        return ops_->start(ctx_, ifc->wlanmac_ifc(), ifc);
    }

    void Stop() {
        ops_->stop(ctx_);
    }

    zx_status_t QueueTx(uint32_t options, wlan_tx_packet_t* pkt) {
        return ops_->queue_tx(ctx_, options, pkt);
    }

    zx_status_t SetChannel(uint32_t options, wlan_channel_t* chan) {
        return ops_->set_channel(ctx_, options, chan);
    }

    zx_status_t SetBss(uint32_t options, const uint8_t mac[6], uint8_t type) {
        return ops_->set_bss(ctx_, options, mac, type);
    }

    zx_status_t SetKey(uint32_t options, wlan_key_config_t* key_config) {
        return ops_->set_key(ctx_, options, key_config);
    }

  private:
    wlanmac_protocol_ops_t* ops_;
    void* ctx_;
};

}  // namespace ddk
