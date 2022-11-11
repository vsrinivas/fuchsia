// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common.h"

#include <gtest/gtest.h>
#include <tee-client-api/tee_client_api.h>

namespace optee {
namespace test {

::testing::AssertionResult IsTeecSuccess(TEEC_Result result) {
  if (result == TEEC_SUCCESS) {
    return ::testing::AssertionSuccess();
  } else {
    return ::testing::AssertionFailure() << "result: " << Hex(result);
  }
}

::testing::AssertionResult IsTeecSuccess(const OperationResult& op_result) {
  if (op_result.result == TEEC_SUCCESS) {
    return ::testing::AssertionSuccess();
  } else {
    return ::testing::AssertionFailure() << "result: " << Hex(op_result.result)
                                         << ", return origin: " << Hex(op_result.return_origin);
  }
}

std::vector<uint8_t> StringToBuffer(const std::string& s) {
  return std::vector<uint8_t>(s.cbegin(), s.cend());
}

std::string BufferToString(const std::vector<uint8_t>& buf) {
  return std::string(buf.cbegin(), buf.cend());
}

ContextGuard::ContextGuard() : context_(nullptr) {}

ContextGuard::ContextGuard(TEEC_Context* context) : context_(context) {}

ContextGuard::~ContextGuard() { Close(); }

ContextGuard::ContextGuard(ContextGuard&& other) : context_(other.context_) {
  other.context_ = nullptr;
}

ContextGuard& ContextGuard::operator=(ContextGuard&& other) {
  if (&other == this) {
    return *this;
  }

  Close();
  context_ = other.Release();
  return *this;
}

bool ContextGuard::IsValid() const { return context_ != nullptr; }

TEEC_Context* ContextGuard::Get() const { return context_; }

void ContextGuard::Close() {
  if (IsValid()) {
    TEEC_FinalizeContext(context_);
    context_ = nullptr;
  }
}

TEEC_Context* ContextGuard::Release() {
  TEEC_Context* released = context_;
  context_ = nullptr;
  return released;
}

SessionGuard::SessionGuard() : session_(nullptr) {}

SessionGuard::SessionGuard(TEEC_Session* session) : session_(session) {}

SessionGuard::~SessionGuard() { Close(); }

SessionGuard::SessionGuard(SessionGuard&& other) : session_(other.session_) {
  other.session_ = nullptr;
}

SessionGuard& SessionGuard::operator=(SessionGuard&& other) {
  if (&other == this) {
    return *this;
  }

  Close();
  session_ = other.Release();
  return *this;
}

bool SessionGuard::IsValid() const { return session_ != nullptr; }

TEEC_Session* SessionGuard::Get() const { return session_; }

void SessionGuard::Close() {
  if (IsValid()) {
    TEEC_CloseSession(session_);
    session_ = nullptr;
  }
}

TEEC_Session* SessionGuard::Release() {
  TEEC_Session* released = session_;
  session_ = nullptr;
  return released;
}

}  // namespace test
}  // namespace optee
