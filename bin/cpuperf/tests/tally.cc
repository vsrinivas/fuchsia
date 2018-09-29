// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/logging.h>

#include "garnet/lib/cpuperf/events.h"

#include "verify_test.h"

class TallyVerifier : public Verifier {
 public:
  static std::unique_ptr<Verifier> Create(
      const cpuperf::SessionResultSpec* spec) {
    return std::make_unique<TallyVerifier>(spec);
  }

  TallyVerifier(const cpuperf::SessionResultSpec* spec)
      : Verifier(spec) {
    const cpuperf::EventDetails* details;

    bool rc __UNUSED =
      cpuperf::LookupEventByName("fixed", "instructions_retired", &details);
    FXL_DCHECK(rc);
    instructions_retired_id_ = details->id;
  }

 private:
  bool VerifyRecord(const cpuperf::SampleRecord& record) override {
    if (record.header->event == instructions_retired_id_) {
      ++instructions_retired_count_;
    }
    return true;
  }

  bool VerifyTrace(const RecordCounts& counts) override {
    bool pass = true;
    if (instructions_retired_count_ == 0) {
      FXL_LOG(ERROR) << "Missing instructions_retired events";
      pass = false;
    } else if (instructions_retired_count_ > 1) {
      FXL_LOG(ERROR) << "Too many instructions_retired events";
      pass = false;
    }
    return pass;
  }

  // Ids of the events we should see.
  cpuperf_event_id_t instructions_retired_id_;

  // Counts of the events we should see;
  size_t instructions_retired_count_ = 0;
};

const TestSpec kTallySpec = {
  "tally",
  &TallyVerifier::Create,
};
