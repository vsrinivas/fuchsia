// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/security/kms-stateless/kms-stateless.h"

#include <fcntl.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <stdint.h>
#include <stdio.h>

#include <filesystem>
#include <memory>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <ramdevice-client/ramdisk.h>

#include "src/security/lib/keysafe/keysafe.h"
#include "tee-client-api/tee_client_api.h"

namespace kms_stateless {
namespace {

const size_t kDerivedKeySize = 16;
const char kDeviceClass[] = "/dev/class/tee";
// Path estimated to be "/dev/class/tee/XXX".
const size_t kMaxPathLen = 64;

// UUID of the keysafe TA.
const TEEC_UUID kKeysafeTaUuid = TA_KEYSAFE_UUID;

// Wrapper around TEEC_Session to ensure correct deletion.
class ScopedTeecSession {
 public:
  ScopedTeecSession(TEEC_Session session_) { session = session_; }
  TEEC_Result invokeCommand(uint32_t commandID, TEEC_Operation* operation) {
    return TEEC_InvokeCommand(&session, commandID, operation, nullptr);
  }
  ~ScopedTeecSession() { TEEC_CloseSession(&session); }

 private:
  TEEC_Session session;
};

// Wrapper around TEEC_Context to ensure correct deletion.
class ScopedTeecContext {
 public:
  ScopedTeecContext() : context_({}), initialized_(false) {}
  ~ScopedTeecContext() {
    if (initialized_) {
      TEEC_FinalizeContext(&context_);
    }
  }

  TEEC_Result initialize(const char* device_path) {
    TEEC_Result result = TEEC_InitializeContext(device_path, &context_);
    if (result == TEEC_SUCCESS) {
      initialized_ = true;
    }
    return result;
  }

  std::unique_ptr<ScopedTeecSession> openSession() {
    TEEC_Session session = {0, 0};
    TEEC_Result result = TEEC_OpenSession(&context_, &session, &kKeysafeTaUuid, TEEC_LOGIN_PUBLIC,
                                          0 /* connectionData*/, nullptr /* operation */,
                                          nullptr /* returnOrigin */);
    if (result != TEEC_SUCCESS) {
      fprintf(stderr, "TEE Unable to open session. Error: %X\n", result);
      return std::unique_ptr<ScopedTeecSession>();
    }
    std::unique_ptr<ScopedTeecSession> session_ptr = std::make_unique<ScopedTeecSession>(session);
    return session_ptr;
  }

 private:
  TEEC_Context context_;
  bool initialized_;
};

// Gets a hardware derived key from a tee device at |device_path|.
//
// Arguments:
//    device_path: The path to the tee device. If device_path is nullptr, we would let tee client
//                 api select the correct path to connect.
//    key_info: The key information as part of the input to the key derivation function.
//    key_info_size: The size of |key_info|.
//    key_buffer: The caller allocated buffer to store the derived key.
//    key_size: The size of the derived key.
//
// Returns true if the operation succeed, false otherwise.
bool GetKeyFromTeeDevice(const char* device_path, uint8_t* key_info, size_t key_info_size,
                         uint8_t* key_buffer, size_t* key_size, size_t key_buffer_size) {
  ScopedTeecContext scoped_teec_context;
  TEEC_Result result = scoped_teec_context.initialize(device_path);
  if (result != TEEC_SUCCESS) {
    fprintf(stderr, "Failed to initialize TEE context: %X\n", result);
    return false;
  }
  std::unique_ptr<ScopedTeecSession> session_ptr = scoped_teec_context.openSession();
  if (!session_ptr.get()) {
    fprintf(stderr, "Failed to open TEE Session to Keysafe!\n");
    return false;
  }

  TEEC_Operation op{};
  op.paramTypes =
      TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_NONE, TEEC_NONE, TEEC_MEMREF_TEMP_OUTPUT);
  op.params[0].tmpref.buffer = key_info;
  op.params[0].tmpref.size = key_info_size;
  op.params[3].tmpref.buffer = key_buffer;
  op.params[3].tmpref.size = key_buffer_size;

  result = session_ptr->invokeCommand(TA_KEYSAFE_CMD_GET_USER_DATA_STORAGE_KEY, &op);
  if (result == TEEC_ERROR_SHORT_BUFFER) {
    fprintf(stderr, "Output buffer for TEE key is too small!\n");
    *key_size = op.params[3].tmpref.size;
    return false;
  }

  if (result != TEEC_SUCCESS) {
    fprintf(stderr, "Failed to get TEE key: result=0x%x\n", result);
    return false;
  }

  *key_size = op.params[3].tmpref.size;
  return true;
}

// Rotates an existing hardware derived key from a tee device at |device_path|.
//
// Arguments:
//    device_path: The path to the tee device. If device_path is nullptr, we would let tee client
//                 api select the correct path to connect.
//    key_info: The key information that identifies the key to be rotated.
//    key_info_size: The size of |key_info|.
//
// Returns TEEC_SUCCESS if the operation succeeds, TEEC_ERROR_* otherwise.
static TEEC_Result RotateKeyFromTeeDevice(const char* device_path, uint8_t* key_info,
                                          size_t key_info_size) {
  ScopedTeecContext scoped_teec_context;
  TEEC_Result result = scoped_teec_context.initialize(device_path);
  if (result != TEEC_SUCCESS) {
    fprintf(stderr, "Failed to initialize TEE context: %X\n", result);
    return result;
  }

  std::unique_ptr<ScopedTeecSession> session_ptr = scoped_teec_context.openSession();
  if (!session_ptr.get()) {
    fprintf(stderr, "Failed to open TEE Session to Keysafe!\n");
    return TEEC_ERROR_GENERIC;
  }

  TEEC_Operation op{};
  op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
  op.params[0].tmpref.buffer = key_info;
  op.params[0].tmpref.size = key_info_size;

  result = session_ptr->invokeCommand(TA_KEYSAFE_CMD_ROTATE_HARDWARE_DERIVED_KEY, &op);
  if (result != TEEC_SUCCESS) {
    fprintf(stderr, "Failed to rotate TEE key: result=0x%x\n", result);
    return result;
  }

  return TEEC_SUCCESS;
}

// The structure to pass as cookie to fdio_watch_directory callback function.
struct WatchTeeArgs {
  // The callback function called when a hardware key is successfully derived.
  GetHardwareDerivedKeyCallback callback;
  uint8_t* key_info;
  size_t key_info_size;
};

// Callback function called when a TEE device is found.
zx_status_t WatchTee(int dirfd, int event, const char* filename, void* cookie) {
  if (std::string_view{filename} == ".") {
    return ZX_OK;
  }
  fbl::StringBuffer<kMaxPathLen> device_path;
  device_path.Append(kDeviceClass).Append("/").Append(filename);
  // Hardware derived key is expected to be 128-bit AES key.
  std::unique_ptr<uint8_t[]> key_buffer(new uint8_t[kDerivedKeySize]);
  size_t key_size = 0;
  WatchTeeArgs* args = reinterpret_cast<WatchTeeArgs*>(cookie);
  if (!GetKeyFromTeeDevice(device_path.c_str(), std::move(args->key_info), args->key_info_size,
                           key_buffer.get(), &key_size, kDerivedKeySize)) {
    fprintf(stderr, "Failed to get hardware derived key from TEE!\n");
    return ZX_ERR_IO;
  }
  if (key_size != kDerivedKeySize) {
    fprintf(stderr, "The hardware derived key is of wrong size!\n");
    return ZX_ERR_IO;
  }
  zx_status_t status = args->callback(std::move(key_buffer), key_size);
  if (status == ZX_OK) {
    return ZX_ERR_STOP;
  } else {
    fprintf(stderr, "Get hardware key callback function returns error!\n");
    return status;
  }
}

}  // namespace

zx_status_t GetHardwareDerivedKey(GetHardwareDerivedKeyCallback callback,
                                  uint8_t key_info[kExpectedKeyInfoSize]) {
  if (wait_for_device(kDeviceClass, ZX_SEC(5)) != ZX_OK) {
    fprintf(stderr, "Error waiting for tee device directory!\n");
    return ZX_ERR_IO;
  }

  fbl::unique_fd dirfd(open(kDeviceClass, O_RDONLY));
  if (!dirfd.is_valid()) {
    fprintf(stderr, "Failed to open tee device directory!\n");
    return ZX_ERR_IO;
  }
  WatchTeeArgs args = {std::move(callback), std::move(key_info), kExpectedKeyInfoSize};
  zx_status_t watch_status =
      fdio_watch_directory(dirfd.get(), WatchTee, ZX_SEC(5), reinterpret_cast<void*>(&args));
  if (watch_status != ZX_ERR_STOP) {
    fprintf(stderr, "Failed to get hardware derived key!\n");
    return watch_status;
  }
  return ZX_OK;
}

zx_status_t GetHardwareDerivedKeyFromService(GetHardwareDerivedKeyCallback callback,
                                             uint8_t key_info[kExpectedKeyInfoSize]) {
  // Hardware derived key is expected to be 128-bit AES key.
  std::unique_ptr<uint8_t[]> key_buffer(new uint8_t[kDerivedKeySize]);
  size_t key_size = 0;
  if (!GetKeyFromTeeDevice(nullptr, key_info, kExpectedKeyInfoSize, key_buffer.get(), &key_size,
                           kDerivedKeySize)) {
    fprintf(stderr, "Failed to get hardware derived key from TEE!\n");
    return ZX_ERR_IO;
  }
  if (key_size != kDerivedKeySize) {
    fprintf(stderr, "The hardware derived key is of wrong size!\n");
    return ZX_ERR_IO;
  }

  return callback(std::move(key_buffer), key_size);
}

zx_status_t RotateHardwareDerivedKeyFromService(uint8_t key_info[kExpectedKeyInfoSize]) {
  TEEC_Result result = RotateKeyFromTeeDevice(nullptr, key_info, kExpectedKeyInfoSize);

  if (result == TEEC_ERROR_NOT_SUPPORTED) {
    fprintf(stderr, "Hardware key rotation not supported by TEE!\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (result != TEEC_SUCCESS) {
    fprintf(stderr, "Failed to rotate hardware key from TEE!\n");
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

}  // namespace kms_stateless
