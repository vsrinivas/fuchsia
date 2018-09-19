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

OpenSessionMessage::OpenSessionMessage(SharedMemoryManager::DriverMemoryPool* pool,
                                       const UuidView& trusted_app,
                                       const UuidView& client_app,
                                       uint32_t client_login,
                                       uint32_t cancel_id,
                                       const fbl::Array<MessageParam>& msg_params) {
    const size_t num_params = msg_params.size() + kNumFixedOpenSessionParams;
    ZX_DEBUG_ASSERT(num_params <= fbl::numeric_limits<uint32_t>::max());

    // Allocate from pool
    pool->Allocate(CalculateSize(num_params), &memory_);

    header()->command = Command::kOpenSession;
    header()->cancel_id = cancel_id;
    header()->num_params = static_cast<uint32_t>(num_params);

    auto current_param = params().begin();

    // Param 0 is the trusted app UUID
    current_param->attribute = MessageParam::kAttributeTypeMeta |
                               MessageParam::kAttributeTypeValueInput;
    trusted_app.ToUint64Pair(&current_param->payload.value.generic.a,
                             &current_param->payload.value.generic.b);
    current_param++;

    // Param 1 is the client app UUID and login
    current_param->attribute = MessageParam::kAttributeTypeMeta |
                               MessageParam::kAttributeTypeValueInput;
    client_app.ToUint64Pair(&current_param->payload.value.generic.a,
                            &current_param->payload.value.generic.b);
    current_param->payload.value.generic.c = client_login;
    current_param++;

    // Copy input params in
    for (const auto& param : msg_params) {
        *current_param = param;
        current_param++;
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
    case MessageParam::kAttributeTypeTempMemInOut:
        mem_id_ = memory_reference_param.payload.temporary_memory.shared_memory_reference;
        mem_size_ = memory_reference_param.payload.temporary_memory.size;
        out_ta_size_ = &memory_reference_param.payload.temporary_memory.size;
        // Temporary Memory References are owned by the TEE/TA and used only for the duration of
        // this operation. Thus, it is sized exactly for the operation being performed and does not
        // have an offset.
        mem_offset_ = 0;
        break;
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

} // namespace optee
