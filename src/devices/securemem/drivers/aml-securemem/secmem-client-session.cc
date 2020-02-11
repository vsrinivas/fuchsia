// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secmem-client-session.h"

#include <zircon/assert.h>

#include <cinttypes>

#include <fbl/algorithm.h>

#include "log.h"

// Randomly-generated UUID for the TA.
constexpr TEEC_UUID kSecmemUuid = {
    0x2c1a33c0, 0x44cc, 0x11e5, {0xbc, 0x3b, 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};

enum TeeParamType {
  kTeeParamTypeBuffer,
  kTeeParamTypeUint32,
  kTeeParamTypeUint64,
  kTeeParamTypePvoid,
};

struct TeeCommandParam {
  TeeParamType type;
  union {
    struct {
      uint32_t buffer_length;
      uint32_t pbuf[1];
    } buf;         // kTeeParamTypeBuffer
    uint32_t u32;  // kTeeParamTypeUint32
  } param;
};

// Defined by the TA.
enum SecmemCommandIds {
  kSecmemCommandIdAllocateSecureMemory = 101,
  kSecmemCommandIdProtectMemory = 104,
  kSecmemCommandIdUnprotectMemory = 105,
  kSecmemCommandIdGetPadding = 107,
  kSecmemCommandIdGetVp9HeaderSize = 108,
  kSecmemCommandIdGetMemSize = 110,
};

static constexpr uint32_t kParameterAlignment = 32u;
static constexpr uint32_t kParameterBufferSize = 4 * 1024u;
static constexpr uint32_t kParameterBufferPadding = 64u;

SecmemClientSession::SecmemClientSession(TEEC_Context* context) : context_(context) {
  // nothing else to do here
}

SecmemClientSession::~SecmemClientSession() {
  if (parameter_buffer_) {
    TEEC_ReleaseSharedMemory(&parameter_buffer_.value());
    parameter_buffer_.reset();
  }
  if (session_) {
    TEEC_CloseSession(&session_.value());
    session_.reset();
  }
}

zx_status_t SecmemClientSession::Init() {
  uint32_t return_origin;

  session_.emplace();
  TEEC_Result result;
  // Crashes happen about 10% of the time, so 10 retries should greatly reduce the probability.
  constexpr uint32_t kRetryCount = 10;
  for (uint32_t i = 0; i < kRetryCount; i++) {
    result = TEEC_OpenSession(context_, &session_.value(), &kSecmemUuid,
                                          TEEC_LOGIN_PUBLIC, NULL, NULL, &return_origin);
    if (result == TEEC_SUCCESS) {
      break;
    } else {
      // fxb/37747 - The TA sometimes crashes when opening a session on sherlock. The crashes seem
      // uncorrelated, so retrying works.
      LOG(ERROR, "TEEC_OpenSession failed - Retrying - result: %" PRIx32 " origin: %" PRIu32,
        result, return_origin);
    }
  }
  if (result != TEEC_SUCCESS) {
    session_.reset();
    LOG(ERROR,
        "TEEC_OpenSession failed - Maybe bootloader version is incorrect - "
        "result: %" PRIu32 " origin: %" PRIu32,
        result, return_origin);
    return ZX_ERR_INVALID_ARGS;
  }

  parameter_buffer_.emplace();
  parameter_buffer_->size = kParameterBufferSize;
  parameter_buffer_->flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
  result = TEEC_AllocateSharedMemory(context_, &parameter_buffer_.value());
  if (result != TEEC_SUCCESS) {
    parameter_buffer_.reset();
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void SecmemClientSession::PackUint32Parameter(uint32_t value, size_t* offset_in_out) {
  ZX_DEBUG_ASSERT(offset_in_out);
  size_t offset = *offset_in_out;
  ZX_ASSERT(offset + sizeof(TeeCommandParam) <= parameter_buffer_->size);
  TeeCommandParam p;
  p.type = kTeeParamTypeUint32;
  p.param.u32 = value;
  auto buffer_address = reinterpret_cast<uint8_t*>(parameter_buffer_->buffer);
  memcpy(buffer_address + offset, &p, sizeof(TeeCommandParam));
  offset += sizeof(TeeCommandParam);
  *offset_in_out = fbl::round_up(offset, kParameterAlignment);
}

TEEC_Result SecmemClientSession::InvokeSecmemCommand(uint32_t command, size_t length) {
  TEEC_Operation operation = {};
  operation.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_PARTIAL_INOUT,  // Shared memory buffer
                                          TEEC_NONE, TEEC_NONE,
                                          TEEC_VALUE_OUTPUT);  // Command result
  operation.params[0].memref.parent = &parameter_buffer_.value();
  operation.params[0].memref.offset = 0;
  operation.params[0].memref.size = length + kParameterBufferPadding;
  TEEC_Result res = TEEC_InvokeCommand(&session_.value(), command, &operation, nullptr);
  if (res != TEEC_SUCCESS) {
    return res;
  }
  return static_cast<TEEC_Result>(operation.params[3].value.a);
}

bool SecmemClientSession::UnpackUint32Parameter(uint32_t* value, size_t* offset_in_out) {
  ZX_DEBUG_ASSERT(value);
  ZX_DEBUG_ASSERT(offset_in_out);
  size_t offset = *offset_in_out;
  ZX_ASSERT(offset + sizeof(TeeCommandParam) <= parameter_buffer_->size);
  auto buffer_address = reinterpret_cast<uint8_t*>(parameter_buffer_->buffer);
  TeeCommandParam p;
  memcpy(&p, buffer_address + offset, sizeof(TeeCommandParam));
  if (p.type != kTeeParamTypeUint32) {
    LOG(ERROR, "p.type != kTeeParamTypeUint32");
    return false;
  }
  *value = p.param.u32;
  offset += sizeof(TeeCommandParam);
  *offset_in_out = fbl::round_up(offset, kParameterAlignment);
  return true;
}

TEEC_Result SecmemClientSession::ProtectMemoryRange(uint32_t start, uint32_t length,
                                                    bool is_enable) {
  size_t input_offset = 0;

  PackUint32Parameter(kSecmemCommandIdProtectMemory, &input_offset);

  uint32_t enable_protection = is_enable ? 1 : 0;
  PackUint32Parameter(enable_protection, &input_offset);

  // must be 1-4 inclusive
  constexpr uint32_t kRegionNum = 1;
  PackUint32Parameter(kRegionNum, &input_offset);

  PackUint32Parameter(start, &input_offset);
  PackUint32Parameter(length, &input_offset);

  return InvokeSecmemCommand(kSecmemCommandIdProtectMemory, input_offset);
}

TEEC_Result SecmemClientSession::AllocateSecureMemory(uint32_t* start, uint32_t* length) {
  // First, ask secmem TA for the max size of VDEC, then allocate that size.

  // kSecmemCommandIdGetMemSize command first
  size_t input_offset = 0;
  size_t output_offset = 0;
  PackUint32Parameter(kSecmemCommandIdGetMemSize, &input_offset);
  TEEC_Result tee_status = InvokeSecmemCommand(kSecmemCommandIdGetMemSize, input_offset);
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "kSecmemCommandIdGetMemSize failed - tee_status: %" PRIu32, tee_status);
    return tee_status;
  }
  uint32_t max_vdec_size = 0;
  if (!UnpackUint32Parameter(&max_vdec_size, &output_offset)) {
    LOG(ERROR,
        "UnpackUint32Parameter() after kSecmemCommandIdGetMemSize failed - returning "
        "TEEC_ERROR_COMMUNICATION");
    return TEEC_ERROR_COMMUNICATION;
  }

  // Reset for new command: kSecmemCommandIdAllocateSecureMemory.
  input_offset = 0;
  output_offset = 0;

  PackUint32Parameter(kSecmemCommandIdAllocateSecureMemory, &input_offset);

  // ignored
  constexpr uint32_t kDbgLevel = 0;
  PackUint32Parameter(kDbgLevel, &input_offset);

  // We can pass false for is_vp9, even if later when we do
  // kSecmemCommandIdGetVp9HeaderSize we start at exactly one AMLV header length
  // into a page to avoid one frame/sub-frame being copied.
  constexpr uint32_t kIsVp9 = false;  // 0
  PackUint32Parameter(kIsVp9, &input_offset);

  PackUint32Parameter(max_vdec_size, &input_offset);

  tee_status = InvokeSecmemCommand(kSecmemCommandIdAllocateSecureMemory, input_offset);
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "kSecmemCommandIdAllocateSecureMemory failed - tee_status: %" PRIu32, tee_status);
    return tee_status;
  }
  uint32_t vdec_paddr = 0;
  if (!UnpackUint32Parameter(&vdec_paddr, &output_offset)) {
    LOG(ERROR,
        "UnpackUint32Parameter() after kSecmemCommandIdAllocateSecureMemory failed - "
        "returning TEEC_ERROR_COMMUNICATION");
    return TEEC_ERROR_COMMUNICATION;
  }

  *start = vdec_paddr;
  *length = max_vdec_size;

  return TEEC_SUCCESS;
}
