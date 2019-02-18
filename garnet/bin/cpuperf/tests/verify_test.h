// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_TESTS_VERIFY_TEST_H_
#define GARNET_BIN_CPUPERF_TESTS_VERIFY_TEST_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "garnet/bin/cpuperf/session_result_spec.h"
#include "garnet/lib/perfmon/records.h"

class Verifier {
 public:
  // Statistics on collected records.
  struct RecordCounts {
    size_t time_records;
    size_t tick_records;
    size_t count_records;
    size_t value_records;
    size_t pc_records;
    size_t last_branch_records;
  };

  virtual ~Verifier() = default;

  bool VerifyIteration(uint32_t iter);
  void Verify();

 protected:
  Verifier(const cpuperf::SessionResultSpec* session_result_spec)
      : session_result_spec_(session_result_spec) {
  }

  virtual bool VerifyRecord(const perfmon::SampleRecord& record) = 0;
  virtual bool VerifyTrace(const RecordCounts& counts) = 0;

  // Kernel pcs are guaranteed to have this bit set.
  // Userspace pcs are guaranteed to not have this bit set.
  static constexpr uint64_t kKernelPcMask = 1ul << 63;
  static bool IsKernelPc(uint64_t pc) { return !!(pc & kKernelPcMask); }
  static bool IsUserPc(uint64_t pc) { return !(pc & kKernelPcMask); }

  const cpuperf::SessionResultSpec* const session_result_spec_;
};

using MakeVerifier = std::unique_ptr<Verifier>(
    const cpuperf::SessionResultSpec* spec);

struct TestSpec {
  const char* config_name;
  MakeVerifier* make_verifier;
};

// These are defined in each testcase's file, and referenced by the
// testcase verifier.
extern const TestSpec kFixedCounterSpec;
extern const TestSpec kLastBranchSpec;
extern const TestSpec kOsFlagSpec;
extern const TestSpec kProgrammableCounterSpec;
extern const TestSpec kTallySpec;
extern const TestSpec kUserFlagSpec;
extern const TestSpec kValueRecordsSpec;

// Common routine for verifying the result of a test run.
// |spec_file_path| is the path to the cpspec file.
void VerifySpec(const std::string& spec_file_path);

#endif  // GARNET_BIN_CPUPERF_TESTS_VERIFY_TEST_H_
