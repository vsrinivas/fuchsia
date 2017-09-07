// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/ethernet.h>
#include <ddktl/protocol/ethernet-internal.h>
#include <magenta/assert.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>

// DDK ethernet protocol support
//
// :: Proxies ::
//
// ddk::EthmacIfcProxy and ddk::EthmacProtocolProxy are simple wrappers around ethmac_ifc_t and
// ethmac_protocol_t, respectively. They do not own the pointers passed to them.
//
// :: Mixins ::
//
// ddk::EthmacIfc and ddk::EthmacProtocol are mixin classes that simplify writing DDK drivers that
// interact with the ethernet protocol. They take care of implementing the function pointer tables
// and calling into the object that wraps them.
//
// :: Examples ::
//
// // A driver that communicates with a MX_PROTOCOL_ETHERMAC device as a ethmac_ifc_t
// class EthDevice;
// using EthDeviceType = ddk::Device<EthDevice, /* ddk mixins */>;
//
// class EthDevice : public EthDeviceType,
//                   public ddk::EthmacIfc<EthDevice> {
//   public:
//     EthDevice(mx_device_t* parent)
//       : EthDeviceType("my-eth-device"),
//         parent_(parent) {}
//
//     mx_status_t Bind() {
//         ethmac_protocol_t* ops;
//         auto status = get_device_protocol(parent_, MX_PROTOCOL_ETHERMAC,
//                                           reinterpret_cast<void**>(&ops));
//         if (status != MX_OK) {
//             return status;
//         }
//        proxy_.reset(new ddk::EthmacProtocolProxy(ops, parent_));
//        status = proxy_->Start(this);
//        if (status != MX_OK) {
//            return status;
//        }
//        return device_add(ddk_device(), parent_);
//     }
//
//     void DdkRelease() {
//         // Clean up
//     }
//
//     void EthmacStatus(uint32_t status) {
//         // Report status
//     }
//
//     void EthmacRecv(void* buf, size_t length, uint32_t flags) {
//         // Receive data buffer from ethmac device
//     }
//
//   private:
//     mx_device_t* parent_;
//     fbl::unique_ptr<ddk::EthmacProtocolProxy> proxy_;
// };
//
//
// // A driver that implements a MX_PROTOCOL_ETHERMAC device
// class EthmacDevice;
// using EthmacDeviceType = ddk::Device<EthmacDevice, /* ddk mixins */>;
//
// class EthmacDevice : public EthmacDeviceType,
//                      public ddk::EthmacProtocol<EthmacDevice> {
//   public:
//     EthmacDevice(mx_device_t* parent)
//       : EthmacDeviceType("my-ethmac-device"),
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
//     mx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info) {
//         // Fill out the ethmac info
//         return MX_OK;
//     }
//
//     void EthmacStop() {
//         // Device should stop
//     }
//
//     mx_status_t EthmacStart(fbl::unique_ptr<ddk::EthmacIfcProxy> proxy) {
//         // Start ethmac operation
//         proxy_.swap(proxy);
//         return MX_OK;
//     }
//
//     void EthmacSend(uint32_t options, void* data, size_t length) {
//         // Send the data
//     }
//
//   private:
//     mx_device_t* parent_;
//     fbl::unique_ptr<ddk::EthmacIfcProxy> proxy_;
// };

namespace ddk {

template <typename D>
class EthmacIfc {
  public:
    EthmacIfc() {
        internal::CheckEthmacIfc<D>();
        ifc_.status = Status;
        ifc_.recv = Recv;
    }

    ethmac_ifc_t* ethmac_ifc() { return &ifc_; }

  private:
    static void Status(void* cookie, uint32_t status) {
        static_cast<D*>(cookie)->EthmacStatus(status);
    }

    static void Recv(void* cookie, void* data, size_t length, uint32_t flags) {
        static_cast<D*>(cookie)->EthmacRecv(data, length, flags);
    }

    ethmac_ifc_t ifc_ = {};
};

class EthmacIfcProxy {
  public:
    EthmacIfcProxy(ethmac_ifc_t* ifc, void* cookie)
      : ifc_(ifc), cookie_(cookie) {}

    void Status(uint32_t status) {
        ifc_->status(cookie_, status);
    }

    void Recv(void* data, size_t length, uint32_t flags) {
        ifc_->recv(cookie_, data, length, flags);
    }

  private:
    ethmac_ifc_t* ifc_;
    void* cookie_;
};

template <typename D>
class EthmacProtocol : public internal::base_protocol {
  public:
    EthmacProtocol() {
        internal::CheckEthmacProtocolSubclass<D>();
        ops_.query = Query;
        ops_.stop = Stop;
        ops_.start = Start;
        ops_.send = Send;

        // Can only inherit from one base_protocol implemenation
        MX_ASSERT(ddk_proto_ops_ == nullptr);
        ddk_proto_id_ = MX_PROTOCOL_ETHERMAC;
        ddk_proto_ops_ = &ops_;
    }

  private:
    static mx_status_t Query(void* ctx, uint32_t options, ethmac_info_t* info) {
        return static_cast<D*>(ctx)->EthmacQuery(options, info);
    }

    static void Stop(void* ctx) {
        static_cast<D*>(ctx)->EthmacStop();
    }

    static mx_status_t Start(void* ctx, ethmac_ifc_t* ifc, void* cookie) {
        auto ifc_proxy = fbl::unique_ptr<EthmacIfcProxy>(new EthmacIfcProxy(ifc, cookie));
        return static_cast<D*>(ctx)->EthmacStart(fbl::move(ifc_proxy));
    }

    static void Send(void* ctx, uint32_t options, void* data, size_t length) {
        static_cast<D*>(ctx)->EthmacSend(options, data, length);
    }

    ethmac_protocol_ops_t ops_ = {};
};

class EthmacProtocolProxy {
  public:
    EthmacProtocolProxy(ethmac_protocol_t* proto)
      : ops_(proto->ops), ctx_(proto->ctx) {}

    mx_status_t Query(uint32_t options, ethmac_info_t* info) {
        return ops_->query(ctx_, options, info);
    }

    template <typename D>
    mx_status_t Start(D* ifc) {
        static_assert(fbl::is_base_of<EthmacIfc<D>, D>::value,
                      "Start must be called with a subclass of EthmacIfc");
        return ops_->start(ctx_, ifc->ethmac_ifc(), ifc);
    }

    void Stop() {
        ops_->stop(ctx_);
    }

    void Send(uint32_t options, void* data, size_t length) {
        ops_->send(ctx_, options, data, length);
    }

  private:
    ethmac_protocol_ops_t* ops_;
    void* ctx_;
};

}  // namespace ddk
