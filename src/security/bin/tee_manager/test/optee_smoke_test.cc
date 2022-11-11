// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/tee/cpp/fidl.h>
#include <lib/fit/defer.h>
#include <lib/zx/handle.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <gtest/gtest.h>
#include <tee-client-api/tee_client_api.h>

#include "common.h"

namespace optee {
namespace test {
namespace {

// UUID of the keysafe TA.
//
// We use this TA because it is there.  We are just trying to verify
// connectivity with any TA running in the TEE.
const TEEC_UUID kKeysafeTaUuid = {
    0x808032e0, 0xfd9e, 0x4e6f, {0x88, 0x96, 0x54, 0x47, 0x35, 0xc9, 0x84, 0x80}};
// Command ID of the SignHash function of the TA.
const uint32_t kKeysafeGetHardwareDerivedKeyCmdID = 5;
const char kHardwareKeyInfo[] = "zxcrypt";
const size_t kExpectedKeyInfoSize = 32;
const size_t kDerivedKeySize = 16;

uint32_t GetHandleCount(zx::unowned_handle h) {
  zx_info_handle_count_t info = {};
  auto err = h->get_info(ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr);
  if (err) {
    return -1;
  }
  return info.handle_count;
}

}  // namespace

class OpteeSmokeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    TEEC_Result result = TEEC_InitializeContext(nullptr, &context_);
    ASSERT_TRUE(IsTeecSuccess(result));
    context_guard_ = ContextGuard(&context_);

    OperationResult op_result;
    op_result.result = TEEC_OpenSession(&context_, &session_, &kKeysafeTaUuid, TEEC_LOGIN_PUBLIC,
                                        nullptr, nullptr, &op_result.return_origin);
    ASSERT_TRUE(IsTeecSuccess(op_result));
    session_guard_ = SessionGuard(&session_);
  }

  void TearDown() override {
    // nothing to do here
  }

  TEEC_Context* GetContext() { return &context_; }
  TEEC_Session* GetSession() { return &session_; }

 private:
  TEEC_Context context_{};
  ContextGuard context_guard_;
  TEEC_Session session_{};
  SessionGuard session_guard_;
};

TEST_F(OpteeSmokeTest, VerifyTeeConnectivity) {
  // key_info is |kHardwareKeyInfo| padded with 0.
  uint8_t key_info[kExpectedKeyInfoSize] = {};
  memcpy(key_info, kHardwareKeyInfo, sizeof(kHardwareKeyInfo));

  // Hardware derived key is expected to be 128-bit AES key.
  auto key_buffer = std::make_unique<uint8_t[]>(kDerivedKeySize);

  TEEC_Operation op = {};
  op.params[0].tmpref.buffer = key_info;
  op.params[0].tmpref.size = sizeof(key_info);
  op.params[3].tmpref.buffer = key_buffer.get();
  op.params[3].tmpref.size = kDerivedKeySize;
  op.paramTypes =
      TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_NONE, TEEC_NONE, TEEC_MEMREF_TEMP_OUTPUT);

  OperationResult op_result;
  op_result.result = TEEC_InvokeCommand(GetSession(), kKeysafeGetHardwareDerivedKeyCmdID, &op,
                                        &op_result.return_origin);

  ASSERT_TRUE(IsTeecSuccess(op_result));
  ASSERT_EQ(op.params[3].tmpref.size, kDerivedKeySize);
}

TEST_F(OpteeSmokeTest, SupportsNullMemoryReferences) {
  // Both input and output null memory references should be supported.
  TEEC_Operation op = {};
  op.params[0].tmpref.buffer = 0;
  op.params[0].tmpref.size = 0;
  op.params[3].tmpref.buffer = 0;
  op.params[3].tmpref.size = 0;
  op.paramTypes =
      TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_NONE, TEEC_NONE, TEEC_MEMREF_TEMP_OUTPUT);

  OperationResult op_result;
  op_result.result = TEEC_InvokeCommand(GetSession(), kKeysafeGetHardwareDerivedKeyCmdID, &op,
                                        &op_result.return_origin);

  // The TA is not expected to succeed given this input. It is sufficient to verify that
  // the error origin is not the api or the comms.
  ASSERT_TRUE(IsTeecSuccess(op_result) || ((op_result.return_origin != TEEC_ORIGIN_API) &&
                                           (op_result.return_origin != TEEC_ORIGIN_COMMS)));
}

TEST_F(OpteeSmokeTest, VmosNotLeaked) {
  // key_info is |kHardwareKeyInfo| padded with 0.
  uint8_t key_info[kExpectedKeyInfoSize] = {};
  memcpy(key_info, kHardwareKeyInfo, sizeof(kHardwareKeyInfo));

  TEEC_SharedMemory shared_mem = {
      .buffer = nullptr, .size = kDerivedKeySize, .flags = TEEC_MEM_OUTPUT};
  auto alloc_result = TEEC_AllocateSharedMemory(GetContext(), &shared_mem);
  ASSERT_TRUE(IsTeecSuccess(alloc_result));

  auto shared_mem_cleanup = fit::defer([&shared_mem]() { TEEC_ReleaseSharedMemory(&shared_mem); });

  TEEC_Operation op = {};
  op.params[0].tmpref.buffer = key_info;
  op.params[0].tmpref.size = sizeof(key_info);
  op.params[3].memref = {.parent = &shared_mem, .size = kDerivedKeySize, .offset = 0};
  op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_NONE, TEEC_NONE, TEEC_MEMREF_WHOLE);

  OperationResult op_result;
  op_result.result = TEEC_InvokeCommand(GetSession(), kKeysafeGetHardwareDerivedKeyCmdID, &op,
                                        &op_result.return_origin);

  ASSERT_TRUE(IsTeecSuccess(op_result));
  ASSERT_EQ(op.params[3].memref.size, kDerivedKeySize);

  ASSERT_EQ(GetHandleCount(zx::unowned_handle(shared_mem.imp.vmo)), 1u);
}

}  // namespace test
}  // namespace optee
