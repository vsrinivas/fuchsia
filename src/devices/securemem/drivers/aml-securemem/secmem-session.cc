// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secmem-session.h"

#include <zircon/assert.h>

#include <algorithm>
#include <iterator>

#include <fbl/algorithm.h>
#include <safemath/checked_math.h>

#include "log.h"

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
    LOG(WARNING, "OpenSession to secmem failed - TEEC_Result: %" PRIx64 ", origin: %" PRIu32 ".",
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

TEEC_Result SecmemSession::ProtectMemoryRange(uint32_t start, uint32_t length, bool is_enable) {
  std::vector<uint8_t> cmd_buffer;
  // Reserve room for 4 parameters.
  cmd_buffer.reserve(kParameterAlignment * 4);

  PackUint32Parameter(kSecmemCommandIdProtectMemory, &cmd_buffer);

  uint32_t enable_protection = is_enable ? 1 : 0;
  PackUint32Parameter(enable_protection, &cmd_buffer);

  // must be 1-4 inclusive
  constexpr uint32_t kRegionNum = 1;
  PackUint32Parameter(kRegionNum, &cmd_buffer);

  PackUint32Parameter(start, &cmd_buffer);

  PackUint32Parameter(length, &cmd_buffer);

  return InvokeSecmemCommand(kSecmemCommandIdProtectMemory, &cmd_buffer);
}

TEEC_Result SecmemSession::AllocateSecureMemory(uint32_t* start, uint32_t* length) {
  // First, ask secmem TA for the max size of VDEC, then allocate that size.

  std::vector<uint8_t> cmd_buffer;
  // Reserve room for 4 parameters.
  cmd_buffer.reserve(kParameterAlignment * 4);

  // kSecmemCommandIdGetMemSize command first
  PackUint32Parameter(kSecmemCommandIdGetMemSize, &cmd_buffer);
  TEEC_Result tee_status = InvokeSecmemCommand(kSecmemCommandIdGetMemSize, &cmd_buffer);
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "kSecmemCommandIdGetMemSize failed - TEEC_Result: %" PRIx32, tee_status);
    return tee_status;
  }

  size_t output_offset = 0;
  fit::result<uint32_t> max_vdec_size_result = UnpackUint32Parameter(cmd_buffer, &output_offset);
  if (!max_vdec_size_result.is_ok()) {
    LOG(ERROR, "UnpackUint32Parameter() after kSecmemCommandIdGetMemSize failed");
    return TEEC_ERROR_COMMUNICATION;
  }

  // Reset for new command: kSecmemCommandIdAllocateSecureMemory.
  cmd_buffer.clear();

  PackUint32Parameter(kSecmemCommandIdAllocateSecureMemory, &cmd_buffer);

  // ignored
  constexpr uint32_t kDbgLevel = 0;
  PackUint32Parameter(kDbgLevel, &cmd_buffer);

  // We can pass false for is_vp9, even if later when we do
  // kSecmemCommandIdGetVp9HeaderSize we start at exactly one AMLV header length
  // into a page to avoid one frame/sub-frame being copied.
  constexpr auto kIsVp9 = static_cast<uint32_t>(false);  // 0
  PackUint32Parameter(kIsVp9, &cmd_buffer);

  PackUint32Parameter(max_vdec_size_result.value(), &cmd_buffer);

  tee_status = InvokeSecmemCommand(kSecmemCommandIdAllocateSecureMemory, &cmd_buffer);
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "kSecmemCommandIdAllocateSecureMemory failed - TEEC_Result: %" PRIx32, tee_status);
    return tee_status;
  }

  output_offset = 0;
  fit::result<uint32_t> vdec_paddr_result = UnpackUint32Parameter(cmd_buffer, &output_offset);
  if (!vdec_paddr_result.is_ok()) {
    LOG(ERROR, "UnpackUint32Parameter() after kSecmemCommandIdAllocateSecureMemory failed");
    return TEEC_ERROR_COMMUNICATION;
  }

  *start = vdec_paddr_result.value();
  *length = max_vdec_size_result.value();

  return TEEC_SUCCESS;
}
