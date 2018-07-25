// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/limits.h>
#include <fbl/unique_ptr.h>

#include "optee-client.h"
#include "optee-controller.h"
#include "optee-smc.h"

namespace optee {

static bool IsOpteeApi(const tee::TrustedOsCallUidResult& returned_uid) {
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
    static const zx_smc_parameters_t kGetApiFuncCall = tee::CreateSmcFunctionCall(
        tee::kTrustedOsCallUidFuncId);
    union {
        zx_smc_result_t raw;
        tee::TrustedOsCallUidResult uid;
    } result;
    zx_status_t status = zx_smc_call(secure_monitor_, &kGetApiFuncCall, &result.raw);

    return status == ZX_OK
               ? IsOpteeApi(result.uid) ? ZX_OK : ZX_ERR_NOT_FOUND
               : status;
}

zx_status_t OpteeController::ValidateApiRevision() const {
    static const zx_smc_parameters_t kGetApiRevisionFuncCall = tee::CreateSmcFunctionCall(
        tee::kTrustedOsCallRevisionFuncId);
    union {
        zx_smc_result_t raw;
        tee::TrustedOsCallRevisionResult revision;
    } result;
    zx_status_t status = zx_smc_call(secure_monitor_, &kGetApiRevisionFuncCall, &result.raw);

    return status == ZX_OK
               ? IsOpteeApiRevisionSupported(result.revision) ? ZX_OK : ZX_ERR_NOT_SUPPORTED
               : status;
}

zx_status_t OpteeController::GetOsRevision() {
    static const zx_smc_parameters_t kGetOsRevisionFuncCall = tee::CreateSmcFunctionCall(
        kGetOsRevisionFuncId);
    union {
        zx_smc_result_t raw;
        GetOsRevisionResult revision;
    } result;
    zx_status_t status = zx_smc_call(secure_monitor_, &kGetOsRevisionFuncCall, &result.raw);

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

    const zx_smc_parameters_t func_call = tee::CreateSmcFunctionCall(kExchangeCapabilitiesFuncId,
                                                                     nonsecure_world_capabilities);
    union {
        zx_smc_result_t raw;
        ExchangeCapabilitiesResult response;
    } result;

    zx_status_t status = zx_smc_call(secure_monitor_, &func_call, &result.raw);

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

zx_status_t OpteeController::DdkOpen(zx_device_t** out_dev, uint32_t flags) {
    // Create a new OpteeClient device and hand off client communication to it.
    fbl::AllocChecker ac;
    auto client = fbl::make_unique_checked<OpteeClient>(&ac, this);

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = client->DdkAdd("optee-client", DEVICE_ADD_INSTANCE);
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the memory for the tee client
    OpteeClient* client_ptr = client.release();
    *out_dev = client_ptr->zxdev();

    AddClient(client_ptr);

    return ZX_OK;
}

void OpteeController::AddClient(OpteeClient* client) {
    fbl::AutoLock lock(&lock_);
    clients_.push_back(client);
}

void OpteeController::CloseClients() {
    fbl::AutoLock lock(&lock_);
    for (auto& client : clients_) {
        client.MarkForClosing();
    }
}

void OpteeController::DdkUnbind() {
    CloseClients();
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

void OpteeController::RemoveClient(OpteeClient* client) {
    fbl::AutoLock lock(&lock_);
    ZX_DEBUG_ASSERT(client != nullptr);
    if (client->InContainer()) {
        clients_.erase(*client);
    }
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
