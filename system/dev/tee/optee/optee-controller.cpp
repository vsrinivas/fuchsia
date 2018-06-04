// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/limits.h>
#include <fbl/unique_ptr.h>

#include "optee-controller.h"
#include "optee-smc.h"

namespace optee {

static bool IsOpteeMsgApi(const tee::TrustedOsCallUidResult& returned_uid) {
    return returned_uid.uid_0_3 == kOpteeApiUid_0 &&
           returned_uid.uid_4_7 == kOpteeApiUid_1 &&
           returned_uid.uid_8_11 == kOpteeApiUid_2 &&
           returned_uid.uid_12_15 == kOpteeApiUid_3;
}

static bool IsOpteeApiRevisionSupported(const tee::TrustedOsCallRevisionResult& returned_rev) {
    // The cast is unfortunately necessary to mute a compiler warning about an unsigned expression
    // always being greater than 0.
    ZX_DEBUG_ASSERT(returned_rev.minor <= fbl::numeric_limits<int32_t>::max());
    return returned_rev.major == kOpteeApiRevisionMajor &&
           static_cast<int32_t>(returned_rev.minor) >= static_cast<int32_t>(kOpteeApiRevisionMinor);
}

zx_status_t OpteeController::ValidateApiUid() const {
    static const zx_smc_parameters_t kGetApiMsg = tee::CreateSmcMessage(
        tee::kTrustedOsCallUidFuncId);
    union {
        zx_smc_result_t raw;
        tee::TrustedOsCallUidResult uid;
    } result;
    zx_status_t status = zx_smc_call(secure_monitor_, &kGetApiMsg, &result.raw);

    return status == ZX_OK
               ? IsOpteeMsgApi(result.uid) ? ZX_OK : ZX_ERR_NOT_FOUND
               : status;
}

zx_status_t OpteeController::ValidateApiRevision() const {
    static const zx_smc_parameters_t kGetApiRevisionMsg = tee::CreateSmcMessage(
        tee::kTrustedOsCallRevisionFuncId);
    union {
        zx_smc_result_t raw;
        tee::TrustedOsCallRevisionResult revision;
    } result;
    zx_status_t status = zx_smc_call(secure_monitor_, &kGetApiRevisionMsg, &result.raw);

    return status == ZX_OK
               ? IsOpteeApiRevisionSupported(result.revision) ? ZX_OK : ZX_ERR_NOT_SUPPORTED
               : status;
}

zx_status_t OpteeController::GetOsRevision() {
    static const zx_smc_parameters_t kGetOsRevisionMsg = tee::CreateSmcMessage(
        kGetOsRevisionFuncId);
    union {
        zx_smc_result_t raw;
        GetOsRevisionResult revision;
    } result;
    zx_status_t status = zx_smc_call(secure_monitor_, &kGetOsRevisionMsg, &result.raw);

    if (status != ZX_OK) {
        return status;
    }

    os_revision_.major = result.revision.major;
    os_revision_.minor = result.revision.minor;

    return ZX_OK;
}

zx_status_t OpteeController::ExchangeCapabilities() {
    uint64_t nonsecure_world_capabilities = 0;
    if (zx_system_get_num_cpus() == 1) {
        nonsecure_world_capabilities |= kNonSecureCapUniprocessor;
    }

    const zx_smc_parameters_t message = tee::CreateSmcMessage(kExchangeCapabilitiesFuncId,
                                                              nonsecure_world_capabilities);
    union {
        zx_smc_result_t raw;
        ExchangeCapabilitiesResult response;
    } result;

    zx_status_t status = zx_smc_call(secure_monitor_, &message, &result.raw);

    if (status != ZX_OK) {
        return status;
    }

    if (result.response.status != kReturnOk) {
        return ZX_ERR_INTERNAL;
    }

    secure_world_capabilities_ = result.response.secure_world_capabilities;

    return ZX_OK;
}

zx_status_t OpteeController::Bind() {
    zx_status_t status = ZX_ERR_INTERNAL;

    status = device_get_protocol(parent(), ZX_PROTOCOL_PLATFORM_DEV, &pdev_proto_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee: Unable to get pdev protocol\n");
        return status;
    }

    // TODO(rjascani): Replace this with a real secure monitor only resource
    secure_monitor_ = get_root_resource();

    // TODO(MTWN-140): Remove this once we have a tee core driver that will discover the TEE OS
    status = ValidateApiUid();
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee: API UID does not match\n");
        return status;
    }

    status = ValidateApiRevision();
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee: API revision not supported\n");
        return status;
    }

    status = GetOsRevision();
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee: Unable to get Trusted OS revision\n");
        return status;
    }

    status = ExchangeCapabilities();
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee: Could not exchange capabilities\n");
        return status;
    }

    status = DdkAdd("optee-tz");
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee: Failed to add device\n");
        return status;
    }

    return status;
}

void OpteeController::DdkUnbind() {
    // Unpublish our device node.
    DdkRemove();
}

void OpteeController::DdkRelease() {
    // devmgr has given up ownership, so we must clean ourself up.
    delete this;
}

zx_status_t OpteeController::GetDescription(tee_ioctl_description_t* out_description,
                                            size_t* out_size) const {
    // The OP-TEE UUID does not vary and since we validated that the TEE is OP-TEE by checking
    // the API UID, we can skip the OS UUID SMC call and just return the static UUID.
    out_description->os_uuid[0] = kOpteeOsUuid_0;
    out_description->os_uuid[1] = kOpteeOsUuid_1;
    out_description->os_uuid[2] = kOpteeOsUuid_2;
    out_description->os_uuid[3] = kOpteeOsUuid_3;
    out_description->os_revision = os_revision_;
    out_description->is_global_platform_compliant = true;

    *out_size = sizeof(*out_description);

    return ZX_OK;
}

zx_status_t OpteeController::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                                      void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_TEE_GET_DESCRIPTION: {
        if ((out_buf == nullptr) || (out_len != sizeof(tee_ioctl_description_t)) ||
            (out_actual == nullptr)) {
            return ZX_ERR_INVALID_ARGS;
        }

        return GetDescription(reinterpret_cast<tee_ioctl_description_t*>(out_buf), out_actual);
    }
    }

    return ZX_ERR_NOT_SUPPORTED;
}

} // namespace optee

extern "C" zx_status_t optee_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto tee = fbl::make_unique_checked<::optee::OpteeController>(&ac, parent);

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = tee->Bind();
    if (status == ZX_OK) {
        // devmgr is now in charge of the memory for tee
        __UNUSED auto ptr = tee.release();
    }

    return status;
}
