// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-message.h"

#include <ddk/debug.h>
#include <endian.h>
#include <limits>
#include <string.h>

namespace optee {

namespace {

// Converts a big endian UUID from a MessageParam value to a host endian TEEC_UUID.
// The fields of a UUID are stored in big endian in a MessageParam by the TEE and is thus why the
// parameter value cannot be directly reinterpreted as a UUID.
void ConvertMessageParamToUuid(const MessageParam::Value& src, TEEC_UUID* dst) {
    // Convert TEEC_UUID fields from big endian to host endian
    dst->timeLow = betoh32(src.uuid_big_endian.timeLow);
    dst->timeMid = betoh16(src.uuid_big_endian.timeMid);
    dst->timeHiAndVersion = betoh16(src.uuid_big_endian.timeHiAndVersion);

    // Because clockSeqAndNode is uint8_t, no need to convert endianness - just memcpy
    memcpy(dst->clockSeqAndNode,
           src.uuid_big_endian.clockSeqAndNode,
           sizeof(src.uuid_big_endian.clockSeqAndNode));
}

constexpr bool IsParameterInput(fuchsia_tee_Direction direction) {
    return (direction == fuchsia_tee_Direction_INPUT) ||
           (direction == fuchsia_tee_Direction_INOUT);
}

constexpr bool IsParameterOutput(fuchsia_tee_Direction direction) {
    return (direction == fuchsia_tee_Direction_OUTPUT) ||
           (direction == fuchsia_tee_Direction_INOUT);
}

} // namespace

bool Message::TryInitializeParameters(size_t starting_param_index,
                                      const fuchsia_tee_ParameterSet& parameter_set,
                                      SharedMemoryManager::ClientMemoryPool* temp_memory_pool) {
    // If we don't have any parameters to parse, then we can just skip this
    if (parameter_set.count == 0) {
        return true;
    }

    bool is_valid = true;
    for (size_t i = 0; i < parameter_set.count; i++) {
        MessageParam& optee_param = params()[starting_param_index + i];
        const fuchsia_tee_Parameter& zx_param = parameter_set.parameters[i];

        switch (zx_param.tag) {
        case fuchsia_tee_ParameterTag_empty:
            optee_param.attribute = MessageParam::kAttributeTypeNone;
            break;
        case fuchsia_tee_ParameterTag_value:
            is_valid = TryInitializeValue(zx_param.value, &optee_param);
            break;
        case fuchsia_tee_ParameterTag_buffer:
            is_valid = TryInitializeBuffer(zx_param.buffer, temp_memory_pool, &optee_param);
            break;
        default:
            return false;
        }

        if (!is_valid) {
            zxlogf(ERROR, "optee: failed to initialize parameters\n");
            return false;
        }
    }

    return true;
}

bool Message::TryInitializeValue(const fuchsia_tee_Value& value, MessageParam* out_param) {
    ZX_DEBUG_ASSERT(out_param != nullptr);

    switch (value.direction) {
    case fuchsia_tee_Direction_INPUT:
        out_param->attribute = MessageParam::kAttributeTypeValueInput;
        break;
    case fuchsia_tee_Direction_OUTPUT:
        out_param->attribute = MessageParam::kAttributeTypeValueOutput;
        break;
    case fuchsia_tee_Direction_INOUT:
        out_param->attribute = MessageParam::kAttributeTypeValueInOut;
        break;
    default:
        return false;
    }
    out_param->payload.value.generic.a = value.a;
    out_param->payload.value.generic.b = value.b;
    out_param->payload.value.generic.c = value.c;

    return true;
}

bool Message::TryInitializeBuffer(const fuchsia_tee_Buffer& buffer,
                                  SharedMemoryManager::ClientMemoryPool* temp_memory_pool,
                                  MessageParam* out_param) {
    ZX_DEBUG_ASSERT(temp_memory_pool != nullptr);
    ZX_DEBUG_ASSERT(out_param != nullptr);

    // Take ownership of the provided VMO. If we have to return early for any reason, this will
    // take care of closing the VMO.
    zx::vmo vmo(buffer.vmo);

    MessageParam::AttributeType attribute;
    switch (buffer.direction) {
    case fuchsia_tee_Direction_INPUT:
        attribute = MessageParam::kAttributeTypeTempMemInput;
        break;
    case fuchsia_tee_Direction_OUTPUT:
        attribute = MessageParam::kAttributeTypeTempMemOutput;
        break;
    case fuchsia_tee_Direction_INOUT:
        attribute = MessageParam::kAttributeTypeTempMemInOut;
        break;
    default:
        return false;
    }

    // If an invalid VMO was provided, but the buffer is only an output, this is just a size check.
    if (!vmo.is_valid()) {
        if (IsParameterInput(buffer.direction)) {
            return false;
        } else {
            // No need to allocate a temporary buffer from the shared memory pool,
            out_param->attribute = attribute;
            out_param->payload.temporary_memory.buffer = 0;
            out_param->payload.temporary_memory.size = buffer.size;
            out_param->payload.temporary_memory.shared_memory_reference = 0;
            return true;
        }
    }

    // For most buffer types, we must allocate a temporary shared memory buffer within the physical
    // pool to share it with the TEE. We'll attach them to the Message object so that they can be
    // looked up upon return from TEE and to tie the lifetimes of the Message and the temporary
    // shared memory together.
    SharedMemoryPtr shared_mem;
    if (temp_memory_pool->Allocate(buffer.size, &shared_mem) != ZX_OK) {
        zxlogf(ERROR, "optee: Failed to allocate temporary shared memory (%" PRIu64 ")\n",
               buffer.size);
        return false;
    }

    uint64_t paddr = static_cast<uint64_t>(shared_mem->paddr());

    TemporarySharedMemory temp_shared_mem{std::move(vmo), buffer.offset, buffer.size,
                                          std::move(shared_mem)};

    // Input buffers should be copied into the shared memory buffer. Output only buffers can skip
    // this step.
    if (IsParameterInput(buffer.direction)) {
        zx_status_t status = temp_shared_mem.SyncToSharedMemory();
        if (status != ZX_OK) {
            zxlogf(ERROR, "optee: shared memory sync failed (%d)\n", status);
            return false;
        }
    }

    allocated_temp_memory_.push_back(std::move(temp_shared_mem));
    uint64_t index = static_cast<uint64_t>(allocated_temp_memory_.size()) - 1;

    out_param->attribute = attribute;
    out_param->payload.temporary_memory.buffer = paddr;
    out_param->payload.temporary_memory.size = buffer.size;
    out_param->payload.temporary_memory.shared_memory_reference = index;
    return true;
}

zx_status_t
Message::CreateOutputParameterSet(size_t starting_param_index,
                                  fuchsia_tee_ParameterSet* out_parameter_set) {
    ZX_DEBUG_ASSERT(out_parameter_set != nullptr);

    // Use a temporary parameter set to avoid populating the output until we're sure it's valid.
    fuchsia_tee_ParameterSet parameter_set = {};

    // Below code assumes that we can fit all of the parameters from optee into the FIDL parameter
    // set. This static assert ensures that the FIDL parameter set can always fit the number of
    // parameters into the count.
    static_assert(fbl::count_of(parameter_set.parameters) <=
                      std::numeric_limits<decltype(parameter_set.count)>::max(),
                  "The size of the tee parameter set has outgrown the count");

    if (header()->num_params < starting_param_index) {
        zxlogf(ERROR, "optee: Message contained fewer parameters (%" PRIu32 ") than required %zd\n",
               header()->num_params, starting_param_index);
        return ZX_ERR_INVALID_ARGS;
    }

    // Ensure that the number of parameters returned by the TEE does not exceed the parameter set
    // array of parameters.
    const size_t count = header()->num_params - starting_param_index;
    if (count > fbl::count_of(parameter_set.parameters)) {
        zxlogf(ERROR, "optee: Message contained more parameters (%zd) than allowed\n", count);
        return ZX_ERR_INVALID_ARGS;
    }

    parameter_set.count = static_cast<uint16_t>(count);
    for (size_t i = starting_param_index; i < header()->num_params; i++) {
        const MessageParam& optee_param = params()[i];
        fuchsia_tee_Parameter& zx_param = parameter_set.parameters[i];

        switch (optee_param.attribute) {
        case MessageParam::kAttributeTypeNone:
            zx_param.tag = fuchsia_tee_ParameterTag_empty;
            zx_param.empty = {};
            break;
        case MessageParam::kAttributeTypeValueInput:
        case MessageParam::kAttributeTypeValueOutput:
        case MessageParam::kAttributeTypeValueInOut:
            zx_param.tag = fuchsia_tee_ParameterTag_value;
            zx_param.value = CreateOutputValueParameter(optee_param);
            break;
        case MessageParam::kAttributeTypeTempMemInput:
        case MessageParam::kAttributeTypeTempMemOutput:
        case MessageParam::kAttributeTypeTempMemInOut:
            zx_param.tag = fuchsia_tee_ParameterTag_buffer;
            if (zx_status_t status = CreateOutputBufferParameter(optee_param, &zx_param.buffer);
                status != ZX_OK) {
                return status;
            }
            break;
        case MessageParam::kAttributeTypeRegMemInput:
        case MessageParam::kAttributeTypeRegMemOutput:
        case MessageParam::kAttributeTypeRegMemInOut:
        default:
            break;
        }
    }

    *out_parameter_set = parameter_set;
    return ZX_OK;
}

fuchsia_tee_Value Message::CreateOutputValueParameter(const MessageParam& optee_param) {
    fuchsia_tee_Value zx_value = {};

    switch (optee_param.attribute) {
    case MessageParam::kAttributeTypeValueInput:
        zx_value.direction = fuchsia_tee_Direction_INPUT;
        break;
    case MessageParam::kAttributeTypeValueOutput:
        zx_value.direction = fuchsia_tee_Direction_OUTPUT;
        break;
    case MessageParam::kAttributeTypeValueInOut:
        zx_value.direction = fuchsia_tee_Direction_INOUT;
        break;
    default:
        ZX_PANIC("Invalid OP-TEE attribute specified\n");
    }

    const MessageParam::Value& optee_value = optee_param.payload.value;

    if (IsParameterOutput(zx_value.direction)) {
        zx_value.a = optee_value.generic.a;
        zx_value.b = optee_value.generic.b;
        zx_value.c = optee_value.generic.c;
    }
    return zx_value;
}

zx_status_t Message::CreateOutputBufferParameter(const MessageParam& optee_param,
                                                 fuchsia_tee_Buffer* out_buffer) {
    ZX_DEBUG_ASSERT(out_buffer != nullptr);

    // Use a temporary buffer to avoid populating the output until we're sure it's valid.
    fuchsia_tee_Buffer zx_buffer = {};

    switch (optee_param.attribute) {
    case MessageParam::kAttributeTypeTempMemInput:
        zx_buffer.direction = fuchsia_tee_Direction_INPUT;
        break;
    case MessageParam::kAttributeTypeTempMemOutput:
        zx_buffer.direction = fuchsia_tee_Direction_OUTPUT;
        break;
    case MessageParam::kAttributeTypeTempMemInOut:
        zx_buffer.direction = fuchsia_tee_Direction_INOUT;
        break;
    default:
        ZX_PANIC("Invalid OP-TEE attribute specified\n");
    }

    const MessageParam::TemporaryMemory& optee_temp_mem = optee_param.payload.temporary_memory;

    zx_buffer.size = optee_temp_mem.size;

    if (optee_temp_mem.buffer == 0) {
        // If there was no buffer and this was just a size check, just return the size.
        *out_buffer = zx_buffer;
        return ZX_OK;
    }

    if (optee_temp_mem.shared_memory_reference >= allocated_temp_memory_.size()) {
        zxlogf(ERROR, "optee: TEE returned an invalid shared_memory_reference (%" PRIu64 ")\n",
               optee_temp_mem.shared_memory_reference);
        return ZX_ERR_INVALID_ARGS;
    }

    auto& temp_shared_memory = allocated_temp_memory_[optee_temp_mem.shared_memory_reference];

    if (!temp_shared_memory.is_valid()) {
        zxlogf(ERROR, "optee: Invalid TemporarySharedMemory attempted to be used\n");
        return ZX_ERR_INVALID_ARGS;
    }

    // For output buffers, we need to sync the shared memory buffer back to the VMO. It's possible
    // that the returned size is smaller or larger than the originally provided buffer.
    if (IsParameterOutput(zx_buffer.direction)) {
        if (zx_status_t status = temp_shared_memory.SyncToVmo(zx_buffer.size); status != ZX_OK) {
            zxlogf(ERROR, "optee: SharedMemory writeback to vmo failed (%d)\n", status);
            return status;
        }
    }

    zx_buffer.vmo = temp_shared_memory.ReleaseVmo();
    zx_buffer.offset = temp_shared_memory.vmo_offset();

    *out_buffer = zx_buffer;

    return ZX_OK;
}

Message::TemporarySharedMemory::TemporarySharedMemory(zx::vmo vmo, uint64_t vmo_offset, size_t size,
                                                      fbl::unique_ptr<SharedMemory> shared_memory)
    : vmo_(std::move(vmo)), vmo_offset_(vmo_offset), size_(size),
      shared_memory_(std::move(shared_memory)) {}

zx_status_t Message::TemporarySharedMemory::SyncToSharedMemory() {
    ZX_DEBUG_ASSERT(is_valid());
    return vmo_.read(reinterpret_cast<void*>(shared_memory_->vaddr()), vmo_offset_, size_);
}

zx_status_t Message::TemporarySharedMemory::SyncToVmo(size_t actual_size) {
    ZX_DEBUG_ASSERT(is_valid());
    // If the actual size of the data is larger than the size of the vmo, then we should skip the
    // actual write. This is a valid scenario and the Trusted World will be responsible for
    // providing the short buffer error code in it's result.
    if (actual_size > size_) {
        return ZX_OK;
    }
    return vmo_.write(reinterpret_cast<void*>(shared_memory_->vaddr()), vmo_offset_, actual_size);
}

zx_handle_t Message::TemporarySharedMemory::ReleaseVmo() {
    return vmo_.release();
}

OpenSessionMessage::OpenSessionMessage(SharedMemoryManager::DriverMemoryPool* message_pool,
                                       SharedMemoryManager::ClientMemoryPool* temp_memory_pool,
                                       const Uuid& trusted_app,
                                       const fuchsia_tee_ParameterSet& parameter_set) {
    ZX_DEBUG_ASSERT(message_pool != nullptr);

    const size_t num_params = parameter_set.count + kNumFixedOpenSessionParams;
    ZX_DEBUG_ASSERT(num_params <= std::numeric_limits<uint32_t>::max());

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

    // If we fail to initialize the parameters, then null out the message memory.
    if (!TryInitializeParameters(kNumFixedOpenSessionParams, parameter_set, temp_memory_pool)) {
        memory_ = nullptr;
    }
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
                                           SharedMemoryManager::ClientMemoryPool* temp_memory_pool,
                                           uint32_t session_id, uint32_t command_id,
                                           const fuchsia_tee_ParameterSet& parameter_set) {
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

    if (!TryInitializeParameters(0, parameter_set, temp_memory_pool)) {
        memory_ = nullptr;
    }
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
        mem_paddr_ = static_cast<zx_paddr_t>(temp_mem.buffer);
        out_ta_size_ = &temp_mem.size;
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

    return true;
}

bool GetTimeRpcMessage::TryInitializeMembers() {
    if (header()->num_params != kNumParams) {
        zxlogf(ERROR,
               "optee: RPC command to get current time received unexpected number of parameters!"
               "\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return false;
    }

    // Parse the output time parameter
    MessageParam& time_param = params()[kTimeParamIndex];
    if (time_param.attribute != MessageParam::kAttributeTypeValueOutput) {
        zxlogf(ERROR,
               "optee: RPC command to get current time received unexpected first parameter!\n");
        set_return_origin(TEEC_ORIGIN_COMMS);
        set_return_code(TEEC_ERROR_BAD_PARAMETERS);
    }

    out_secs_ = &time_param.payload.value.get_time_specs.seconds;
    out_nanosecs_ = &time_param.payload.value.get_time_specs.nanoseconds;

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
