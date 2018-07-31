// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/iommu.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "iommu-internal.h"

// DDK IOMMU protocol support.
//
// :: Proxies ::
//
// ddk::IommuProtocolProxy is a simple wrappers around iommu_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::IommuProtocol is a mixin class that simplifies writing DDK drivers that
// implement the platform bus protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_IOMMU device.
// class IommuDevice;
// using IommuDeviceType = ddk::Device<IommuDevice, /* ddk mixins */>;
//
// class IommuDevice : public IommuDeviceType,
//                     public ddk::IommuProtocol<IommuDevice> {
//   public:
//     IommuDevice(zx_device_t* parent)
//       : IommuDeviceType("my-iommu-device", parent) {}
//
//    zx_status_t GetBti(uint32_t iommu_index, uint32_t bti_id, zx_handle_t* out_handle);
//     ...
// };

namespace ddk {

template <typename D>
class IommuProtocol {
public:
    IommuProtocol() {
        internal::CheckIommuProtocolSubclass<D>();
        iommu_proto_ops_.get_bti = GetBti;
    }

protected:
    iommu_protocol_ops_t iommu_proto_ops_ = {};

private:
    static zx_status_t GetBti(void* ctx, uint32_t iommu_index, uint32_t bti_id,
                              zx_handle_t* out_handle) {
        return static_cast<D*>(ctx)->GetBti(iommu_index, bti_id, out_handle);
    }
};

class IommuProtocolProxy {
public:
    IommuProtocolProxy(iommu_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(iommu_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }

    zx_status_t GetBti(uint32_t iommu_index, uint32_t bti_id, zx_handle_t* out_handle) {
        return ops_->get_bti(ctx_, iommu_index, bti_id, out_handle);
    }

private:
    iommu_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
