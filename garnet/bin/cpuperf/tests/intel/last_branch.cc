// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <src/lib/fxl/logging.h>

#include "garnet/lib/perfmon/events.h"

#include "intel_tests.h"

class LastBranchVerifier : public Verifier {
 public:
  static std::unique_ptr<Verifier> Create(
      const cpuperf::SessionResultSpec* spec) {
    return std::make_unique<LastBranchVerifier>(spec);
  }

  LastBranchVerifier(const cpuperf::SessionResultSpec* spec)
      : Verifier(spec) {
    const perfmon::EventDetails* details;

    bool rc __UNUSED =
      LookupEventByName("arch", "instructions_retired", &details);
    FXL_DCHECK(rc);
    instructions_retired_id_ = details->id;
  }

 private:
  bool VerifyRecord(const perfmon::SampleRecord& record) override {
    if (record.header->event == instructions_retired_id_) {
      ++instructions_retired_count_;
    }
    if (record.type() == PERFMON_RECORD_LAST_BRANCH) {
      ++last_branch_record_count_;
      if (record.header->event != instructions_retired_id_) {
        FXL_LOG(ERROR) << "Last branch record has wrong event id: "
                       << record.header->event;
        return false;
      }
      const perfmon_last_branch_record_t* lbr = record.last_branch;
      uint64_t valid_info_mask = (PERFMON_LAST_BRANCH_INFO_CYCLES_MASK |
                                  PERFMON_LAST_BRANCH_INFO_MISPRED_MASK);
      for (size_t i = 0; i < lbr->num_branches; ++i) {
        if (lbr->aspace == 0) {
          FXL_LOG(ERROR) << "Last branch record has zero aspace";
          return false;
        }
        if (lbr->num_branches > PERFMON_MAX_NUM_LAST_BRANCH) {
          FXL_LOG(ERROR) << "Last branch record has too many branches";
          return false;
        }
        // Maybe branch to zero could end up here, so we don't verify from,to
        // are non-zero, but they both can't be.
        if (lbr->branches[i].from == 0 && lbr->branches[i].to == 0) {
          FXL_LOG(ERROR) << "Last branch record with zero from,to";
          return false;
        }
        if (lbr->branches[i].info & !valid_info_mask) {
          FXL_LOG(ERROR) << "Last branch record has invalid info bits";
          return false;
        }
      }
    }
    return true;
  }

  bool VerifyTrace(const RecordCounts& counts) override {
    bool pass = true;
    if (instructions_retired_count_ == 0) {
      FXL_LOG(ERROR) << "Missing instructions_retired events";
      pass = false;
    }
    if (last_branch_record_count_ == 0) {
      FXL_LOG(ERROR) << "Missing last-branch records events";
      pass = false;
    }
    return pass;
  }

  // Ids of the events we should see.
  perfmon::EventId instructions_retired_id_;

  // Counts of the events we should see.
  size_t instructions_retired_count_ = 0;
  size_t last_branch_record_count_ = 0;
};

const TestSpec kLastBranchSpec = {
  "last-branch",
  &LastBranchVerifier::Create,
};
