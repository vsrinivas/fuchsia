// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>

#include "intel_tests.h"

class OsFlagVerifier : public Verifier {
 public:
  static std::unique_ptr<Verifier> Create(
      const cpuperf::SessionResultSpec* spec) {
    return std::make_unique<OsFlagVerifier>(spec);
  }

  OsFlagVerifier(const cpuperf::SessionResultSpec* spec)
      : Verifier(spec) {
  }

 private:
  bool VerifyRecord(const perfmon::SampleRecord& record) override {
    // IWBN to verify we got a kernel pc here, but that doesn't always
    // happen. There can be slippage to the time the event is reported.
    return true;
  }

  bool VerifyTrace(const RecordCounts& counts) override {
    if (counts.value_records != 0) {
      FXL_LOG(ERROR) << "Found unexpected value records";
      return false;
    }
    if (counts.pc_records == 0) {
      FXL_LOG(ERROR) << "Expected pc records, none present";
      return false;
    }
    return true;
  }
};

const TestSpec kOsFlagSpec = {
  "os-flag",
  &OsFlagVerifier::Create,
};
