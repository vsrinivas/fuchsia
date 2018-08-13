// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/type_support.h>
#include <string.h>

#include "optee-client.h"
#include "optee-smc.h"

template <typename SRC_T, typename DST_T>
static constexpr typename fbl::enable_if<
    fbl::is_unsigned_integer<SRC_T>::value &&
    fbl::is_unsigned_integer<DST_T>::value>::type
SplitInto32BitParts(SRC_T src, DST_T* dst_hi, DST_T* dst_lo) {
    static_assert(sizeof(SRC_T) == 8, "Type SRC_T should be 64 bits!");
    static_assert(sizeof(DST_T) >= 4, "Type DST_T should be at least 32 bits!");
    ZX_DEBUG_ASSERT(dst_hi != nullptr);
    ZX_DEBUG_ASSERT(dst_lo != nullptr);
    *dst_hi = static_cast<DST_T>(src >> 32);
    *dst_lo = static_cast<DST_T>(static_cast<uint32_t>(src));
}

template <typename SRC_T, typename DST_T>
static constexpr typename fbl::enable_if<
    fbl::is_unsigned_integer<SRC_T>::value &&
    fbl::is_unsigned_integer<DST_T>::value>::type
JoinFrom32BitParts(SRC_T src_hi, SRC_T src_lo, DST_T* dst) {
    static_assert(sizeof(SRC_T) >= 4, "Type SRC_T should be at least 32 bits!");
    static_assert(sizeof(DST_T) >= 8, "Type DST_T should be at least 64-bits!");
    ZX_DEBUG_ASSERT(dst != nullptr);
    *dst = (static_cast<DST_T>(src_hi) << 32) | static_cast<DST_T>(static_cast<uint32_t>(src_lo));
}

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

    if (controller_->CallWithMessage(message,
                                     fbl::BindMember(this, &OpteeClient::HandleRpc)) != kReturnOk) {
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

zx_status_t OpteeClient::AllocateSharedMemory(size_t size, SharedMemory** out_shared_memory) {
    ZX_DEBUG_ASSERT(out_shared_memory != nullptr);

    if (size == 0) {
        *out_shared_memory = nullptr;
        return ZX_OK;
    }

    fbl::unique_ptr<SharedMemory> sh_mem;
    zx_status_t status = controller_->driver_pool()->Allocate(size, &sh_mem);
    if (status == ZX_OK) {
        // Track the new piece of allocated SharedMemory in the list
        allocated_shared_memory_.push_back(fbl::move(sh_mem));
        *out_shared_memory = &allocated_shared_memory_.back();

        // TODO(godtamit): Remove this when all of RPC is implemented
        zxlogf(INFO,
               "optee: allocated shared memory at physical addr 0x%" PRIuPTR
               " with cookie 0x%" PRIuPTR "\n",
               (*out_shared_memory)->paddr(),
               reinterpret_cast<uintptr_t>(out_shared_memory));
    }

    return status;
}

zx_status_t OpteeClient::HandleRpc(const RpcFunctionArgs& args, RpcFunctionResult* out_result) {
    ZX_DEBUG_ASSERT(out_result != nullptr);

    zx_status_t status;
    uint32_t func_code = GetRpcFunctionCode(args.generic.status);

    switch (func_code) {
    case kRpcFunctionIdAllocateMemory:
        status = HandleRpcAllocateMemory(args.allocate_memory, &out_result->allocate_memory);
        break;
    case kRpcFunctionIdFreeMemory:
        status = HandleRpcFreeMemory(args.free_memory, &out_result->free_memory);
        break;
    case kRpcFunctionIdDeliverIrq:
        status = ZX_ERR_NOT_SUPPORTED;
        break;
    case kRpcFunctionIdExecuteCommand:
        status = ZX_ERR_NOT_SUPPORTED;
        break;
    default:
        status = ZX_ERR_NOT_SUPPORTED;
        break;
    }

    // Set the function to return from RPC
    out_result->generic.func_id = optee::kReturnFromRpcFuncId;

    return status;
}

/* HandleRpcAllocateMemory
 *
 * Fulfill request from secure world to allocate memory.
 *
 * Return:
 * ZX_OK:               Memory allocation was successful.
 * ZX_ERR_NO_MEMORY:    Memory allocation failed due to resource constraints.
 */
zx_status_t OpteeClient::HandleRpcAllocateMemory(const RpcFunctionAllocateMemoryArgs& args,
                                                 RpcFunctionAllocateMemoryResult* out_result) {
    ZX_DEBUG_ASSERT(out_result != nullptr);

    SharedMemory* sh_mem;

    zx_status_t status = AllocateSharedMemory(static_cast<size_t>(args.size), &sh_mem);
    if (status == ZX_OK && sh_mem != nullptr) {
        // Put the physical address of allocated memory in the args
        SplitInto32BitParts(sh_mem->paddr(),
                            &out_result->phys_addr_upper32,
                            &out_result->phys_addr_lower32);

        // Use address of the linked list node of SharedMemory as the "cookie" to this memory
        SplitInto32BitParts(reinterpret_cast<uintptr_t>(sh_mem),
                            &out_result->mem_cookie_upper32,
                            &out_result->mem_cookie_lower32);
    } else {
        out_result->phys_addr_upper32 = 0;
        out_result->phys_addr_lower32 = 0;
        out_result->mem_cookie_upper32 = 0;
        out_result->mem_cookie_lower32 = 0;
    }

    return status;
}

/* HandleRpcFreeMemory
 *
 * Fulfill request from secure world to free previously allocated memory.
 *
 * Return:
 * ZX_OK:               Memory free was successful.
 * ZX_ERR_NOT_FOUND:    Requested memory address to free was not found in the allocated list.
 */
zx_status_t OpteeClient::HandleRpcFreeMemory(const RpcFunctionFreeMemoryArgs& args,
                                             RpcFunctionFreeMemoryResult* out_result) {
    ZX_DEBUG_ASSERT(out_result != nullptr);

    uintptr_t memory_cookie;
    JoinFrom32BitParts(args.mem_cookie_upper32, args.mem_cookie_lower32, &memory_cookie);

    // Use the 64-bit "cookie" as the address for the SharedMemory
    SharedMemory* sh_mem_raw = reinterpret_cast<SharedMemory*>(memory_cookie);

    // Remove memory block from allocated list (if it exists)
    fbl::unique_ptr<SharedMemory> sh_mem = allocated_shared_memory_.erase_if(
        [sh_mem_raw](const SharedMemory& item) {
            return &item == sh_mem_raw;
        });

    // TODO(godtamit): Remove this when all of RPC is implemented
    zxlogf(INFO,
           "optee: successfully freed shared memory at phys 0x%" PRIuPTR "\n",
           sh_mem->paddr());

    // When sh_mem falls out of scope, destructor will automatically free block back into pool
    return ZX_OK;
}

} // namespace optee
