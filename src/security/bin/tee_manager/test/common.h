// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_BIN_TEE_MANAGER_TEST_COMMON_H_
#define SRC_SECURITY_BIN_TEE_MANAGER_TEST_COMMON_H_

#include <fuchsia/tee/cpp/fidl.h>

#include <gtest/gtest.h>
#include <tee-client-api/tee_client_api.h>

namespace optee {
namespace test {

struct OperationResult {
  TEEC_Result result;
  uint32_t return_origin;
};

::testing::AssertionResult IsTeecSuccess(TEEC_Result result);

::testing::AssertionResult IsTeecSuccess(const OperationResult& op_result);

// Helper class to print numeric values in hex for gtest
template <typename NumericType>
class Hex {
 public:
  constexpr explicit Hex(NumericType number) : number_(number) {}

  friend std::ostream& operator<<(std::ostream& os, const Hex<NumericType>& obj) {
    return os << "0x" << std::hex << obj.number_;
  }

 private:
  NumericType number_;
};

std::vector<uint8_t> StringToBuffer(const std::string& s);

std::string BufferToString(const std::vector<uint8_t>& buf);

class ContextGuard {
 public:
  ContextGuard();

  explicit ContextGuard(TEEC_Context* context);

  ~ContextGuard();

  ContextGuard(ContextGuard&& other);

  ContextGuard& operator=(ContextGuard&& other);

  ContextGuard(const ContextGuard&) = delete;
  ContextGuard& operator=(const ContextGuard&) = delete;

  bool IsValid() const;

  TEEC_Context* Get() const;

  void Close();

  TEEC_Context* Release();

 private:
  TEEC_Context* context_;
};

class SessionGuard {
 public:
  SessionGuard();

  explicit SessionGuard(TEEC_Session* session);

  ~SessionGuard();

  SessionGuard(SessionGuard&& other);

  SessionGuard& operator=(SessionGuard&& other);

  SessionGuard(const SessionGuard&) = delete;
  SessionGuard& operator=(const SessionGuard&) = delete;

  bool IsValid() const;

  TEEC_Session* Get() const;

  void Close();

  TEEC_Session* Release();

 private:
  TEEC_Session* session_;
};

}  // namespace test
}  // namespace optee

#endif  // SRC_SECURITY_BIN_TEE_MANAGER_TEST_COMMON_H_
