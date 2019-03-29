// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>

#include "garnet/lib/perfmon/events.h"

#include "intel_tests.h"

class FixedCounterVerifier : public Verifier {
 public:
  static std::unique_ptr<Verifier> Create(
      const cpuperf::SessionResultSpec* spec) {
    return std::make_unique<FixedCounterVerifier>(spec);
  }

  FixedCounterVerifier(const cpuperf::SessionResultSpec* spec)
      : Verifier(spec) {
    const perfmon::EventDetails* details;

    bool rc __UNUSED =
      LookupEventByName("fixed", "instructions_retired", &details);
    FXL_DCHECK(rc);
    instructions_retired_id_ = details->id;

    rc = LookupEventByName("fixed", "unhalted_core_cycles", &details);
    FXL_DCHECK(rc);
    unhalted_core_cycles_id_ = details->id;

    rc = LookupEventByName("fixed", "unhalted_reference_cycles", &details);
    FXL_DCHECK(rc);
    unhalted_reference_cycles_id_ = details->id;
  }

 private:
  bool VerifyRecord(const perfmon::SampleRecord& record) override {
    if (record.header->event == instructions_retired_id_) {
      ++instructions_retired_count_;
    } else if (record.header->event == unhalted_core_cycles_id_) {
      ++unhalted_core_cycles_count_;
    } else if (record.header->event == unhalted_reference_cycles_id_) {
      ++unhalted_reference_cycles_count_;
    }
    return true;
  }

  bool VerifyTrace(const RecordCounts& counts) override {
    bool pass = true;
    if (instructions_retired_count_ == 0) {
      FXL_LOG(ERROR) << "Missing instructions_retired events";
      pass = false;
    }
    if (unhalted_core_cycles_count_ == 0) {
      FXL_LOG(ERROR) << "Missing unhalted_core_cycles events";
      pass = false;
    }
    if (unhalted_reference_cycles_count_ == 0) {
      FXL_LOG(ERROR) << "Missing unhalted_reference_cycles events";
      pass = false;
    }
    return pass;
  }

  // Ids of the events we should see.
  perfmon_event_id_t instructions_retired_id_;
  perfmon_event_id_t unhalted_core_cycles_id_;
  perfmon_event_id_t unhalted_reference_cycles_id_;

  // Counts of the events we should see;
  size_t instructions_retired_count_ = 0;
  size_t unhalted_core_cycles_count_ = 0;
  size_t unhalted_reference_cycles_count_ = 0;
};

const TestSpec kFixedCounterSpec = {
  "fixed-counters",
  &FixedCounterVerifier::Create,
};
