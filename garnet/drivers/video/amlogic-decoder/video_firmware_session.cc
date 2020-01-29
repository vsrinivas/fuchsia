// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video_firmware_session.h"

#include <zircon/assert.h>

#include <cinttypes>

#include <fbl/algorithm.h>

#include "macros.h"

constexpr TEEC_UUID kVideoFirmwareUuid = {
    0x526fc4fc, 0x7ee6, 0x4a12, {0x96, 0xe3, 0x83, 0xda, 0x95, 0x65, 0xbc, 0xe8}};

// Defined by the TA.
enum VideoFirmwareCommandIds {
  // Firmware for video decode HW.
  kVideoFirmwareCommandIdLoadVideoFirmware = 0,
  // Firmware for video encode HW.
  kVideoFirmwareCommandIdLoadVideoFirmwareEncoder = 1,
  // For normal builds of the TA, this isn't that useful, but it is a command.  We probably won't
  // need to implement a method for this command.
  kVideoFirmwareCommandIdDebugVideoFirmware = 2,
};

VideoFirmwareSession::VideoFirmwareSession(TEEC_Context* context) : context_(context) {
  // nothing else to do here
}

VideoFirmwareSession::~VideoFirmwareSession() {
  if (session_) {
    TEEC_CloseSession(&session_.value());
    session_.reset();
  }
}

zx_status_t VideoFirmwareSession::Init() {
  uint32_t return_origin;

  session_.emplace();
  TEEC_Result result;
  result = TEEC_OpenSession(context_, &session_.value(), &kVideoFirmwareUuid, TEEC_LOGIN_PUBLIC,
                            NULL, NULL, &return_origin);
  if (result != TEEC_SUCCESS) {
    session_.reset();
    LOG(ERROR,
        "TEEC_OpenSession failed - Maybe bootloader version is incorrect - "
        "result: %" PRIx32 " origin: %" PRIu32,
        result, return_origin);
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t VideoFirmwareSession::LoadVideoFirmware(uint8_t* data, uint32_t size) {
  ZX_DEBUG_ASSERT(session_);

  constexpr uint32_t kSignatureSize = 256;
  if (size < kSignatureSize) {
    LOG(ERROR, "size < kSignatureSize -- size: %u", size);
    return ZX_ERR_INVALID_ARGS;
  }

  TEEC_Operation operation = {};
  operation.paramTypes =
      TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_INPUT, TEEC_NONE, TEEC_NONE);
  operation.params[0].tmpref.buffer = data + kSignatureSize;
  operation.params[0].tmpref.size = size - kSignatureSize;
  operation.params[1].tmpref.buffer = data;
  operation.params[1].tmpref.size = kSignatureSize;
  TEEC_Result res = TEEC_InvokeCommand(&session_.value(), kVideoFirmwareCommandIdLoadVideoFirmware,
                                       &operation, nullptr);
  if (res != TEEC_SUCCESS) {
    LOG(ERROR, "kVideoFirmwareCommandIdLoadVideoFirmware failed - res: 0x%x", res);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t VideoFirmwareSession::LoadVideoFirmwareEncoder(uint8_t* data, uint32_t size) {
  ZX_DEBUG_ASSERT(session_);

  constexpr uint32_t kAesIvSize = 16;
  constexpr uint32_t kSignatureSize = 256;
  if (size < kAesIvSize + kSignatureSize) {
    LOG(ERROR, "size < kAesIvSize + kSignatureSize -- size: %u", size);
    return ZX_ERR_INVALID_ARGS;
  }

  TEEC_Operation operation = {};
  operation.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_INPUT,
                                          TEEC_MEMREF_TEMP_INPUT, TEEC_NONE);
  operation.params[0].tmpref.buffer = data;
  operation.params[0].tmpref.size = kAesIvSize;
  operation.params[1].tmpref.buffer = data + kAesIvSize;
  operation.params[1].tmpref.size = kSignatureSize;
  operation.params[2].tmpref.buffer = data + kAesIvSize + kSignatureSize;
  operation.params[2].tmpref.size = size - kAesIvSize - kSignatureSize;
  TEEC_Result res = TEEC_InvokeCommand(
      &session_.value(), kVideoFirmwareCommandIdLoadVideoFirmwareEncoder, &operation, nullptr);
  if (res != TEEC_SUCCESS) {
    LOG(ERROR, "kVideoFirmwareCommandIdLoadVideoFirmwareEncoder failed - res: 0x%x", res);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}
