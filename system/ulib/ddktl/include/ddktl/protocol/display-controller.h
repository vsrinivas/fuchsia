// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/display-controller.h>
#include <ddktl/device-internal.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>

namespace ddk {

template <typename D>
class DisplayControllerProtocol : public internal::base_protocol {
  public:
    DisplayControllerProtocol() {
        // TODO(stevensd): Add subclass check once API is stabilized
        ops_.set_display_controller_cb = SetDisplayControllerCb;
        ops_.get_display_info = GetDisplayInfo;
        ops_.import_vmo_image = ImportVmoImage;
        ops_.release_image = ReleaseImage;
        ops_.check_configuration = CheckConfiguration;
        ops_.apply_configuration = ApplyConfiguration;
        ops_.compute_linear_stride = ComputeLinearStride;
        ops_.allocate_vmo = AllocateVmo;

        // Can only inherit from one base_protocol implemenation
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL;
        ddk_proto_ops_ = &ops_;
    }

  private:
    static void SetDisplayControllerCb(void* ctx, void* cb_ctx, display_controller_cb_t* cb) {
        static_cast<D*>(ctx)->SetDisplayControllerCb(cb_ctx, cb);
    }

    static zx_status_t GetDisplayInfo(void* ctx, uint64_t display_id, display_info_t* info) {
        return static_cast<D*>(ctx)->GetDisplayInfo(display_id, info);
    }

    static zx_status_t ImportVmoImage(void* ctx, image_t* image, zx_handle_t vmo, size_t offset) {
        return static_cast<D*>(ctx)->ImportVmoImage(image, *zx::unowned_vmo(vmo), offset);
    }

    static void ReleaseImage(void* ctx, image_t* image) {
        static_cast<D*>(ctx)->ReleaseImage(image);
    }

    static void CheckConfiguration(void* ctx, const display_config_t** display_config,
                                   uint32_t* display_cfg_result, uint32_t** layer_cfg_result,
                                   uint32_t display_count) {
        static_cast<D*>(ctx)->CheckConfiguration(display_config, display_cfg_result,
                                                 layer_cfg_result, display_count);
    }

    static void ApplyConfiguration(void* ctx, const display_config_t** display_config,
                                   uint32_t display_count) {
        static_cast<D*>(ctx)->ApplyConfiguration(display_config, display_count);
    }

    static uint32_t ComputeLinearStride(void* ctx, uint32_t width, zx_pixel_format_t format) {
        return static_cast<D*>(ctx)->ComputeLinearStride(width, format);
    }

    static zx_status_t AllocateVmo(void* ctx, uint64_t size, zx_handle_t* vmo_out) {
        return static_cast<D*>(ctx)->AllocateVmo(size, vmo_out);
    }

    display_controller_protocol_ops_t ops_ = {};
};

}
