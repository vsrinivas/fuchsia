// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-sanitizer-cov-proxy.h"

#include <lib/syslog/cpp/macros.h>

#include "fake-libfuzzer.h"
#include "sanitizer-cov.h"

namespace fuzzing {

FakeSanitizerCovProxy *FakeSanitizerCovProxy::GetInstance() {
  static FakeSanitizerCovProxy instance;
  return &instance;
}

void FakeSanitizerCovProxy::Init8BitCounters(uint8_t *start, uint8_t *stop) {
  FakeSanitizerCovProxy::GetInstance()->InitImpl(sizeof(uint8_t) * (stop - start));
}

void FakeSanitizerCovProxy::InitPcs(const uintptr_t *pcs_beg, const uintptr_t *pcs_end) {
  FakeSanitizerCovProxy::GetInstance()->InitImpl(sizeof(uintptr_t) * (pcs_end - pcs_beg));
}

void FakeSanitizerCovProxy::InitImpl(size_t length) {
  std::lock_guard<std::mutex> lock(lock_);
  inits_.push_back(length);
}

void FakeSanitizerCovProxy::Trace(Instruction::Type type, uintptr_t pc, uint64_t arg0,
                                  uint64_t arg1) {
  FakeSanitizerCovProxy::GetInstance()->TraceImpl(type, pc, arg0, arg1);
}

void FakeSanitizerCovProxy::TraceImpl(uint64_t type, uint64_t pc, uint64_t arg0, uint64_t arg1) {
  std::lock_guard<std::mutex> lock(lock_);
  traces_.push_back(type);
  traces_.push_back(pc);
  traces_.push_back(arg0);
  traces_.push_back(arg1);
}

void FakeSanitizerCovProxy::TraceSwitch(uintptr_t pc, uint64_t val, uint64_t *cases) {
  // __sanitizer_cov_trace_switch should not be called by Coverage.
  FX_NOTREACHED();
}

bool FakeSanitizerCovProxy::HasInit(size_t length) {
  return FakeSanitizerCovProxy::GetInstance()->HasInitImpl(length);
}

bool FakeSanitizerCovProxy::HasInitImpl(size_t length) {
  std::lock_guard<std::mutex> lock(lock_);
  return std::find(inits_.begin(), inits_.end(), length) != inits_.end();
}

size_t FakeSanitizerCovProxy::Count(uint64_t type, uint64_t pc, uint64_t arg0, uint64_t arg1) {
  return FakeSanitizerCovProxy::GetInstance()->CountImpl(type, pc, arg0, arg1);
}

size_t FakeSanitizerCovProxy::CountImpl(uint64_t type, uint64_t pc, uint64_t arg0, uint64_t arg1) {
  std::lock_guard<std::mutex> lock(lock_);
  size_t count = 0;
  for (auto i = traces_.begin(); i != traces_.end();) {
    uint64_t actual_type = *i++;
    uint64_t actual_pc = *i++;
    uint64_t actual_arg0 = *i++;
    uint64_t actual_arg1 = *i++;
    // Ignore the distinguisher for testing.
    actual_pc = (actual_pc << 16) >> 16;
    if (type == actual_type && pc == actual_pc && arg0 == actual_arg0 && arg1 == actual_arg1) {
      ++count;
    }
  }
  return count;
}

void FakeSanitizerCovProxy::Reset() { FakeSanitizerCovProxy::GetInstance()->ResetImpl(); }

void FakeSanitizerCovProxy::ResetImpl() {
  std::lock_guard<std::mutex> lock(lock_);
  inits_.clear();
  traces_.clear();
}

}  // namespace fuzzing

#define SANITIZER_COV_PROXY FakeSanitizerCovProxy
#define GET_CALLER_PC() GetRemotePC()

// Generates an implmentation of the __sanitizer_cov_* interface that logs calls and uses fake PCs
// instead of real ones.
#include "sanitizer-cov.inc"

#undef SANITIZER_COV_PROXY
#undef GET_CALLER_PC
