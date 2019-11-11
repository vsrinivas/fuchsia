// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/securemem/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/algorithm.h>
#include <src/lib/fxl/logging.h>
#include <tee-client-api/tee_client_api.h>

#include "input_copier.h"

// Randomly-generated UUID for the TA.
constexpr TEEC_UUID kClearTvpUuid = {
    0x41fe9859, 0x71e4, 0x4bf4, {0xbb, 0xaa, 0xd7, 0x14, 0x35, 0xb1, 0x27, 0xae}};

class ClearTvpSession : public InputCopier {
 public:
  ClearTvpSession() {}
  ~ClearTvpSession();

  zx_status_t Init();

  uint32_t PaddingLength() const override {
    // clearTVP adds 0x00, 0x00, 0x00, 0x01 to end of copied data.
    return 4;
  }
  int DecryptVideo(void* data, uint32_t data_len, const zx::vmo& vmo) override;

 private:
  fuchsia::hardware::securemem::DeviceSyncPtr securemem_;
  std::unique_ptr<TEEC_Context> context_;
  std::unique_ptr<TEEC_Session> session_;
};

ClearTvpSession::~ClearTvpSession() {
  if (session_)
    TEEC_CloseSession(session_.get());
  if (context_)
    TEEC_FinalizeContext(context_.get());
}

zx_status_t ClearTvpSession::Init() {
  zx_status_t status = fdio_service_connect("/dev/class/securemem/000",
                                            securemem_.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Connecting to securemem failed";
    return status;
  }
  uint32_t return_origin;
  context_ = std::make_unique<TEEC_Context>();
  TEEC_Result result = TEEC_InitializeContext(NULL, context_.get());
  if (result != TEEC_SUCCESS) {
    context_.reset();
    FXL_LOG(ERROR) << "TEEC_InitializeContext failed " << result;
    return ZX_ERR_INVALID_ARGS;
  }

  session_ = std::make_unique<TEEC_Session>();
  result = TEEC_OpenSession(context_.get(), session_.get(), &kClearTvpUuid, TEEC_LOGIN_PUBLIC, NULL,
                            NULL, &return_origin);
  if (result != TEEC_SUCCESS) {
    session_.reset();
    FXL_LOG(ERROR) << "TEEC_OpenSession failed with result " << result << " origin "
                   << return_origin << ". Maybe bootloader the version is incorrect.";
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

constexpr uint32_t kClearTvpCommandDecryptVideo = 6;

int ClearTvpSession::DecryptVideo(void* data, uint32_t data_len, const zx::vmo& vmo) {
  zx::vmo dup_vmo;
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
  if (status != ZX_OK) {
    return TEEC_ERROR_GENERIC;
  }
  zx_status_t status2 = ZX_OK;
  zx_paddr_t output_paddr;

  status = securemem_->GetSecureMemoryPhysicalAddress(std::move(dup_vmo), &status2, &output_paddr);
  if (status != ZX_OK || status2 != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get physical address: " << status << " " << status2;
    return TEEC_ERROR_GENERIC;
  }

  TEEC_Operation operation = {};

  operation.paramTypes =
      TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_VALUE_INPUT, TEEC_VALUE_INPUT, TEEC_NONE);
  operation.params[0].tmpref.buffer = data;
  operation.params[0].tmpref.size = data_len;
  // clear data len
  operation.params[1].value.a = data_len;
  // enc data len - all input data is clear
  operation.params[1].value.b = 0;
  // output_offset - not needed since any offset is baked into output_handle
  operation.params[2].value.a = 0;
  // output_handle
  operation.params[2].value.b = output_paddr;
  TEEC_Result res =
      TEEC_InvokeCommand(session_.get(), kClearTvpCommandDecryptVideo, &operation, nullptr);
  if (res != TEEC_SUCCESS) {
    FXL_LOG(ERROR) << "Failed to invoke command: " << res;
    return res;
  }
  return 0;
}

std::unique_ptr<InputCopier> InputCopier::Create() {
  auto tvp = std::make_unique<ClearTvpSession>();
  if (tvp->Init() != ZX_OK)
    return nullptr;
  return tvp;
}
