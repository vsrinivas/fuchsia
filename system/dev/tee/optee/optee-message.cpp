// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-message.h"

#include <ddk/debug.h>
#include <endian.h>
#include <fbl/limits.h>
#include <string.h>

namespace {

// Converts a big endian UUID from a MessageParam value to a host endian TEEC_UUID.
// The fields of a UUID are stored in big endian in a MessageParam by the TEE and is thus why the
// parameter value cannot be directly reinterpreted as a UUID.
void ConvertMessageParamToUuid(const optee::MessageParam::Value& src, TEEC_UUID* dst) {
    // Convert TEEC_UUID fields from big endian to host endian
    dst->timeLow = betoh32(src.uuid_big_endian.timeLow);
    dst->timeMid = betoh16(src.uuid_big_endian.timeMid);
    dst->timeHiAndVersion = betoh16(src.uuid_big_endian.timeHiAndVersion);

    // Because clockSeqAndNode is uint8_t, no need to convert endianness - just memcpy
    memcpy(dst->clockSeqAndNode,
           src.uuid_big_endian.clockSeqAndNode,
           sizeof(src.uuid_big_endian.clockSeqAndNode));
}

}; // namespace

namespace optee {

OpenSessionMessage::OpenSessionMessage(SharedMemoryManager::DriverMemoryPool* message_pool,
                                       const Uuid& trusted_app,
                                       const zircon_tee_ParameterSet& parameter_set) {
    ZX_DEBUG_ASSERT(message_pool != nullptr);

    const size_t num_params = parameter_set.count + kNumFixedOpenSessionParams;
    ZX_DEBUG_ASSERT(num_params <= fbl::numeric_limits<uint32_t>::max());

    zx_status_t status = message_pool->Allocate(CalculateSize(num_params), &memory_);

    if (status != ZX_OK) {
        memory_ = nullptr;
        return;
    }

    header()->command = Command::kOpenSession;
    header()->cancel_id = 0;
    header()->num_params = static_cast<uint32_t>(num_params);

    MessageParam& trusted_app_param = params()[kTrustedAppParamIndex];
    MessageParam& client_app_param = params()[kClientAppParamIndex];

    trusted_app_param.attribute = MessageParam::kAttributeTypeMeta |
                                  MessageParam::kAttributeTypeValueInput;
    trusted_app.ToUint64Pair(&trusted_app_param.payload.value.generic.a,
                             &trusted_app_param.payload.value.generic.b);

    client_app_param.attribute = MessageParam::kAttributeTypeMeta |
                                 MessageParam::kAttributeTypeValueInput;
    // Not really any need to provide client app uuid, so just fill in with 0s
    client_app_param.payload.value.generic.a = 0;
    client_app_param.payload.value.generic.b = 0;
    client_app_param.payload.value.generic.c = TEEC_LOGIN_PUBLIC;
}

CloseSessionMessage::CloseSessionMessage(SharedMemoryManager::DriverMemoryPool* message_pool,
                                         uint32_t session_id) {
    ZX_DEBUG_ASSERT(message_pool != nullptr);

    zx_status_t status = message_pool->Allocate(CalculateSize(kNumParams), &memory_);

    if (status != ZX_OK) {
        memory_ = nullptr;
        return;
    }

    header()->command = Command::kCloseSession;
    header()->num_params = static_cast<uint32_t>(kNumParams);
    header()->session_id = session_id;
}

InvokeCommandMessage::InvokeCommandMessage(SharedMemoryManager::DriverMemoryPool* message_pool,
                                           uint32_t session_id,
                                           uint32_t command_id,
                                           const zircon_tee_ParameterSet& parameter_set) {
    ZX_DEBUG_ASSERT(message_pool != nullptr);

    zx_status_t status = message_pool->Allocate(CalculateSize(parameter_set.count), &memory_);

    if (status != ZX_OK) {
        memory_ = nullptr;
        return;
    }

    header()->command = Command::kInvokeCommand;
    header()->session_id = session_id;
    header()->app_function = command_id;
    header()->cancel_id = 0;
    header()->num_params = parameter_set.count;
}

bool RpcMessage::TryInitializeMembers() {
    size_t memory_size = memory_->size();
    if (memory_size < sizeof(MessageHeader)) {
        zxlogf(ERROR,
               "optee: shared memory region passed into RPC command could not be parsed into a "
               "valid message!\n");
        return false;
    }

    if (memory_size < CalculateSize(header()->num_params)) {
        zxlogf(ERROR,
               "optee: shared memory region passed into RPC command could not be parsed into a "
               "valid message!\n");
        // Can at least write error code to the header since that has been checked already
        header()->return_origin = TEEC_ORIGIN_COMMS;
        header()->return_code = TEEC_ERROR_BAD_PARAMETERS;
        return false;
    }

    return true;
}

bool LoadTaRpcMessage::TryInitializeMembers() {
    if (header()->num_params != kNumParams) {
        zxlogf(ERROR,
               "optee: RPC command to load trusted app received unexpected number of parameters!"
               "\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    // Parse the UUID of the trusted application from the parameters
    MessageParam& uuid_param = params()[kUuidParamIndex];
    switch (uuid_param.attribute) {
    case MessageParam::kAttributeTypeValueInput:
    case MessageParam::kAttributeTypeValueInOut:
        ConvertMessageParamToUuid(uuid_param.payload.value, &ta_uuid_);
        break;
    default:
        zxlogf(ERROR,
               "optee: RPC command to load trusted app received unexpected first parameter!\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    // Parse where in memory to write the trusted application
    MessageParam& memory_reference_param = params()[kMemoryReferenceParamIndex];
    switch (memory_reference_param.attribute) {
    case MessageParam::kAttributeTypeTempMemOutput:
    case MessageParam::kAttributeTypeTempMemInOut: {
        MessageParam::TemporaryMemory& temp_mem = memory_reference_param.payload.temporary_memory;
        mem_id_ = temp_mem.shared_memory_reference;
        mem_size_ = static_cast<size_t>(temp_mem.size);
        out_ta_size_ = &temp_mem.size;
        // Temporary Memory References are owned by the TEE/TA and used only for the duration of
        // this operation. Thus, it is sized exactly for the operation being performed and does not
        // have an offset.
        mem_offset_ = 0;
        break;
    }
    case MessageParam::kAttributeTypeRegMemOutput:
    case MessageParam::kAttributeTypeRegMemInOut:
        zxlogf(ERROR,
               "optee: received unsupported registered memory parameter!\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_NOT_IMPLEMENTED);
        return false;
    default:
        zxlogf(ERROR,
               "optee: RPC command to load trusted app received unexpected second parameter!\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    if (mem_offset_ >= mem_size_ && mem_offset_ > 0) {
        zxlogf(ERROR, "optee: RPC command received a memory offset out of bounds!\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    return true;
}

bool AllocateMemoryRpcMessage::TryInitializeMembers() {
    if (header()->num_params != kNumParams) {
        zxlogf(ERROR,
               "optee: RPC command to allocate shared memory received unexpected number of "
               "parameters!\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    // Parse the memory specifications parameter
    MessageParam& value_param = params()[kMemorySpecsParamIndex];
    if (value_param.attribute != MessageParam::kAttributeTypeValueInput) {
        zxlogf(ERROR,
               "optee: RPC command to allocate shared memory received unexpected first parameter!"
               "\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    auto& memory_specs_param = value_param.payload.value.allocate_memory_specs;

    switch (memory_specs_param.memory_type) {
    case SharedMemoryType::kApplication:
    case SharedMemoryType::kKernel:
    case SharedMemoryType::kGlobal:
        memory_type_ = static_cast<SharedMemoryType>(memory_specs_param.memory_type);
        break;
    default:
        zxlogf(ERROR,
               "optee: received unknown memory type %" PRIu64 " to allocate\n",
               memory_specs_param.memory_type);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    memory_size_ = static_cast<size_t>(memory_specs_param.memory_size);

    // Set up the memory output parameter
    MessageParam& out_param = params()[kOutputTemporaryMemoryParamIndex];
    out_param.attribute = MessageParam::AttributeType::kAttributeTypeTempMemOutput;
    MessageParam::TemporaryMemory& out_temp_mem_param = out_param.payload.temporary_memory;
    out_memory_size_ = &out_temp_mem_param.size;
    out_memory_buffer_ = &out_temp_mem_param.buffer;
    out_memory_id_ = &out_temp_mem_param.shared_memory_reference;

    return true;
}

bool FreeMemoryRpcMessage::TryInitializeMembers() {
    if (header()->num_params != kNumParams) {
        zxlogf(ERROR,
               "optee: RPC command to free shared memory received unexpected number of parameters!"
               "\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    // Parse the memory specifications parameter
    MessageParam& value_param = params()[kMemorySpecsParamIndex];
    if (value_param.attribute != MessageParam::kAttributeTypeValueInput) {
        zxlogf(ERROR,
               "optee: RPC command to free shared memory received unexpected first parameter!"
               "\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    auto& memory_specs_param = value_param.payload.value.free_memory_specs;

    switch (memory_specs_param.memory_type) {
    case SharedMemoryType::kApplication:
    case SharedMemoryType::kKernel:
    case SharedMemoryType::kGlobal:
        memory_type_ = static_cast<SharedMemoryType>(memory_specs_param.memory_type);
        break;
    default:
        zxlogf(ERROR,
               "optee: received unknown memory type %" PRIu64 " to free\n",
               memory_specs_param.memory_type);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    memory_id_ = memory_specs_param.memory_id;
    return true;
}

bool FileSystemRpcMessage::TryInitializeMembers() {
    if (header()->num_params < kMinNumParams) {
        zxlogf(ERROR,
               "optee: RPC command to access file system received unexpected number of parameters!"
               "\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    // Parse the file system command parameter
    MessageParam& command_param = params()[kFileSystemCommandParamIndex];
    switch (command_param.attribute) {
    case MessageParam::kAttributeTypeValueInput:
    case MessageParam::kAttributeTypeValueInOut:
        break;
    default:
        zxlogf(ERROR,
               "optee: RPC command to access file system received unexpected first parameter!\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    uint64_t command_num = command_param.payload.value.file_system_command.command_number;
    if (command_num >= kNumFileSystemCommands) {
        zxlogf(ERROR, "optee: received unknown file system command %" PRIu64 "\n", command_num);
        set_return_code(TEEC_ERROR_NOT_SUPPORTED);
        return false;
    }

    fs_command_ = static_cast<FileSystemCommand>(command_num);
    return true;
}

} // namespace optee
