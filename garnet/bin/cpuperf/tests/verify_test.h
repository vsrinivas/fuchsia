// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_TESTS_VERIFY_TEST_H_
#define GARNET_BIN_CPUPERF_TESTS_VERIFY_TEST_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "garnet/bin/cpuperf/session_result_spec.h"
#include "garnet/lib/perfmon/events.h"
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
      : session_result_spec_(session_result_spec) {}

  virtual bool VerifyRecord(const perfmon::SampleRecord& record) = 0;
  virtual bool VerifyTrace(const RecordCounts& counts) = 0;

  // Kernel pcs are guaranteed to have this bit set.
  // Userspace pcs are guaranteed to not have this bit set.
  static constexpr uint64_t kKernelPcMask = 1ul << 63;
  static bool IsKernelPc(uint64_t pc) { return !!(pc & kKernelPcMask); }
  static bool IsUserPc(uint64_t pc) { return !(pc & kKernelPcMask); }

  // Wrappers on ModelEventManager that first we ensure we have a model
  // event manager.
  bool LookupEventByName(const char* group_name, const char* event_name,
                         const perfmon::EventDetails** out_details);

  const cpuperf::SessionResultSpec* const session_result_spec_;

 private:
  void GetModelEventManager();

  std::unique_ptr<perfmon::ModelEventManager> model_event_manager_;
};

using MakeVerifier = std::unique_ptr<Verifier>(const cpuperf::SessionResultSpec* spec);

struct TestSpec {
  const char* config_name;
  MakeVerifier* make_verifier;
};

// These are provided by each arch's subdirectory of tests.
extern const TestSpec* const kTestSpecs[];
extern const size_t kTestSpecCount;

// Common routine for verifying the result of a test run.
// |spec_file_path| is the path to the cpspec file.
void VerifySpec(const std::string& spec_file_path);

#endif  // GARNET_BIN_CPUPERF_TESTS_VERIFY_TEST_H_
