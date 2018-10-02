// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <fbl/type_support.h>
#include <lib/zx/vmo.h>
#include <tee-client-api/tee-client-types.h>

#include "optee-client.h"
#include "optee-smc.h"

namespace {
// RFC 4122 specification dictates a UUID is of the form xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
constexpr const char* kUuidNameFormat = "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x";
constexpr size_t kUuidNameLength = 36;

constexpr const char kFirmwarePathPrefix[] = "/boot/lib/firmware/";
constexpr const char kTaFileExtension[] = ".ta";

// The length of a path to a trusted app consists of the path prefix, the UUID, and file extension
// Subtracting 1 from sizeof(char[])s to account for the terminating null character.
constexpr size_t kTaPathLength = (sizeof(kFirmwarePathPrefix) - 1u) +
                                 kUuidNameLength +
                                 (sizeof(kTaFileExtension) - 1u);

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

// Builds a UUID string from a TEEC_UUID, formatting as per the RFC 4122 specification.
static fbl::StringBuffer<kUuidNameLength> BuildUuidString(const TEEC_UUID& ta_uuid) {
    fbl::StringBuffer<kUuidNameLength> buf;

    buf.AppendPrintf(kUuidNameFormat,
                     ta_uuid.timeLow,
                     ta_uuid.timeMid,
                     ta_uuid.timeHiAndVersion,
                     ta_uuid.clockSeqAndNode[0],
                     ta_uuid.clockSeqAndNode[1],
                     ta_uuid.clockSeqAndNode[2],
                     ta_uuid.clockSeqAndNode[3],
                     ta_uuid.clockSeqAndNode[4],
                     ta_uuid.clockSeqAndNode[5],
                     ta_uuid.clockSeqAndNode[6],
                     ta_uuid.clockSeqAndNode[7]);
    return buf;
}

// Builds the expected path to a trusted application, given its UUID string.
static fbl::StringBuffer<kTaPathLength> BuildTaPath(const fbl::StringPiece& uuid_str) {
    fbl::StringBuffer<kTaPathLength> buf;

    buf.Append(kFirmwarePathPrefix);
    buf.Append(uuid_str);
    buf.Append(kTaFileExtension);

    return buf;
}
}; // namespace

namespace optee {

zircon_tee_Device_ops_t OpteeClient::kFidlOps = {
    .GetOsInfo =
        [](void* ctx, fidl_txn_t* txn) {
            return reinterpret_cast<OpteeClient*>(ctx)->GetOsInfo(txn);
        },
    .OpenSession =
        [](void* ctx, const zircon_tee_Uuid* trusted_app,
           const zircon_tee_ParameterSet* parameter_set, fidl_txn_t* txn) {
            return reinterpret_cast<OpteeClient*>(ctx)->OpenSession(trusted_app, parameter_set,
                                                                    txn);
        },
    .InvokeCommand =
        [](void* ctx, uint32_t session_id, uint32_t command_id,
           const zircon_tee_ParameterSet* parameter_set, fidl_txn_t* txn) {
            return reinterpret_cast<OpteeClient*>(ctx)->InvokeCommand(session_id, command_id,
                                                                      parameter_set, txn);
        },
    .CloseSession =
        [](void* ctx, uint32_t session_id, fidl_txn_t* txn) {
            return reinterpret_cast<OpteeClient*>(ctx)->CloseSession(session_id, txn);
        },
};

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

zx_status_t OpteeClient::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    if (needs_to_close_) {
        // The underlying channel is owned by the devhost and thus we do not need to directly close
        // it. This check exists for the scenario where we are in the process of unbinding the
        // parent device and cannot fulfill any requests any more. The underlying channel will be
        // closed by devhost once the unbind is complete.
        return ZX_ERR_PEER_CLOSED;
    }
    return zircon_tee_Device_dispatch(this, txn, msg, &kFidlOps);
}

zx_status_t OpteeClient::GetOsInfo(fidl_txn_t* txn) const {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t OpteeClient::OpenSession(const zircon_tee_Uuid* trusted_app,
                                     const zircon_tee_ParameterSet* parameter_set,
                                     fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t OpteeClient::InvokeCommand(uint32_t session_id,
                                       uint32_t command_id,
                                       const zircon_tee_ParameterSet* parameter_set,
                                       fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t OpteeClient::CloseSession(uint32_t session_id,
                                      fidl_txn_t* txn) {
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
        out_session->return_code = TEEC_ERROR_BAD_PARAMETERS;
        out_session->return_origin = TEEC_ORIGIN_COMMS;
        return status;
    }

    OpenSessionMessage message(controller_->driver_pool(),
                               trusted_app,
                               client_app,
                               session_request->client_login,
                               session_request->cancel_id,
                               params);

    *out_actual = sizeof(*out_session);
    uint32_t call_code =
        controller_->CallWithMessage(message, fbl::BindMember(this, &OpteeClient::HandleRpc));
    if (call_code != kReturnOk) {
        out_session->return_code = TEEC_ERROR_COMMUNICATION;
        out_session->return_origin = TEEC_ORIGIN_COMMS;
        return status;
    }

    // TODO(rjascani): Create session object from session id
    out_session->session_id = message.session_id();
    out_session->return_code = message.return_code();
    out_session->return_origin = message.return_origin();
    // TODO(godtamit): Remove this when all of RPC is implemented
    zxlogf(INFO,
           "session ID is 0x%x, return code is 0x%x, return origin is 0x%x\n",
           out_session->session_id,
           out_session->return_code,
           out_session->return_origin);

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
        const tee_ioctl_param_t& ioctl_param = params[i];
        MessageParam& optee_param = optee_params[i];

        switch (ioctl_param.type) {
        case TEE_PARAM_TYPE_NONE:
            optee_param.attribute = MessageParam::kAttributeTypeNone;
            optee_param.payload.value.generic.a = 0;
            optee_param.payload.value.generic.b = 0;
            optee_param.payload.value.generic.c = 0;
            break;
        case TEE_PARAM_TYPE_VALUE_INPUT:
            optee_param.attribute = MessageParam::kAttributeTypeValueInput;
            optee_param.payload.value.generic.a = ioctl_param.a;
            optee_param.payload.value.generic.b = ioctl_param.b;
            optee_param.payload.value.generic.c = ioctl_param.c;
            break;
        case TEE_PARAM_TYPE_VALUE_OUTPUT:
            optee_param.attribute = MessageParam::kAttributeTypeValueOutput;
            optee_param.payload.value.generic.a = ioctl_param.a;
            optee_param.payload.value.generic.b = ioctl_param.b;
            optee_param.payload.value.generic.c = ioctl_param.c;
            break;
        case TEE_PARAM_TYPE_VALUE_INOUT:
            optee_param.attribute = MessageParam::kAttributeTypeValueInOut;
            optee_param.payload.value.generic.a = ioctl_param.a;
            optee_param.payload.value.generic.b = ioctl_param.b;
            optee_param.payload.value.generic.c = ioctl_param.c;
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

template <typename SharedMemoryPoolTraits>
zx_status_t OpteeClient::AllocateSharedMemory(size_t size,
                                              SharedMemoryPool<SharedMemoryPoolTraits>* memory_pool,
                                              zx_paddr_t* out_phys_addr,
                                              uint64_t* out_mem_id) {
    ZX_DEBUG_ASSERT(memory_pool != nullptr);
    ZX_DEBUG_ASSERT(out_phys_addr != nullptr);
    ZX_DEBUG_ASSERT(out_mem_id != nullptr);

    // Set these to 0 and overwrite, if necessary, on success path
    *out_phys_addr = 0;
    *out_mem_id = 0;

    if (size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::unique_ptr<SharedMemory> sh_mem;
    zx_status_t status = memory_pool->Allocate(size, &sh_mem);
    if (status != ZX_OK) {
        return status;
    }

    *out_phys_addr = sh_mem->paddr();

    // Track the new piece of allocated SharedMemory in the list
    allocated_shared_memory_.push_back(fbl::move(sh_mem));

    // TODO(godtamit): Move away from memory addresses as memory identifiers
    //
    // Make the memory identifier the address of the SharedMemory object
    auto sh_mem_addr = reinterpret_cast<uintptr_t>(&allocated_shared_memory_.back());
    *out_mem_id = static_cast<uint64_t>(sh_mem_addr);

    // TODO(godtamit): Remove when all RPC is done
    zxlogf(INFO,
           "optee: allocated shared memory at physical addr 0x%" PRIuPTR
           " with id 0x%" PRIu64 "\n",
           *out_phys_addr,
           *out_mem_id);

    return status;
}

zx_status_t OpteeClient::FreeSharedMemory(uint64_t mem_id) {
    // Check if client owns memory that matches the memory id
    SharedMemoryList::iterator mem_iter = FindSharedMemory(mem_id);
    if (mem_iter == allocated_shared_memory_.end()) {
        return ZX_ERR_NOT_FOUND;
    }

    // Destructor of SharedMemory will automatically free block back into pool
    //
    // TODO(godtamit): Remove mem_to_free and logging when all of RPC is implemented
    __UNUSED auto mem_to_free = allocated_shared_memory_.erase(mem_iter);
    zxlogf(INFO,
           "optee: successfully freed shared memory at phys 0x%" PRIuPTR "\n",
           mem_to_free->paddr());

    return ZX_OK;
}

OpteeClient::SharedMemoryList::iterator OpteeClient::FindSharedMemory(uint64_t mem_id) {
    // TODO(godtamit): Move away from memory addresses as memory identifiers
    auto mem_id_ptr_val = static_cast<uintptr_t>(mem_id);
    return allocated_shared_memory_.find_if(
        [mem_id_ptr_val](auto& item) {
            return mem_id_ptr_val == reinterpret_cast<uintptr_t>(&item);
        });
}

zx_status_t OpteeClient::HandleRpc(const RpcFunctionArgs& args, RpcFunctionResult* out_result) {
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
        // TODO(godtamit): Remove when all of RPC is implemented
        zxlogf(INFO, "optee: delivering IRQ\n");
        // Foreign interrupt detected while in the secure world
        // Zircon handles this so just mark the RPC as handled
        status = ZX_OK;
        break;
    case kRpcFunctionIdExecuteCommand:
        status = HandleRpcCommand(args.execute_command, &out_result->execute_command);
        break;
    default:
        status = ZX_ERR_NOT_SUPPORTED;
        break;
    }

    // Set the function to return from RPC
    out_result->generic.func_id = optee::kReturnFromRpcFuncId;

    return status;
}

zx_status_t OpteeClient::HandleRpcAllocateMemory(const RpcFunctionAllocateMemoryArgs& args,
                                                 RpcFunctionAllocateMemoryResult* out_result) {
    ZX_DEBUG_ASSERT(out_result != nullptr);

    zx_paddr_t paddr;
    uint64_t mem_id;

    zx_status_t status = AllocateSharedMemory(static_cast<size_t>(args.size),
                                              controller_->driver_pool(),
                                              &paddr,
                                              &mem_id);
    // If allocation failed, AllocateSharedMemory sets paddr and mem_id to 0. Continue with packing
    // those values into the result regardless.

    // Put the physical address of allocated memory in the args
    SplitInto32BitParts(paddr, &out_result->phys_addr_upper32, &out_result->phys_addr_lower32);

    // Pack the memory identifier in the args
    SplitInto32BitParts(mem_id, &out_result->mem_id_upper32, &out_result->mem_id_lower32);

    return status;
}

zx_status_t OpteeClient::HandleRpcFreeMemory(const RpcFunctionFreeMemoryArgs& args,
                                             RpcFunctionFreeMemoryResult* out_result) {
    ZX_DEBUG_ASSERT(out_result != nullptr);

    uint64_t mem_id;
    JoinFrom32BitParts(args.mem_id_upper32, args.mem_id_lower32, &mem_id);

    return FreeSharedMemory(mem_id);
}

zx_status_t OpteeClient::HandleRpcCommand(const RpcFunctionExecuteCommandsArgs& args,
                                          RpcFunctionExecuteCommandsResult* out_result) {
    uint64_t mem_id;
    JoinFrom32BitParts(args.msg_mem_id_upper32, args.msg_mem_id_lower32, &mem_id);

    // Make sure memory where message is stored is valid
    // This dispatcher method only checks that the memory needed for the header is valid. Commands
    // that require more memory than just the header will need to do further memory checks.
    SharedMemoryList::iterator mem_iter = FindSharedMemory(mem_id);
    if (mem_iter == allocated_shared_memory_.end()) {
        zxlogf(ERROR, "optee: invalid shared memory region passed into RPC command!\n");
        return ZX_ERR_INVALID_ARGS;
    } else if (mem_iter->size() < sizeof(MessageHeader)) {
        zxlogf(ERROR,
               "optee: shared memory region passed into RPC command is too small\n");
        return ZX_ERR_INVALID_ARGS;
    }

    // Read message header from shared memory
    SharedMemory& msg_mem = *mem_iter;
    RpcMessage message(&msg_mem);
    if (!message.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Mark that the return code will originate from driver
    message.set_return_origin(TEEC_ORIGIN_COMMS);

    switch (message.command()) {
    case RpcMessage::Command::kLoadTa: {
        LoadTaRpcMessage load_ta_msg(fbl::move(message));
        if (!load_ta_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandLoadTa(&load_ta_msg);
    }
    case RpcMessage::Command::kAccessFileSystem: {
        FileSystemRpcMessage fs_msg(fbl::move(message));
        if (!fs_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandFileSystem(&fs_msg);
    }
    case RpcMessage::Command::kGetTime:
        zxlogf(ERROR, "optee: RPC command to access file system recognized but not implemented\n");
        return ZX_ERR_NOT_SUPPORTED;
    case RpcMessage::Command::kWaitQueue:
        zxlogf(ERROR, "optee: RPC command wait queue recognized but not implemented\n");
        return ZX_ERR_NOT_SUPPORTED;
    case RpcMessage::Command::kSuspend:
        zxlogf(ERROR, "optee: RPC command to suspend recognized but not implemented\n");
        return ZX_ERR_NOT_SUPPORTED;
    case RpcMessage::Command::kAllocateMemory: {
        AllocateMemoryRpcMessage alloc_mem_msg(fbl::move(message));
        if (!alloc_mem_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandAllocateMemory(&alloc_mem_msg);
    }
    case RpcMessage::Command::kFreeMemory: {
        FreeMemoryRpcMessage free_mem_msg(fbl::move(message));
        if (!free_mem_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandFreeMemory(&free_mem_msg);
    }
    case RpcMessage::Command::kPerformSocketIo:
        zxlogf(ERROR, "optee: RPC command to perform socket IO recognized but not implemented\n");
        message.set_return_code(TEEC_ERROR_NOT_SUPPORTED);
        return ZX_OK;
    case RpcMessage::Command::kAccessReplayProtectedMemoryBlock:
    case RpcMessage::Command::kAccessSqlFileSystem:
    case RpcMessage::Command::kLoadGprof:
        zxlogf(INFO, "optee: received unsupported RPC command\n");
        message.set_return_code(TEEC_ERROR_NOT_SUPPORTED);
        return ZX_OK;
    default:
        zxlogf(ERROR,
               "optee: unrecognized command passed to RPC 0x%" PRIu32 "\n",
               message.command());
        message.set_return_code(TEEC_ERROR_NOT_SUPPORTED);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t OpteeClient::HandleRpcCommandLoadTa(LoadTaRpcMessage* message) {
    ZX_DEBUG_ASSERT(message != nullptr);
    ZX_DEBUG_ASSERT(message->is_valid());

    // The amount of memory available for loading the TA
    uint64_t mem_usable_size = message->memory_reference_size() -
                               message->memory_reference_offset();

    // Try to find the SharedMemory based on the memory id
    uint8_t* out_ta_mem; // Where to write the TA in memory

    if (message->memory_reference_id() != 0) {
        SharedMemoryList::iterator out_mem_iter = FindSharedMemory(message->memory_reference_id());
        if (out_mem_iter == allocated_shared_memory_.end()) {
            // Valid memory reference could not be found and TEE is not querying size
            zxlogf(ERROR,
                   "optee: received invalid memory reference from TEE command to load TA!\n");
            message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
            return ZX_ERR_INVALID_ARGS;
        } else if (mem_usable_size > out_mem_iter->size()) {
            // The TEE is claiming the memory reference given is larger than it actually is
            // We want to catch this in case TEE is buggy
            zxlogf(ERROR,
                   "optee: TEE claimed a memory reference's size is larger than the real memory"
                   "size!\n");
            message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
            return ZX_ERR_INVALID_ARGS;
        }

        out_ta_mem = reinterpret_cast<uint8_t*>(out_mem_iter->vaddr() +
                                                message->memory_reference_offset());
    } else {
        // TEE is just querying size of TA, so it sent a memory identifier of 0
        ZX_DEBUG_ASSERT(message->memory_reference_offset() == 0);
        ZX_DEBUG_ASSERT(message->memory_reference_size() == 0);

        out_ta_mem = nullptr;
    }

    auto ta_name = BuildUuidString(message->ta_uuid());
    auto ta_path = BuildTaPath(ta_name.ToStringPiece());

    // Load the trusted app into a VMO
    size_t ta_size;
    zx::vmo ta_vmo;
    zx_status_t status = load_firmware(controller_->zxdev(),
                                       ta_path.data(),
                                       ta_vmo.reset_and_get_address(),
                                       &ta_size);

    if (status != ZX_OK) {
        if (status == ZX_ERR_NOT_FOUND) {
            zxlogf(ERROR, "optee: could not find trusted app %s!\n", ta_path.data());
            message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
        } else {
            zxlogf(ERROR, "optee: error loading trusted app %s!\n", ta_path.data());
            message->set_return_code(TEEC_ERROR_GENERIC);
        }

        return status;
    } else if (ta_size == 0) {
        zxlogf(ERROR, "optee: loaded trusted app %s with unexpected size!\n", ta_path.data());
        message->set_return_code(TEEC_ERROR_GENERIC);
        return status;
    }

    message->set_output_ta_size(static_cast<uint64_t>(ta_size));

    if (out_ta_mem == nullptr) {
        // TEE is querying the size of the TA
        message->set_return_code(TEEC_SUCCESS);
        return ZX_OK;
    } else if (ta_size > mem_usable_size) {
        // TEE provided too small of a memory region to write TA into
        message->set_return_code(TEEC_ERROR_SHORT_BUFFER);
        return ZX_OK;
    }

    // TODO(godtamit): in the future, we may want to register the memory as shared and use its VMO,
    // so we don't have to do a copy of the TA
    status = ta_vmo.read(out_ta_mem, 0, ta_size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee: failed to copy trusted app from VMO to shared memory!\n");
        message->set_return_code(TEEC_ERROR_GENERIC);
        return status;
    }

    if (ta_size < mem_usable_size) {
        // Clear out the rest of the memory after the TA
        uint8_t* ta_end = out_ta_mem + ta_size;
        memset(ta_end, 0, mem_usable_size - ta_size);
    }

    message->set_return_code(TEEC_SUCCESS);
    return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandAllocateMemory(AllocateMemoryRpcMessage* message) {
    ZX_DEBUG_ASSERT(message != nullptr);
    ZX_DEBUG_ASSERT(message->is_valid());

    if (message->memory_type() == SharedMemoryType::kGlobal) {
        zxlogf(ERROR, "optee: implementation currently does not support global shared memory!\n");
        message->set_return_code(TEEC_ERROR_NOT_SUPPORTED);
        return ZX_ERR_NOT_SUPPORTED;
    }

    size_t size = message->memory_size();
    zx_paddr_t paddr;
    uint64_t mem_id;
    zx_status_t status = AllocateSharedMemory(size, controller_->client_pool(), &paddr, &mem_id);
    if (status != ZX_OK) {
        if (status == ZX_ERR_NO_MEMORY) {
            message->set_return_code(TEEC_ERROR_OUT_OF_MEMORY);
        } else {
            message->set_return_code(TEEC_ERROR_GENERIC);
        }

        return status;
    }

    message->set_output_memory_size(size);
    message->set_output_buffer(paddr);
    message->set_output_memory_identifier(mem_id);

    message->set_return_code(TEEC_SUCCESS);

    return status;
}

zx_status_t OpteeClient::HandleRpcCommandFreeMemory(FreeMemoryRpcMessage* message) {
    ZX_DEBUG_ASSERT(message != nullptr);
    ZX_DEBUG_ASSERT(message->is_valid());

    if (message->memory_type() == SharedMemoryType::kGlobal) {
        zxlogf(ERROR, "optee: implementation currently does not support global shared memory!\n");
        message->set_return_code(TEEC_ERROR_NOT_SUPPORTED);
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = FreeSharedMemory(message->memory_identifier());
    if (status != ZX_OK) {
        if (status == ZX_ERR_NOT_FOUND) {
            message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
        } else {
            message->set_return_code(TEEC_ERROR_GENERIC);
        }

        return status;
    }

    message->set_return_code(TEEC_SUCCESS);
    return status;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystem(FileSystemRpcMessage* message) {
    ZX_DEBUG_ASSERT(message != nullptr);
    ZX_DEBUG_ASSERT(message->is_valid());

    switch (message->command()) {
    case FileSystemRpcMessage::FileSystemCommand::kOpenFile:
        zxlogf(ERROR, "optee: RPC command to open file recognized but not implemented\n");
        break;
    case FileSystemRpcMessage::FileSystemCommand::kCreateFile:
        zxlogf(ERROR, "optee: RPC command to create file recognized but not implemented\n");
        break;
    case FileSystemRpcMessage::FileSystemCommand::kCloseFile:
        zxlogf(ERROR, "optee: RPC command to close file recognized but not implemented\n");
        break;
    case FileSystemRpcMessage::FileSystemCommand::kReadFile:
        zxlogf(ERROR, "optee: RPC command to read file recognized but not implemented\n");
        break;
    case FileSystemRpcMessage::FileSystemCommand::kWriteFile:
        zxlogf(ERROR, "optee: RPC command to write file recognized but not implemented\n");
        break;
    case FileSystemRpcMessage::FileSystemCommand::kTruncateFile:
        zxlogf(ERROR, "optee: RPC command to truncate file recognized but not implemented\n");
        break;
    case FileSystemRpcMessage::FileSystemCommand::kRemoveFile:
        zxlogf(ERROR, "optee: RPC command to remove file recognized but not implemented\n");
        break;
    case FileSystemRpcMessage::FileSystemCommand::kRenameFile:
        zxlogf(ERROR, "optee: RPC command to rename file recognized but not implemented\n");
        break;
    case FileSystemRpcMessage::FileSystemCommand::kOpenDirectory:
        zxlogf(ERROR, "optee: RPC command to open directory recognized but not implemented\n");
        break;
    case FileSystemRpcMessage::FileSystemCommand::kCloseDirectory:
        zxlogf(ERROR, "optee: RPC command to close directory recognized but not implemented\n");
        break;
    case FileSystemRpcMessage::FileSystemCommand::kGetNextFileInDirectory:
        zxlogf(ERROR,
               "optee: RPC command to get next file in directory recognized but not implemented\n");
        break;
    }

    message->set_return_code(TEEC_ERROR_NOT_SUPPORTED);
    return ZX_OK;
}

} // namespace optee
