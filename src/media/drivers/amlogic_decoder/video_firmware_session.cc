// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video_firmware_session.h"

#include <lib/zx/vmo.h>
#include <zircon/assert.h>

#include <cinttypes>

#include <fbl/algorithm.h>

#include "macros.h"

namespace {

// UUID of the TA.
const fuchsia::tee::Uuid kVideoFirmwareUuid = {
    0x526fc4fc, 0x7ee6, 0x4a12, {0x96, 0xe3, 0x83, 0xda, 0x95, 0x65, 0xbc, 0xe8}};

// Defined by video_firmware TA.
enum VideoFirmwareCommandIds {
  // Firmware for video decode HW.
  kVideoFirmwareCommandIdLoadVideoFirmware = 0,
  // Firmware for video encode HW.
  kVideoFirmwareCommandIdLoadVideoFirmwareEncoder = 1,
  // For normal builds of the TA, this isn't that useful, but it is a command.  We probably won't
  // need to implement a method for this command.
  kVideoFirmwareCommandIdDebugVideoFirmware = 2,
};

zx::vmo CreateVmo(uint64_t size) {
  zx::vmo result;
  zx_status_t status = zx::vmo::create(size, /*options=*/0, &result);
  ZX_ASSERT(status == ZX_OK);

  return result;
}

fit::result<fuchsia::tee::Buffer, zx_status_t> CreateBufferParameter(
    const uint8_t* data, uint64_t size, fuchsia::tee::Direction direction) {
  zx::vmo vmo = CreateVmo(size);

  zx_status_t status = vmo.write(data, /*offset=*/0, static_cast<size_t>(size));
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to write to parameter to VMO - status: %d", status);
    return fit::error(status);
  }

  fuchsia::tee::Buffer buffer;
  buffer.set_vmo(std::move(vmo)).set_direction(direction).set_offset(0).set_size(size);

  return fit::ok(std::move(buffer));
}

}  // namespace

fit::result<VideoFirmwareSession, fuchsia::tee::DeviceSyncPtr> VideoFirmwareSession::TryOpen(
    fuchsia::tee::DeviceSyncPtr tee_connection) {
  if (!tee_connection.is_bound()) {
    return fit::error(std::move(tee_connection));
  }

  fuchsia::tee::OpResult result;
  uint32_t session_id = 0;
  auto params = std::vector<fuchsia::tee::Parameter>();

  if (zx_status_t status =
          tee_connection->OpenSession(kVideoFirmwareUuid, std::move(params), &session_id, &result);
      status != ZX_OK) {
    LOG(ERROR, "OpenSession channel call failed (status: %d)", status);
    return fit::error(std::move(tee_connection));
  }

  if (!result.has_return_code() || !result.has_return_origin()) {
    LOG(ERROR, "OpenSession returned with result codes missing");
    return fit::error(std::move(tee_connection));
  }

  if (result.return_code() != TEEC_SUCCESS) {
    LOG(ERROR, "OpenSession to video_firmware failed (result: %" PRIx64 ", origin: %" PRIu32 ").",
        result.return_code(), static_cast<uint32_t>(result.return_origin()));
  }

  return fit::ok(VideoFirmwareSession{session_id, std::move(tee_connection)});
}

VideoFirmwareSession::~VideoFirmwareSession() {
  if (tee_connection_.is_bound()) {
    tee_connection_->CloseSession(session_id_);
  }
}

zx_status_t VideoFirmwareSession::LoadVideoFirmware(const uint8_t* data, uint32_t size) {
  constexpr uint32_t kSignatureSize = 256;
  if (size < kSignatureSize) {
    LOG(ERROR, "size < kSignatureSize -- size: %u", size);
    return ZX_ERR_INVALID_ARGS;
  }

  const uint8_t* payload_data = data + kSignatureSize;
  const size_t payload_size = size - kSignatureSize;
  auto payload_buffer_result =
      CreateBufferParameter(payload_data, payload_size, fuchsia::tee::Direction::INPUT);
  if (!payload_buffer_result.is_ok()) {
    return payload_buffer_result.error();
  }

  const uint8_t* signature_data = data;
  auto signature_buffer_result =
      CreateBufferParameter(signature_data, kSignatureSize, fuchsia::tee::Direction::INPUT);
  if (!signature_buffer_result.is_ok()) {
    return signature_buffer_result.error();
  }

  constexpr size_t kNumParams = 2;
  auto params = std::vector<fuchsia::tee::Parameter>();
  params.reserve(kNumParams);
  params.push_back(fuchsia::tee::Parameter::WithBuffer(payload_buffer_result.take_value()));
  params.push_back(fuchsia::tee::Parameter::WithBuffer(signature_buffer_result.take_value()));

  fuchsia::tee::OpResult result;
  if (zx_status_t status = tee_connection_->InvokeCommand(
          session_id_, kVideoFirmwareCommandIdLoadVideoFirmware, std::move(params), &result);
      status != ZX_OK) {
    LOG(ERROR, "InvokeCommand channel call failed - status: %d", status);
    return status;
  }

  if (!result.has_return_code() || !result.has_return_origin()) {
    LOG(ERROR, "InvokeCommand returned with result codes missing");
    return ZX_ERR_INTERNAL;
  }

  auto tee_status = static_cast<const TEEC_Result>(result.return_code());

  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "kVideoFirmwareCommandIdLoadVideoFirmware failed - TEEC_Result: 0x%" PRIx32,
        tee_status);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t VideoFirmwareSession::LoadVideoFirmwareEncoder(uint8_t* data, uint32_t size) {
  constexpr uint32_t kAesIvSize = 16;
  constexpr uint32_t kSignatureSize = 256;
  if (size < kAesIvSize + kSignatureSize) {
    LOG(ERROR, "size < kAesIvSize + kSignatureSize -- size: %u", size);
    return ZX_ERR_INVALID_ARGS;
  }

  const uint8_t* iv_data = data;
  auto iv_buffer_result =
      CreateBufferParameter(iv_data, kAesIvSize, fuchsia::tee::Direction::INPUT);
  if (!iv_buffer_result.is_ok()) {
    return iv_buffer_result.error();
  }

  const uint8_t* signature_data = data + kAesIvSize;
  auto signature_buffer_result =
      CreateBufferParameter(signature_data, kSignatureSize, fuchsia::tee::Direction::INPUT);
  if (!signature_buffer_result.is_ok()) {
    return signature_buffer_result.error();
  }

  const uint8_t* payload_data = data + kAesIvSize + kSignatureSize;
  const size_t payload_size = size - kSignatureSize - kAesIvSize;
  auto payload_buffer_result =
      CreateBufferParameter(payload_data, payload_size, fuchsia::tee::Direction::INPUT);
  if (!payload_buffer_result.is_ok()) {
    return payload_buffer_result.error();
  }

  constexpr size_t kNumParams = 3;
  auto params = std::vector<fuchsia::tee::Parameter>();
  params.reserve(kNumParams);
  params.push_back(fuchsia::tee::Parameter::WithBuffer(iv_buffer_result.take_value()));
  params.push_back(fuchsia::tee::Parameter::WithBuffer(signature_buffer_result.take_value()));
  params.push_back(fuchsia::tee::Parameter::WithBuffer(payload_buffer_result.take_value()));

  fuchsia::tee::OpResult result;
  if (zx_status_t status = tee_connection_->InvokeCommand(
          session_id_, kVideoFirmwareCommandIdLoadVideoFirmwareEncoder, std::move(params), &result);
      status != ZX_OK) {
    LOG(ERROR, "InvokeCommand channel call failed - status: %d)", status);
    return status;
  }

  if (!result.has_return_code() || !result.has_return_origin()) {
    LOG(ERROR, "InvokeCommand returned with result codes missing");
    return ZX_ERR_INTERNAL;
  }

  auto tee_status = static_cast<const TEEC_Result>(result.return_code());
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "kVideoFirmwareCommandIdLoadVideoFirmwareEncoder failed - TEEC_Result: 0x%" PRIx32,
        tee_status);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}
