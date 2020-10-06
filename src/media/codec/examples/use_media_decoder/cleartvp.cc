// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/securemem/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/algorithm.h>
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
  zx_status_t DecryptVideo(void* data, uint32_t data_len, const zx::vmo& vmo) override;

 private:
  void EnsureSessionClosed();
  zx_status_t OpenSession();

  fuchsia::hardware::securemem::DeviceSyncPtr securemem_;
  std::unique_ptr<TEEC_Context> context_;
  std::unique_ptr<TEEC_Session> session_;
};

ClearTvpSession::~ClearTvpSession() {
  EnsureSessionClosed();
  if (context_)
    TEEC_FinalizeContext(context_.get());
}

void ClearTvpSession::EnsureSessionClosed() {
  if (session_) {
    TEEC_CloseSession(session_.get());
    session_ = nullptr;
  }
}

zx_status_t ClearTvpSession::Init() {
  zx_status_t status = fdio_service_connect("/dev/class/securemem/000",
                                            securemem_.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Connecting to securemem failed";
    return status;
  }
  context_ = std::make_unique<TEEC_Context>();
  TEEC_Result result = TEEC_InitializeContext(NULL, context_.get());
  if (result != TEEC_SUCCESS) {
    context_ = nullptr;
    FX_LOGS(ERROR) << "TEEC_InitializeContext failed " << std::hex << result;
    return ZX_ERR_INVALID_ARGS;
  }

  status = OpenSession();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "OpenSession() failed with status " << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t ClearTvpSession::OpenSession() {
  ZX_DEBUG_ASSERT(!session_);
  zx_status_t status = ZX_ERR_INTERNAL;
  for (uint32_t i = 0; i < 20; ++i) {
    session_ = std::make_unique<TEEC_Session>();
    uint32_t return_origin;
    TEEC_Result result = TEEC_OpenSession(context_.get(), session_.get(), &kClearTvpUuid,
                                          TEEC_LOGIN_PUBLIC, NULL, NULL, &return_origin);
    if (result != TEEC_SUCCESS) {
      session_ = nullptr;
      FX_LOGS(ERROR) << "TEEC_OpenSession failed with result " << std::hex << result << " origin "
                     << return_origin << ". Maybe the bootloader version is incorrect.";
      status = ZX_ERR_INTERNAL;
      continue;
    }
    return ZX_OK;
  }
  return status;
}

constexpr uint32_t kClearTvpCommandDecryptVideo = 6;

int ClearTvpSession::DecryptVideo(void* data, uint32_t data_len, const zx::vmo& vmo) {
  zx_status_t status = ZX_ERR_INTERNAL;
  for (uint32_t i = 0; i < 20; ++i) {
    if (!session_) {
      status = OpenSession();
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "OpenSession() failed - status: " << status;
        return status;
      }
    }

    zx::vmo dup_vmo;
    status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "vmo.duplicate() failed - status: " << status;
      return status;
    }
    zx_status_t status2 = ZX_OK;
    zx_paddr_t output_paddr;

    status =
        securemem_->GetSecureMemoryPhysicalAddress(std::move(dup_vmo), &status2, &output_paddr);
    if (status != ZX_OK || status2 != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to get physical address: " << status << " " << status2;
      if (status == ZX_OK) {
        status = status2;
      }
      return status;
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
    uint32_t return_origin = -1;
    TEEC_Result res = TEEC_InvokeCommand(session_.get(), kClearTvpCommandDecryptVideo, &operation,
                                         &return_origin);
    if (res != TEEC_SUCCESS) {
      FX_LOGS(ERROR) << "Failed to invoke command: 0x" << std::hex << res
                     << " return_origin: " << return_origin;
      EnsureSessionClosed();
      status = ZX_ERR_INTERNAL;
      continue;
    }
    return ZX_OK;
  }
  return status;
}

std::unique_ptr<InputCopier> InputCopier::Create() {
  auto tvp = std::make_unique<ClearTvpSession>();
  if (tvp->Init() != ZX_OK) {
    // Give up if this happens.
    ZX_PANIC("tvp->Init() failed (ClearTvpSession)");
    return nullptr;
  }
  return tvp;
}
