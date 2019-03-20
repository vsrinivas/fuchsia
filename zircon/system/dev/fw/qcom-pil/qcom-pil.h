// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/platform/device.h>
#include <zircon/types.h>

namespace qcom_pil {

enum class TzService : uint8_t {
    Boot = 1,
    Pil,
};

enum class PasId : uint64_t {
    Modem,
    Q6,
    Dsps,
    Tzapps,
    ModemSw,
    ModemFw,
    Wcnss,
    Secapp,
    Gss,
    Vidc,
    Vpu,
    Bcss,
};

enum class ArgType : uint64_t {
    Val,
    Ro,
    Rw,
    Bufval,
};

enum class Cmd : uint8_t {
    InitImage = 1,
    MemSetup,
    AuthAndReset = 5,
    Shutdown,
    QuerySupport,
};

// Not class for integer usage below.
enum CallType : uint8_t {
    kYieldingCall = 0,
    kFastCall = 1,
};

enum CallConvention : uint8_t {
    kSmc32CallConv = 0,
    kSmc64CallConv = 1,
};

enum Service : uint8_t {
    kArchService = 0x00,
    kCpuService = 0x01,
    kSipService = 0x02,
    kOemService = 0x03,
    kStandardService = 0x04,
    kTrustedOsService = 0x32,
    kTrustedOsServiceEnd = 0x3F,
};

constexpr uint8_t kCallTypeMask = 0x01;
constexpr uint8_t kCallTypeShift = 31;
constexpr uint8_t kCallConvMask = 0x01;
constexpr uint8_t kCallConvShift = 30;
constexpr uint8_t kServiceMask = 0x3F;
constexpr uint8_t kServiceShift = 24;
constexpr uint8_t kTzServiceMask = 0xFF;
constexpr uint8_t kTzServiceShift = 8;
constexpr uint8_t kCallMask = 0xFF;
constexpr uint8_t kCallShift = 0;

static constexpr uint32_t CreateFunctionId(CallType call_type,
                                           CallConvention call_conv,
                                           Service service,
                                           uint8_t tz_service,
                                           uint8_t call) {
    return (((call_type & kCallTypeMask) << kCallTypeShift) |
            ((call_conv & kCallConvMask) << kCallConvShift) |
            ((service & kServiceMask) << kServiceShift) |
            ((tz_service & kTzServiceMask) << kTzServiceShift) |
            ((call & kCallMask) << kCallShift));
}

static constexpr uint32_t CreatePilFunctionId(Cmd cmd) {
    return CreateFunctionId(kYieldingCall, kSmc32CallConv, kSipService,
                            static_cast<uint8_t>(TzService::Pil),
                            static_cast<uint8_t>(cmd));
}

static constexpr uint64_t CreateScmArgs(uint32_t n_args, uint32_t arg0 = 0, uint32_t arg1 = 0,
                                        uint32_t arg2 = 0, uint32_t arg3 = 0, uint32_t arg4 = 0,
                                        uint32_t arg5 = 0, uint32_t arg6 = 0, uint32_t arg7 = 0,
                                        uint32_t arg8 = 0, uint32_t arg9 = 0) {
    return n_args | (arg0 << 4) | (arg1 << 6) | (arg2 << 8) | (arg3 << 10) | (arg4 << 12) |
           (arg5 << 14) | (arg6 << 16) | (arg7 << 18) | (arg8 << 20) | (arg9 << 22);
}

static constexpr zx_smc_parameters_t CreatePilSmcParams(Cmd cmd, uint64_t args, PasId pas_id,
                                                        uint64_t arg3 = 0, uint64_t arg4 = 0,
                                                        uint64_t arg5 = 0, uint64_t arg6 = 0,
                                                        uint16_t client_id = 0,
                                                        uint16_t secure_os_id = 0) {
    return {CreatePilFunctionId(cmd), args,
            static_cast<uint64_t>(pas_id), arg3, arg4, arg5, arg6, client_id, secure_os_id};
}

class PilDevice;
using DeviceType = ddk::Device<PilDevice, ddk::Unbindable>;

class PilDevice : public DeviceType {
public:
    static zx_status_t Create(zx_device_t* parent);

    explicit PilDevice(zx_device_t* parent)
        : DeviceType(parent), pdev_(parent) {}

    zx_status_t Bind();
    zx_status_t Init();

    // Methods required by the ddk mixins
    void DdkUnbind();
    void DdkRelease();

private:
    void ShutDown();

    ddk::PDev pdev_;
    zx::resource smc_;
};
} // namespace qcom_pil
