// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "max98373.h"

#include <algorithm>
#include <memory>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/i2c.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

// TODO(andresoportus): Implement this codec.

namespace {

// clang-format off
constexpr uint16_t kRegReset = 0x2000;
constexpr uint16_t kRegRevId = 0x21ff;

constexpr uint8_t kRegResetReset = 0x01;
// clang-format on

enum {
    COMPONENT_I2C,
    COMPONENT_RESET_GPIO,
    COMPONENT_COUNT,
};

} // namespace

namespace audio {

int Max98373::Thread() {
    auto status = HardwareReset();
    if (status != ZX_OK) {
        return thrd_error;
    }
    status = SoftwareResetAndInitialize();
    if (status != ZX_OK) {
        return thrd_error;
    }
    return thrd_success;
}

zx_status_t Max98373::HardwareReset() {
    fbl::AutoLock lock(&lock_);
    if (codec_reset_.is_valid()) {
        codec_reset_.Write(0);
        zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
        codec_reset_.Write(1);
        zx_nanosleep(zx_deadline_after(ZX_MSEC(3)));
        return ZX_OK;
    }
    zxlogf(ERROR, "%s Could not hardware reset the codec\n", __FILE__);
    return ZX_ERR_INTERNAL;
}

zx_status_t Max98373::SoftwareResetAndInitialize() {
    fbl::AutoLock lock(&lock_);
    auto status = WriteReg(kRegReset, kRegResetReset);
    if (status != ZX_OK) {
        return status;
    }

    uint8_t buffer;
    status = ReadReg(kRegRevId, &buffer);
    if (status == ZX_OK && buffer != 0x43) {
        zxlogf(ERROR, "%s Unexpected Rev Id 0x%02X\n", __FILE__, buffer);
        status = ZX_ERR_INTERNAL;
    }
    initialized_ = true;
    return status;
}

zx_status_t Max98373::Bind() {
    auto thunk =
        [](void* arg) -> int { return reinterpret_cast<Max98373*>(arg)->Thread(); };
    int rc = thrd_create_with_name(&thread_, thunk, this, "Max98373-thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }
    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_MAXIM},
        {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_MAXIM_MAX98373},
    };
    return DdkAdd("max98373", 0, props, countof(props));
}

void Max98373::Shutdown() {
    thrd_join(thread_, NULL);
}

zx_status_t Max98373::Create(zx_device_t* parent) {
    composite_protocol_t composite;

    auto status = device_get_protocol(parent, ZX_PROTOCOL_COMPOSITE, &composite);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Could not get composite protocol\n", __FILE__);
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_device_t* components[COMPONENT_COUNT] = {};
    size_t actual = 0;
    composite_get_components(&composite, components, countof(components), &actual);
    if (actual != COMPONENT_COUNT) {
        zxlogf(ERROR, "%s Could not get components\n", __FILE__);
        return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::AllocChecker ac;
    auto dev = std::unique_ptr<Max98373>(new (&ac) Max98373(parent, components[COMPONENT_I2C],
                                                            components[COMPONENT_RESET_GPIO]));
    status = dev->Bind();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the memory for dev.
    dev.release();
    return ZX_OK;
}

void Max98373::CodecReset(codec_reset_callback callback, void* cookie) {
    auto status = SoftwareResetAndInitialize();
    callback(cookie, status);
}

void Max98373::CodecGetInfo(codec_get_info_callback callback, void* cookie) {
    info_t info;
    info.unique_id = "";
    info.manufacturer = "Maxim";
    info.product_name = "MAX98373";
    callback(cookie, &info);
}

void Max98373::CodecIsBridgeable(codec_is_bridgeable_callback callback, void* cookie) {
    callback(cookie, false);
}

void Max98373::CodecSetBridgedMode(bool enable_bridged_mode,
                                   codec_set_bridged_mode_callback callback, void* cookie) {
    callback(cookie);
}

void Max98373::CodecGetDaiFormats(codec_get_dai_formats_callback callback, void* cookie) {
    callback(cookie, ZX_ERR_NOT_SUPPORTED, nullptr, 0);
}

void Max98373::CodecSetDaiFormat(const dai_format_t* format, codec_set_dai_format_callback callback,
                                 void* cookie) {
    callback(cookie, ZX_ERR_NOT_SUPPORTED);
}

void Max98373::CodecGetGainFormat(codec_get_gain_format_callback callback, void* cookie) {
    callback(cookie, nullptr);
}

void Max98373::CodecSetGainState(const gain_state_t* gain_state,
                                 codec_set_gain_state_callback callback, void* cookie) {
    callback(cookie);
}

void Max98373::CodecGetGainState(codec_get_gain_state_callback callback, void* cookie) {
    callback(cookie, nullptr);
}

void Max98373::CodecGetPlugState(codec_get_plug_state_callback callback, void* cookie) {
    callback(cookie, nullptr);
}

zx_status_t Max98373::WriteReg(uint16_t reg, uint8_t value) {
    uint8_t write_buffer[3];
    write_buffer[0] = static_cast<uint8_t>((reg >> 8) & 0xff);
    write_buffer[1] = static_cast<uint8_t>((reg >> 0) & 0xff);
    write_buffer[2] = value;
//#define TRACE_I2C
#ifdef TRACE_I2C
    printf("%s Writing register 0x%02X to value 0x%02X\n", __FILE__, reg, value);
    auto status = i2c_.WriteSync(write_buffer, countof(write_buffer));
    if (status != ZX_OK) {
        printf("%s Could not I2C write %d\n", __FILE__, status);
        return status;
    }
    uint8_t buffer = 0;
    i2c_.WriteReadSync(write_buffer, countof(write_buffer) - 1, &buffer, 1);
    if (status != ZX_OK) {
        printf("%s Could not I2C read %d\n", __FILE__, status);
        return status;
    }
    printf("%s Read register just written 0x%04X, value 0x%02X\n", __FILE__, reg, buffer);
    return ZX_OK;
#else
    return i2c_.WriteSync(write_buffer, countof(write_buffer));
#endif
}

zx_status_t Max98373::ReadReg(uint16_t reg, uint8_t* value) {
    // TODO(andresoportus): workaround for I2C flakyness.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    uint8_t write_buffer[2];
    write_buffer[0] = static_cast<uint8_t>((reg >> 8) & 0xff);
    write_buffer[1] = static_cast<uint8_t>((reg >> 0) & 0xff);
    auto status = i2c_.WriteReadSync(write_buffer, 2, value, 1);
    if (status != ZX_OK) {
        printf("%s Could not I2C read reg 0x%X status %d\n", __FILE__, reg, status);
        return status;
    }
//#define TRACE_I2C
#ifdef TRACE_I2C
    printf("%s Read register 0x%04X, value 0x%02X\n", __FILE__, reg, *value);
#endif
    return status;
}

zx_status_t max98373_bind(void* ctx, zx_device_t* parent) {
    return Max98373::Create(parent);
}

static zx_driver_ops_t driver_ops = []() {
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = max98373_bind;
    return ops;
}();

} // namespace audio

// clang-format off
ZIRCON_DRIVER_BEGIN(ti_max98373, audio::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MAXIM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MAXIM_MAX98373),
ZIRCON_DRIVER_END(ti_max98373)
// clang-format on
