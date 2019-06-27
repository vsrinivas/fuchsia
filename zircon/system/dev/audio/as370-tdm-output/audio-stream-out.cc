// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio-stream-out.h"

#include <optional>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddktl/metadata/audio.h>
#include <fbl/array.h>
#include <lib/mmio/mmio.h>
#include <soc/as370/as370-audio-regs.h>

// TODO(andresoportus): Implement this controller.

namespace {

enum {
    COMPONENT_PDEV,
    COMPONENT_CODEC,
    COMPONENT_CLOCK,
    COMPONENT_COUNT,
};

} // namespace

namespace audio {
namespace as370 {

As370AudioStreamOut::As370AudioStreamOut(zx_device_t* parent)
    : SimpleAudioStream(parent, false), pdev_(parent) {
}

zx_status_t As370AudioStreamOut::InitPdev() {
    composite_protocol_t composite;

    auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Could not get composite protocol\n", __FILE__);
        return status;
    }

    zx_device_t* components[COMPONENT_COUNT] = {};
    size_t actual;
    composite_get_components(&composite, components, countof(components), &actual);
    // Only PDEV and I2C components are required.
    if (actual < 2) {
        zxlogf(ERROR, "%s could not get components\n", __FILE__);
        return ZX_ERR_NOT_SUPPORTED;
    }

    pdev_ = components[COMPONENT_PDEV];
    if (!pdev_.is_valid()) {
        return ZX_ERR_NO_RESOURCES;
    }
    clks_[kAvpll0Clk] = components[COMPONENT_CLOCK];
    if (!clks_[kAvpll0Clk].is_valid()) {
        zxlogf(ERROR, "%s GetClk failed\n", __FILE__);
        return status;
    }
    clks_[kAvpll0Clk].Enable();

    std::optional<ddk::MmioBuffer> mmio_global, mmio_dhub, mmio_avio_global, mmio_i2s;
    status = pdev_.MapMmio(0, &mmio_global);
    if (status != ZX_OK) {
        return status;
    }
    status = pdev_.MapMmio(1, &mmio_dhub);
    if (status != ZX_OK) {
        return status;
    }
    status = pdev_.MapMmio(2, &mmio_avio_global);
    if (status != ZX_OK) {
        return status;
    }
    status = pdev_.MapMmio(3, &mmio_i2s);
    if (status != ZX_OK) {
        return status;
    }

    ddk::MmioBuffer i2s(*std::move(mmio_i2s));

    // Enable audio channel 0.
    AIO_PRI_TSD0_PRI_CTRL::Get().ReadFrom(&i2s).set_ENABLE(1).WriteTo(&i2s);

    // Enable primary port.
    AIO_PRI_PRIPORT::Get().ReadFrom(&i2s).set_ENABLE(1).WriteTo(&i2s);

    constexpr uint32_t divider = 9; // MCLK = APLL0 / 512.
    AIO_PRI_PRIAUD_CLKDIV::Get().ReadFrom(&i2s).set_SETTING(divider).WriteTo(&i2s);
    AIO_MCLKPRI_ACLK_CTRL::Get().ReadFrom(&i2s).set_clk_Enable(1).WriteTo(&i2s);

    codec_.proto_client_ = components[COMPONENT_CODEC];
    // TODO(andresoportus) configure controller and codec per codec protocol.
    status = codec_.GetInfo();
    if (status != ZX_OK) {
        return status;
    }
    return ZX_OK;
}

zx_status_t As370AudioStreamOut::Init() {
    auto status = InitPdev();
    if (status != ZX_OK) {
        return status;
    }

    // TODO(andresoportus): Get gain format and state.

    snprintf(device_name_, sizeof(device_name_), "as370-audio-out");
    snprintf(mfr_name_, sizeof(mfr_name_), "unknown");
    snprintf(prod_name_, sizeof(prod_name_), "as370");

    unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

    return ZX_OK;
}

zx_status_t As370AudioStreamOut::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370AudioStreamOut::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                                           uint32_t* out_num_rb_frames,
                                           zx::vmo* out_buffer) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370AudioStreamOut::Start(uint64_t* out_start_time) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370AudioStreamOut::Stop() {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370AudioStreamOut::SetGain(const audio_proto::SetGainReq& req) {
    return ZX_ERR_NOT_SUPPORTED;
}

void As370AudioStreamOut::ShutdownHook() {
}

zx_status_t As370AudioStreamOut::InitPost() {
    return ZX_OK;
}

} // namespace as370
} // namespace audio

static zx_status_t syn_audio_out_bind(void* ctx, zx_device_t* device) {
    auto stream =
        audio::SimpleAudioStream::Create<audio::as370::As370AudioStreamOut>(device);
    if (stream == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

static constexpr zx_driver_ops_t syn_audio_out_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = syn_audio_out_bind;
    return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(as370_audio_out, syn_audio_out_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_SYNAPTICS_AS370),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AS370_AUDIO_OUT),
ZIRCON_DRIVER_END(as370_audio_out)
// clang-format on
