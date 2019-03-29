// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/watcher.h>
#include <src/lib/fxl/logging.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <tee-client-api/tee_client_api.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>

#include <memory>

// Randomly-generated UUID for the TA.
constexpr TEEC_UUID kSecmemUuid = {
    0x2c1a33c0,
    0x44cc,
    0x11e5,
    {0xbc, 0x3b, 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};

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
};

static constexpr uint32_t kParameterAlignment = 32u;
static constexpr uint32_t kParameterBufferSize = 4 * 1024u;
static constexpr uint32_t kParameterBufferPadding = 64u;

class SecmemSession {
 public:
  SecmemSession();
  ~SecmemSession();

  zx_status_t Init();
  // Returns TEE result (negative on failure).
  int ProtectMemoryRange(uint32_t start, size_t length);

 private:
  bool PackUint32Parameter(uint32_t num, size_t* offset_in_out);
  int InvokeSecmemCommand(uint32_t command, size_t length);

  std::unique_ptr<TEEC_Context> context_;
  std::unique_ptr<TEEC_Session> session_;
  std::unique_ptr<TEEC_SharedMemory> parameter_buffer_;
};

SecmemSession::SecmemSession() {}
SecmemSession::~SecmemSession() {
  if (parameter_buffer_)
    TEEC_ReleaseSharedMemory(parameter_buffer_.get());
  if (session_)
    TEEC_CloseSession(session_.get());
  if (context_)
    TEEC_FinalizeContext(context_.get());
}

zx_status_t SecmemSession::Init() {
  uint32_t return_origin;
  context_ = std::make_unique<TEEC_Context>();
  TEEC_Result result = TEEC_InitializeContext(NULL, context_.get());
  if (result != TEEC_SUCCESS) {
    context_.reset();
    return ZX_ERR_INVALID_ARGS;
  }

  session_ = std::make_unique<TEEC_Session>();
  result = TEEC_OpenSession(context_.get(), session_.get(), &kSecmemUuid,
                            TEEC_LOGIN_PUBLIC, NULL, NULL, &return_origin);
  if (result != TEEC_SUCCESS) {
    session_.reset();
    return ZX_ERR_INVALID_ARGS;
  }

  parameter_buffer_ = std::make_unique<TEEC_SharedMemory>();
  parameter_buffer_->size = kParameterBufferSize;
  parameter_buffer_->flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
  result = TEEC_AllocateSharedMemory(context_.get(), parameter_buffer_.get());
  if (result != TEEC_SUCCESS) {
    parameter_buffer_.reset();
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

bool SecmemSession::PackUint32Parameter(uint32_t value, size_t* offset_in_out) {
  ZX_DEBUG_ASSERT(offset_in_out);
  size_t offset = *offset_in_out;
  ZX_ASSERT(offset <= parameter_buffer_->size - sizeof(TeeCommandParam));
  TeeCommandParam p;
  p.type = kTeeParamTypeUint32;
  p.param.u32 = value;
  auto buffer_address = reinterpret_cast<uint8_t*>(parameter_buffer_->buffer);
  memcpy(buffer_address + offset, &p, sizeof(TeeCommandParam));
  offset += sizeof(TeeCommandParam);
  *offset_in_out = fbl::round_up(offset, kParameterAlignment);
  return true;
}

int SecmemSession::InvokeSecmemCommand(uint32_t command, size_t length) {
  TEEC_Operation operation = {};

  operation.paramTypes =
      TEEC_PARAM_TYPES(TEEC_MEMREF_PARTIAL_INOUT,  // Shared memory buffer
                       TEEC_NONE, TEEC_NONE,
                       TEEC_VALUE_OUTPUT);  // Command result
  operation.params[0].memref.parent = parameter_buffer_.get();
  operation.params[0].memref.offset = 0;
  operation.params[0].memref.size = length + kParameterBufferPadding;
  TEEC_Result res =
      TEEC_InvokeCommand(session_.get(), command, &operation, nullptr);
  if (res != TEEC_SUCCESS) {
    return res;
  }
  return operation.params[3].value.a;
}

int SecmemSession::ProtectMemoryRange(uint32_t start, size_t length) {
  if (length > std::numeric_limits<uint32_t>::max()) {
    FXL_LOG(ERROR) << "Protected memory range too large";
    return -1;
  }
  size_t input_offset = 0;

  PackUint32Parameter(kSecmemCommandIdProtectMemory, &input_offset);
  PackUint32Parameter(1, &input_offset);

  constexpr uint32_t kEnableProtection = 1;
  PackUint32Parameter(kEnableProtection, &input_offset);
  PackUint32Parameter(start, &input_offset);
  PackUint32Parameter(length, &input_offset);

  return InvokeSecmemCommand(kSecmemCommandIdProtectMemory, input_offset);
}

zx_status_t WaitForDriver(const char* path) {
  auto DeviceAdded = [](int dirfd, int event, const char* name, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if (!strcmp("000", name)) {
      return ZX_ERR_STOP;
    } else {
      return ZX_OK;
    }
  };
  fbl::unique_fd dir_fd(open(path, O_DIRECTORY | O_RDONLY));
  zx_status_t status = fdio_watch_directory(dir_fd.get(), DeviceAdded,
                                            ZX_TIME_INFINITE, nullptr);
  if (status == ZX_ERR_STOP) {
    return ZX_OK;
  } else {
    ZX_DEBUG_ASSERT(status != ZX_OK);
    return status;
  }
}

// This implementation is amlogic-specific for now.
int main(int argc, const char* const* argv) {
  zx_status_t status;
  status = WaitForDriver("/dev/class/sysmem");
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Wait for sysmem driver failed: " << status;
    return -1;
  }

  zx::channel sysmem_driver_connector_server, sysmem_driver_connector_client;
  status = zx::channel::create(0, &sysmem_driver_connector_server,
                               &sysmem_driver_connector_client);
  ZX_ASSERT(status == ZX_OK);

  status = fdio_service_connect("/dev/class/sysmem/000",
                                sysmem_driver_connector_server.release());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "fdio_service_connect failed: " << status;
    return -1;
  }

  zx_status_t status_out;
  uint64_t base, size;
  status = fuchsia_sysmem_DriverConnectorGetProtectedMemoryInfo(
      sysmem_driver_connector_client.get(), &status_out, &base, &size);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read protected memory information";
    return -1;
  }

  // Not an error; this can happen if no protected memory is available.
  if (status_out == ZX_ERR_NOT_SUPPORTED)
    return 0;

  if (status_out != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read protected memory information";
    return -1;
  }

  // Not an error; this can happen if no protected memory is available.
  if (size == 0) {
    return 0;
  }

  // Only wait after checking whether there should really be protected memory
  // allocated; otherwise this process will exit early before this call.
  if (WaitForDriver("/dev/class/tee") != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for TEE driver";
    return -1;
  }

  auto session = std::make_unique<SecmemSession>();
  if (session->Init() != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initialize secmem session";
    return -1;
  }
  int command_result = session->ProtectMemoryRange(static_cast<uint32_t>(base),
                                                   static_cast<uint32_t>(size));
  if (command_result < 0) {
    FXL_LOG(ERROR) << "Failed to protect memory range, result "
                   << command_result;
    return command_result;
  }
  FXL_LOG(INFO) << "Sysmem-assistant initialized protected memory, size: "
                << size;

  // The memory will stay protected as long as the system is running.
  return 0;
}
