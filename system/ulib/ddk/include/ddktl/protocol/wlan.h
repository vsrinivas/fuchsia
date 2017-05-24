// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/wlan.h>
#include <ddktl/protocol/wlan-internal.h>
#include <magenta/assert.h>
#include <mxtl/type_support.h>
#include <mxtl/unique_ptr.h>

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
// // A driver that communicates with a MX_PROTOCOL_WLANMAC device as a wlanmac_ifc_t
// class WlanDevice;
// using WlanDeviceType = ddk::Device<WlanDevice, /* ddk mixins */>;
//
// class WlanDevice : public WlanDeviceType,
//                    public ddk::WlanmacIfc<WlanDevice> {
//   public:
//     WlanDevice(mx_device_t* parent)
//       : WlanDeviceType(driver, "my-wlan-device"),
//         parent_(parent) {}
//
//     mx_status_t Bind() {
//         wlanmac_protocol_t* ops;
//         auto status = get_device_protocol(parent_, MX_PROTOCOL_WLANMAC,
//                                           reinterpret_cast<void**>(&ops));
//         if (status != NO_ERROR) {
//             return status;
//         }
//        proxy_.reset(new ddk::WlanmacProtocolProxy(ops, parent_));
//        status = proxy_->Start(this);
//        if (status != NO_ERROR) {
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
//     void WlanmacRecv(void* buf, size_t length, uint32_t flags) {
//         // Receive data buffer from wlanmac device
//     }
//
//   private:
//     mx_device_t* parent_;
//     mxtl::unique_ptr<ddk::WlanmacProtocolProxy> proxy_;
// };
//
//
// // A driver that implements a MX_PROTOCOL_WLANMAC device
// class WlanmacDevice;
// using WlanmacDeviceType = ddk::Device<WlanmacDevice, /* ddk mixins */>;
//
// class WlanmacDevice : public WlanmacDeviceType,
//                       public ddk::WlanmacProtocol<WlanmacDevice> {
//   public:
//     WlanmacDevice(mx_device_t* parent)
//       : WlanmacDeviceType(driver, "my-wlanmac-device"),
//         parent_(parent) {}
//
//     mx_status_t Bind() {
//         return device_add(ddk_device(), parent_);
//     }
//
//     void DdkRelease() {
//         // Clean up
//     }
//
//     mx_status_t WlanmacQuery(uint32_t options, ethmac_info_t* info) {
//         // Fill out the ethmac info
//         return NO_ERROR;
//     }
//
//     void WlanmacStop() {
//         // Device should stop
//     }
//
//     mx_status_t WlanmacStart(mxtl::unique_ptr<ddk::WlanmacIfcProxy> proxy) {
//         // Start wlanmac operation
//         proxy_.swap(proxy);
//         return NO_ERROR;
//     }
//
//     void WlanmacTx(uint32_t options, void* data, size_t length) {
//         // Send the data
//     }
//
//     mx_status_t WlanmacSetChannel(uint32_t options, wlan_channel_t* chan) {
//         // Set the radio channel
//         return NO_ERROR;
//     }
//
//   private:
//     mx_device_t* parent_;
//     mxtl::unique_ptr<ddk::WlanmacIfcProxy> proxy_;
// };

namespace ddk {

template <typename D>
class WlanmacIfc {
  public:
    WlanmacIfc() {
        internal::CheckWlanmacIfc<D>();
        ifc_.status = Status;
        ifc_.recv = Recv;
    }

    wlanmac_ifc_t* wlanmac_ifc() { return &ifc_; }

  private:
    static void Status(void* cookie, uint32_t status) {
        static_cast<D*>(cookie)->WlanmacStatus(status);
    }

    static void Recv(void* cookie, void* data, size_t length, uint32_t flags) {
        static_cast<D*>(cookie)->WlanmacRecv(data, length, flags);
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

    void Recv(void* data, size_t length, uint32_t flags) {
        ifc_->recv(cookie_, data, length, flags);
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
        ops_.tx = Tx;
        ops_.set_channel = SetChannel;

        // Can only inherit from one base_protocol implemenation
        MX_ASSERT(this->ddk_proto_ops_ == nullptr);
        ddk_proto_id_ = MX_PROTOCOL_WLANMAC;
        ddk_proto_ops_ = &ops_;
    }

  private:
    static mx_status_t Query(mx_device_t* dev, uint32_t options, ethmac_info_t* info) {
        return static_cast<D*>(dev->ctx)->WlanmacQuery(options, info);
    }

    static void Stop(mx_device_t* dev) {
        static_cast<D*>(dev->ctx)->WlanmacStop();
    }

    static mx_status_t Start(mx_device_t* dev, wlanmac_ifc_t* ifc, void* cookie) {
        auto ifc_proxy = mxtl::unique_ptr<WlanmacIfcProxy>(new WlanmacIfcProxy(ifc, cookie));
        return static_cast<D*>(dev->ctx)->WlanmacStart(mxtl::move(ifc_proxy));
    }

    static void Tx(mx_device_t* dev, uint32_t options, void* data, size_t length) {
        static_cast<D*>(dev->ctx)->WlanmacTx(options, data, length);
    }

    static mx_status_t SetChannel(mx_device_t* dev, uint32_t options, wlan_channel_t* chan) {
        return static_cast<D*>(dev->ctx)->WlanmacSetChannel(options, chan);
    }

    wlanmac_protocol_t ops_ = {};
};

class WlanmacProtocolProxy {
  public:
    WlanmacProtocolProxy(wlanmac_protocol_t* ops, mx_device_t* dev)
      : ops_(ops), dev_(dev) {}

    mx_status_t Query(uint32_t options, ethmac_info_t* info) {
        return ops_->query(dev_, options, info);
    }

    template <typename D>
    mx_status_t Start(D* ifc) {
        static_assert(mxtl::is_base_of<WlanmacIfc<D>, D>::value,
                      "Start must be called with a subclass of WlanmacIfc");
        return ops_->start(dev_, ifc->wlanmac_ifc(), ifc);
    }

    void Stop() {
        ops_->stop(dev_);
    }

    void Tx(uint32_t options, void* data, size_t length) {
        ops_->tx(dev_, options, data, length);
    }

    mx_status_t SetChannel(uint32_t options, wlan_channel_t* chan) {
        return ops_->set_channel(dev_, options, chan);
    }

    mx_device_t* device() { return dev_; }

  private:
    wlanmac_protocol_t* ops_;
    mx_device_t* dev_;
};

}  // namespace ddk
