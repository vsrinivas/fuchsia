// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>

#include "garnet/lib/perfmon/events.h"

#include "intel_tests.h"

class TallyVerifier : public Verifier {
 public:
  static std::unique_ptr<Verifier> Create(
      const cpuperf::SessionResultSpec* spec) {
    return std::make_unique<TallyVerifier>(spec);
  }

  TallyVerifier(const cpuperf::SessionResultSpec* spec)
      : Verifier(spec) {
    const perfmon::EventDetails* details;

    bool rc __UNUSED =
      LookupEventByName("fixed", "instructions_retired", &details);
    FXL_DCHECK(rc);
    instructions_retired_id_ = details->id;
  }

 private:
  bool VerifyRecord(const perfmon::SampleRecord& record) override {
    if (record.header->event == instructions_retired_id_) {
      ++instructions_retired_count_;
    }
    return true;
  }

  bool VerifyTrace(const RecordCounts& counts) override {
    bool pass = true;
    // In tally mode there is only one set of records emitted, at the end.
    // Therefore we should have one record per trace (IOW per #cpus).
    if (instructions_retired_count_ != session_result_spec_->num_traces) {
      FXL_LOG(ERROR) << "Wrong number of instructions_retired events: "
                     << instructions_retired_count_;
      pass = false;
    }
    return pass;
  }

  // Ids of the events we should see.
  perfmon_event_id_t instructions_retired_id_;

  // Counts of the events we should see;
  size_t instructions_retired_count_ = 0;
};

const TestSpec kTallySpec = {
  "tally",
  &TallyVerifier::Create,
};
