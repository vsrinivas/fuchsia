// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secmem_client_session.h"

#include <zircon/assert.h>

#include <cinttypes>

#include <fbl/algorithm.h>

#include "macros.h"

// TODO(dustingreen): We could potentially share code with aml-securemem for this class - currently
// we don't mainly because of logging differences.

// UUID of the TA.
constexpr TEEC_UUID kSecmemUuid = {
    0x2c1a33c0, 0x44cc, 0x11e5, {0xbc, 0x3b, 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};

// Some secmem-specific marshaling definitions.

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

// Defined by secmem TA.
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
    result = TEEC_OpenSession(context_, &session_.value(), &kSecmemUuid, TEEC_LOGIN_PUBLIC, NULL,
                              NULL, &return_origin);
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

zx_status_t SecmemClientSession::GetVp9HeaderSize(zx_paddr_t vp9_paddr, uint32_t before_size,
                                                  uint32_t max_after_size, uint32_t* after_size) {
  ZX_DEBUG_ASSERT(after_size);
  if (vp9_paddr > std::numeric_limits<uint32_t>::max()) {
    LOG(ERROR, "vp9_paddr > 0xFFFFFFFF");
    return ZX_ERR_INVALID_ARGS;
  }
  if (vp9_paddr + before_size < vp9_paddr) {
    LOG(ERROR, "vp9_paddr + before_size overflow");
    return ZX_ERR_INVALID_ARGS;
  }
  if ((vp9_paddr & ZX_PAGE_MASK) != 0) {
    // If the intra-page offset is exactly 16, that has special meaning to the TA, so instead of
    // risking that we randomly encounter that case later, require page alignment.
    LOG(ERROR, "vp9_paddr must be page-aligned for now");
    return ZX_ERR_INVALID_ARGS;
  }
  if (max_after_size < before_size) {
    LOG(ERROR, "max_after_size < before_size");
    return ZX_ERR_INVALID_ARGS;
  }
  constexpr uint32_t kMaxFramesPerSuperframe = 8;
  constexpr uint32_t kHeaderSizePerFrame = 16;
  if (max_after_size - before_size < kMaxFramesPerSuperframe * kHeaderSizePerFrame) {
    LOG(ERROR, "max_after_size - before_size < kMaxFramesPerSuperframe * kHeaderSizePerFrame");
    return ZX_ERR_INVALID_ARGS;
  }
  size_t input_offset = 0;
  size_t output_offset = 0;
  PackUint32Parameter(kSecmemCommandIdGetVp9HeaderSize, &input_offset);
  PackUint32Parameter(static_cast<uint32_t>(vp9_paddr), &input_offset);
  PackUint32Parameter(before_size, &input_offset);
  TEEC_Result tee_status = InvokeSecmemCommand(kSecmemCommandIdGetVp9HeaderSize, input_offset);
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "kSecmemCommandIdGetVp9HeaderSize failed - tee_status: %" PRIx32, tee_status);
    return ZX_ERR_INTERNAL;
  }
  uint32_t header_size = 0;
  if (!UnpackUint32Parameter(&header_size, &output_offset)) {
    LOG(ERROR, "UnpackUint32Parameter() after kSecmemCommandIdGetVp9HeaderSize failed");
    return ZX_ERR_INTERNAL;
  }
  *after_size = before_size + header_size;
  return ZX_OK;
}
