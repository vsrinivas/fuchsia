// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-client.h"

#include "optee-smc.h"
#include <ddk/debug.h>

namespace optee {

// These values are defined by the TEE Client API and are here to cover the few cases where
// failure occurs at the communication/driver layer.
constexpr uint32_t kTeecErrorCommunication = 0xFFFF000E;

constexpr uint32_t kTeecOriginComms = 0x00000002;

zx_status_t OpteeClient::DdkClose(uint32_t flags) {
    controller_->RemoveClient(this);
    return ZX_OK;
}

void OpteeClient::DdkRelease() {
    // devmgr has given up ownership, so we must clean ourself up.
    delete this;
}

zx_status_t OpteeClient::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                                  size_t out_len, size_t* out_actual) {
    if (needs_to_close_) {
        return ZX_ERR_PEER_CLOSED;
    }

    switch (op) {
    case IOCTL_TEE_GET_DESCRIPTION: {
        if ((out_buf == nullptr) || (out_len != sizeof(tee_ioctl_description_t)) ||
            (out_actual == nullptr)) {
            return ZX_ERR_INVALID_ARGS;
        }

        return controller_->GetDescription(reinterpret_cast<tee_ioctl_description_t*>(out_buf),
                                           out_actual);
    }
    case IOCTL_TEE_OPEN_SESSION: {
        if ((in_buf == nullptr) || (in_len != sizeof(tee_ioctl_session_request_t)) ||
            (out_buf == nullptr) || (out_len != sizeof(tee_ioctl_session_t)) ||
            (out_actual == nullptr)) {
            return ZX_ERR_INVALID_ARGS;
        }

        return OpenSession(reinterpret_cast<const tee_ioctl_session_request_t*>(in_buf),
                           reinterpret_cast<tee_ioctl_session_t*>(out_buf),
                           out_actual);
    }
    }

    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t OpteeClient::OpenSession(const tee_ioctl_session_request_t* session_request,
                                     tee_ioctl_session_t* out_session,
                                     size_t* out_actual) {
    ZX_DEBUG_ASSERT(session_request != nullptr);
    ZX_DEBUG_ASSERT(out_session != nullptr);
    ZX_DEBUG_ASSERT(out_actual != nullptr);
    *out_actual = 0;

    UuidView trusted_app{session_request->trusted_app, TEE_IOCTL_UUID_SIZE};
    UuidView client_app{session_request->client_app, TEE_IOCTL_UUID_SIZE};

    fbl::Array<MessageParam> params;
    zx_status_t status = ConvertIoctlParamsToOpteeParams(session_request->params,
                                                         session_request->num_params,
                                                         &params);
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee: invalid ioctl parameters\n");
        out_session->return_code = kTeecErrorCommunication;
        out_session->return_origin = kTeecOriginComms;
        return status;
    }

    auto message = OpenSessionMessage::Create(controller_->driver_pool(),
                                              trusted_app,
                                              client_app,
                                              session_request->client_login,
                                              session_request->cancel_id,
                                              params);

    if (controller_->CallWithMessage(message) != kReturnOk) {
        zxlogf(ERROR, "optee: failed to communicate with OP-TEE\n");
        out_session->return_code = kTeecErrorCommunication;
        out_session->return_origin = kTeecOriginComms;
    } else {
        // TODO(rjascani): Create session object from session id
        out_session->session_id = message.session_id();
        out_session->return_code = message.return_code();
        out_session->return_origin = message.return_origin();
    }

    *out_actual = sizeof(*out_session);

    return ZX_OK;
}

zx_status_t OpteeClient::ConvertIoctlParamsToOpteeParams(
    const tee_ioctl_param_t* params,
    size_t num_params,
    fbl::Array<MessageParam>* out_optee_params) {
    ZX_DEBUG_ASSERT(params != nullptr);
    ZX_DEBUG_ASSERT(out_optee_params != nullptr);

    fbl::Array<MessageParam> optee_params(new MessageParam[num_params], num_params);

    for (size_t i = 0; i < num_params; ++i) {
        const tee_ioctl_param_t* ioctl_param = params + i;

        switch (ioctl_param->type) {
        case TEE_PARAM_TYPE_NONE:
            optee_params[i].attribute = MessageParam::kAttributeTypeNone;
            optee_params[i].payload.value.a = 0;
            optee_params[i].payload.value.b = 0;
            optee_params[i].payload.value.c = 0;
            break;
        case TEE_PARAM_TYPE_VALUE_INPUT:
            optee_params[i].attribute = MessageParam::kAttributeTypeValueInput;
            optee_params[i].payload.value.a = ioctl_param->a;
            optee_params[i].payload.value.b = ioctl_param->b;
            optee_params[i].payload.value.c = ioctl_param->c;
            break;
        case TEE_PARAM_TYPE_VALUE_OUTPUT:
            optee_params[i].attribute = MessageParam::kAttributeTypeValueOutput;
            optee_params[i].payload.value.a = ioctl_param->a;
            optee_params[i].payload.value.b = ioctl_param->b;
            optee_params[i].payload.value.c = ioctl_param->c;
            break;
        case TEE_PARAM_TYPE_VALUE_INOUT:
            optee_params[i].attribute = MessageParam::kAttributeTypeValueInOut;
            optee_params[i].payload.value.a = ioctl_param->a;
            optee_params[i].payload.value.b = ioctl_param->b;
            optee_params[i].payload.value.c = ioctl_param->c;
            break;
        case TEE_PARAM_TYPE_MEMREF_INPUT:
        case TEE_PARAM_TYPE_MEMREF_OUTPUT:
        case TEE_PARAM_TYPE_MEMREF_INOUT:
            // TODO(rjascani): Add support for memory references
            return ZX_ERR_NOT_SUPPORTED;
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }
    }

    *out_optee_params = fbl::move(optee_params);
    return ZX_OK;
}

} // namespace optee
