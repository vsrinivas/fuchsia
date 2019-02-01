// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/logging.h>

#include "garnet/lib/cpuperf/events.h"

#include "verify_test.h"

class ValueRecordsVerifier : public Verifier {
 public:
  static std::unique_ptr<Verifier> Create(
      const cpuperf::SessionResultSpec* spec) {
    return std::make_unique<ValueRecordsVerifier>(spec);
  }

  ValueRecordsVerifier(const cpuperf::SessionResultSpec* spec)
      : Verifier(spec) {
    const cpuperf::EventDetails* details;

    bool rc __UNUSED =
      cpuperf::LookupEventByName("misc", "edram_temperature", &details);
    FXL_DCHECK(rc);
    edram_temperature_id_ = details->id;

    rc = cpuperf::LookupEventByName("misc", "package_temperature", &details);
    FXL_DCHECK(rc);
    package_temperature_id_ = details->id;

    rc = cpuperf::LookupEventByName("misc", "ia_temperature", &details);
    FXL_DCHECK(rc);
    ia_temperature_id_ = details->id;

    rc = cpuperf::LookupEventByName("misc", "gt_temperature", &details);
    FXL_DCHECK(rc);
    gt_temperature_id_ = details->id;
  }

 private:
  bool VerifyRecord(const cpuperf::SampleRecord& record) override {
    if (record.header->event == edram_temperature_id_) {
      ++edram_temperature_count_;
    } else if (record.header->event == package_temperature_id_) {
      ++package_temperature_count_;
    } else if (record.header->event == ia_temperature_id_) {
      ++ia_temperature_count_;
    } else if (record.header->event == gt_temperature_id_) {
      ++gt_temperature_count_;
    }
    return true;
  }

  bool VerifyTrace(const RecordCounts& counts) override {
    bool pass = true;
    if (edram_temperature_count_ == 0) {
      FXL_LOG(ERROR) << "Missing edram_temperature events";
      pass = false;
    }
    if (package_temperature_count_ == 0) {
      FXL_LOG(ERROR) << "Missing package_temperature events";
      pass = false;
    }
    if (ia_temperature_count_ == 0) {
      FXL_LOG(ERROR) << "Missing ia_temperature events";
      pass = false;
    }
    if (gt_temperature_count_ == 0) {
      FXL_LOG(ERROR) << "Missing gt_temperature events";
      pass = false;
    }
    return pass;
  }

  // Ids of the events we should see.
  cpuperf_event_id_t edram_temperature_id_;
  cpuperf_event_id_t package_temperature_id_;
  cpuperf_event_id_t ia_temperature_id_;
  cpuperf_event_id_t gt_temperature_id_;

  // Counts of the events we should see;
  size_t edram_temperature_count_ = 0;
  size_t package_temperature_count_ = 0;
  size_t ia_temperature_count_ = 0;
  size_t gt_temperature_count_ = 0;
};

const TestSpec kValueRecordsSpec = {
  "value-records",
  &ValueRecordsVerifier::Create,
};
