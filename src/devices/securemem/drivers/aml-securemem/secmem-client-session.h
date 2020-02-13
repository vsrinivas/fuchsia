// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SECMEM_CLIENT_SESSION_H_
#define SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SECMEM_CLIENT_SESSION_H_

#include <optional>

#include <tee-client-api/tee_client_api.h>

class SecmemClientSession {
 public:
  explicit SecmemClientSession(TEEC_Context* context);
  ~SecmemClientSession();

  [[nodiscard]] zx_status_t Init();

  [[nodiscard]] TEEC_Result ProtectMemoryRange(uint32_t start, uint32_t length, bool is_enable);
  [[nodiscard]] TEEC_Result AllocateSecureMemory(uint32_t* start, uint32_t* length);

 private:
  void PackUint32Parameter(uint32_t value, size_t* offset_in_out);
  [[nodiscard]] TEEC_Result InvokeSecmemCommand(uint32_t command, size_t length);
  [[nodiscard]] bool UnpackUint32Parameter(uint32_t* value, size_t* offset_in_out);

  TEEC_Context* context_ = {};
  std::optional<TEEC_Session> session_ = {};
  std::optional<TEEC_SharedMemory> parameter_buffer_ = {};
};

#endif  // SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SECMEM_CLIENT_SESSION_H_
