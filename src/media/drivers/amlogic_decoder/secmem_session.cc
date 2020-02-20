// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secmem_session.h"

#include <zircon/assert.h>

#include <algorithm>
#include <iterator>

#include <fbl/algorithm.h>
#include <safemath/checked_math.h>

#include "macros.h"

// TODO(dustingreen): We could potentially share code with aml-securemem for this class - currently
// we don't mainly because of logging differences.

namespace {

// UUID of the TA.
const fuchsia::tee::Uuid kSecmemUuid = {
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

zx::vmo CreateVmo(uint64_t size) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, /*options=*/0, &vmo);
  ZX_ASSERT(status == ZX_OK);

  return vmo;
}

fit::result<fuchsia::tee::Buffer> CreateCommandBuffer(const std::vector<uint8_t>& contents) {
  zx::vmo vmo = CreateVmo(static_cast<uint64_t>(contents.size()));

  zx_status_t status = vmo.write(contents.data(), /*offset=*/0, contents.size());
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to write to command buffer VMO - status: %d", status);
    return fit::error();
  }

  fuchsia::tee::Buffer buffer;
  buffer.set_vmo(std::move(vmo))
      .set_size(static_cast<uint64_t>(contents.size()))
      .set_offset(0)
      .set_direction(fuchsia::tee::Direction::INOUT);
  return fit::ok(std::move(buffer));
}

fuchsia::tee::Value CreateReturnCodeParameter() {
  fuchsia::tee::Value value;
  value.set_direction(fuchsia::tee::Direction::OUTPUT);
  return value;
}

fit::result<fuchsia::tee::Buffer> GetCommandBuffer(
    std::vector<fuchsia::tee::Parameter>* parameter_set) {
  ZX_DEBUG_ASSERT(parameter_set);
  constexpr size_t kParamBufferIndex = 0;

  if (!parameter_set->at(kParamBufferIndex).is_buffer()) {
    return fit::error();
  }

  fuchsia::tee::Buffer& buffer = parameter_set->at(kParamBufferIndex).buffer();
  if (!buffer.has_vmo() || !buffer.has_size() || !buffer.has_offset() || !buffer.has_direction()) {
    return fit::error();
  }
  if (buffer.offset() >= buffer.size()) {
    return fit::error();
  }

  return fit::ok(std::move(buffer));
}

bool IsExpectedSecmemCommandResult(const fuchsia::tee::OpResult& result) {
  return result.has_parameter_set() && result.parameter_set().size() == 4 &&
         result.has_return_code() && result.has_return_origin();
}

}  // namespace

fit::result<SecmemSession, fuchsia::tee::DeviceSyncPtr> SecmemSession::TryOpen(
    fuchsia::tee::DeviceSyncPtr tee_connection) {
  if (!tee_connection.is_bound()) {
    return fit::error(std::move(tee_connection));
  }

  fuchsia::tee::OpResult result;
  uint32_t session_id = 0;
  auto params = std::vector<fuchsia::tee::Parameter>();

  if (zx_status_t status =
          tee_connection->OpenSession(kSecmemUuid, std::move(params), &session_id, &result);
      status != ZX_OK) {
    LOG(ERROR, "OpenSession channel call failed - status: %d", status);
    return fit::error(std::move(tee_connection));
  }

  if (!result.has_return_code() || !result.has_return_origin()) {
    LOG(ERROR, "OpenSession returned with result codes missing");
    return fit::error(std::move(tee_connection));
  }

  if (result.return_code() != TEEC_SUCCESS) {
    LOG(ERROR, "OpenSession to secmem failed - TEEC_Result: %" PRIx64 ", origin: %" PRIu32 ".",
        result.return_code(), static_cast<uint32_t>(result.return_origin()));
    return fit::error(std::move(tee_connection));
  }

  return fit::ok(SecmemSession{session_id, std::move(tee_connection)});
}

SecmemSession::~SecmemSession() {
  if (tee_connection_.is_bound()) {
    tee_connection_->CloseSession(session_id_);
  }
}

void SecmemSession::PackUint32Parameter(uint32_t value, std::vector<uint8_t>* buffer) {
  ZX_DEBUG_ASSERT(buffer);

  TeeCommandParam param;
  param.type = kTeeParamTypeUint32;
  param.param.u32 = value;

  auto param_begin = reinterpret_cast<const uint8_t*>(&param);
  auto param_end = reinterpret_cast<const uint8_t*>(&param) + (sizeof(param) / sizeof(uint8_t));

  const size_t new_buf_size = fbl::round_up(buffer->size() + sizeof(param), kParameterAlignment);

  buffer->reserve(new_buf_size);
  buffer->insert(buffer->end(), param_begin, param_end);

  if (buffer->size() < new_buf_size) {
    std::fill_n(std::back_inserter(*buffer), new_buf_size - buffer->size(), 0);
  }
}

TEEC_Result SecmemSession::InvokeSecmemCommand(uint32_t command,
                                               std::vector<uint8_t>* cmd_buffer_vec) {
  ZX_DEBUG_ASSERT(cmd_buffer_vec);

  if (!tee_connection_.is_bound()) {
    return TEEC_ERROR_TARGET_DEAD;
  }

  // The first parameter is where all of Amlogic's custom parameters are packed.
  fuchsia::tee::Buffer in_cmd_buffer;
  if (auto in_cmd_buffer_result = CreateCommandBuffer(*cmd_buffer_vec);
      in_cmd_buffer_result.is_ok()) {
    in_cmd_buffer = in_cmd_buffer_result.take_value();
  } else {
    return TEEC_ERROR_COMMUNICATION;
  }

  constexpr size_t kNumParams = 4;
  auto params = std::vector<fuchsia::tee::Parameter>();
  params.reserve(kNumParams);
  params.push_back(fuchsia::tee::Parameter::WithBuffer(std::move(in_cmd_buffer)));
  params.push_back(fuchsia::tee::Parameter::WithNone(fuchsia::tee::None{}));
  params.push_back(fuchsia::tee::Parameter::WithNone(fuchsia::tee::None{}));
  params.push_back(fuchsia::tee::Parameter::WithValue(CreateReturnCodeParameter()));

  fuchsia::tee::OpResult result;
  if (zx_status_t status =
          tee_connection_->InvokeCommand(session_id_, command, std::move(params), &result);
      status != ZX_OK) {
    LOG(ERROR, "InvokeCommand channel call failed - status: %d", status);
    return TEEC_ERROR_COMMUNICATION;
  }

  if (!IsExpectedSecmemCommandResult(result)) {
    LOG(ERROR, "InvokeCommand returned with unexpected OpResult");
    return TEEC_ERROR_COMMUNICATION;
  }

  fuchsia::tee::Buffer out_cmd_buffer;
  if (auto out_cmd_buffer_result = GetCommandBuffer(result.mutable_parameter_set());
      out_cmd_buffer_result.is_ok()) {
    out_cmd_buffer = out_cmd_buffer_result.take_value();
  } else {
    LOG(ERROR, "Secmem command returned with unexpected command buffer parameter");
    return TEEC_ERROR_COMMUNICATION;
  }

  // Ensure that `cmd_buffer_vec` is of the appropriate size
  cmd_buffer_vec->resize(out_cmd_buffer.size() - out_cmd_buffer.offset(), /*val=*/0);

  // Read output into provided `cmd_buffer_vec`
  if (zx_status_t status =
          out_cmd_buffer.vmo().read(cmd_buffer_vec->data(), out_cmd_buffer.offset(),
                                    out_cmd_buffer.size() - out_cmd_buffer.offset());
      status != ZX_OK) {
    LOG(ERROR, "Failed to read parameters from VMO - status: %d", status);
    return TEEC_ERROR_COMMUNICATION;
  }

  return static_cast<TEEC_Result>(result.return_code());
}

fit::result<uint32_t> SecmemSession::UnpackUint32Parameter(const std::vector<uint8_t>& buffer,
                                                           size_t* offset_in_out) {
  ZX_DEBUG_ASSERT(offset_in_out);

  size_t offset = *offset_in_out;

  if (offset + sizeof(TeeCommandParam) > buffer.size()) {
    return fit::error();
  }

  const uint8_t* param_addr = buffer.data() + offset;
  auto param = reinterpret_cast<const TeeCommandParam*>(param_addr);
  if (param->type != kTeeParamTypeUint32) {
    LOG(ERROR, "Received unexpected param type");
    return fit::error();
  }

  offset += sizeof(TeeCommandParam);
  *offset_in_out = fbl::round_up(offset, kParameterAlignment);

  return fit::ok(param->param.u32);
}

zx_status_t SecmemSession::GetVp9HeaderSize(zx_paddr_t vp9_paddr, uint32_t before_size,
                                            uint32_t max_after_size, uint32_t* after_size) {
  ZX_DEBUG_ASSERT(after_size);
  if (!safemath::IsValueInRangeForNumericType<uint32_t>(vp9_paddr)) {
    LOG(ERROR, "vp9_paddr exceeds 32-bit range");
    return ZX_ERR_INVALID_ARGS;
  }
  if (!safemath::CheckAdd(vp9_paddr, before_size).IsValid()) {
    LOG(ERROR, "vp9_paddr + before_size overflow");
    return ZX_ERR_INVALID_ARGS;
  }
  if ((vp9_paddr & ZX_PAGE_MASK) != 0) {
    // If the intra-page offset is exactly 16, that has special meaning to the TA, so instead of
    // risking that we randomly encounter that case later, require page alignment.
    LOG(ERROR, "vp9_paddr must be page-aligned for now");
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t size_diff;
  if (!safemath::CheckSub(max_after_size, before_size).AssignIfValid(&size_diff)) {
    LOG(ERROR, "max_after_size cannot be less than before_size");
    return ZX_ERR_INVALID_ARGS;
  }
  constexpr uint32_t kMaxFramesPerSuperframe = 8;
  constexpr uint32_t kHeaderSizePerFrame = 16;
  if (size_diff < kMaxFramesPerSuperframe * kHeaderSizePerFrame) {
    LOG(ERROR, "max_after_size - before_size < kMaxFramesPerSuperframe * kHeaderSizePerFrame");
    return ZX_ERR_INVALID_ARGS;
  }

  std::vector<uint8_t> cmd_buffer;
  // Reserve room for 3 parameters.
  cmd_buffer.reserve(kParameterAlignment * 3);

  PackUint32Parameter(kSecmemCommandIdGetVp9HeaderSize, &cmd_buffer);
  PackUint32Parameter(static_cast<uint32_t>(vp9_paddr), &cmd_buffer);
  PackUint32Parameter(before_size, &cmd_buffer);

  const TEEC_Result tee_status = InvokeSecmemCommand(kSecmemCommandIdGetVp9HeaderSize, &cmd_buffer);
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "kSecmemCommandIdGetVp9HeaderSize failed - TEEC_Result: 0x%" PRIx32, tee_status);
    return ZX_ERR_INTERNAL;
  }

  size_t output_offset = 0;
  fit::result<uint32_t> header_size_result = UnpackUint32Parameter(cmd_buffer, &output_offset);
  if (!header_size_result.is_ok()) {
    LOG(ERROR, "UnpackUint32Parameter() after kSecmemCommandIdGetVp9HeaderSize failed");
    return ZX_ERR_INTERNAL;
  }

  *after_size = before_size + header_size_result.value();
  return ZX_OK;
}
