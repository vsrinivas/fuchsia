// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <string>

#include <gtest/gtest.h>
#include <src/lib/fxl/command_line.h>
#include "src/lib/files/file.h"
#include <src/lib/fxl/log_settings.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "garnet/bin/cpuperf/session_result_spec.h"
#include "garnet/bin/cpuperf/session_spec.h"
#include "garnet/lib/perfmon/file_reader.h"

#include "verify_test.h"

#if defined(__x86_64__)
#include "intel/intel_tests.h"
#elif defined(__aarch64__)
#include "arm64/arm64_tests.h"
#endif

bool Verifier::VerifyIteration(uint32_t iter) {
  auto get_file_name = [this, &iter] (uint32_t trace_num) -> std::string {
      return session_result_spec_->GetTraceFilePath(iter, trace_num);
  };

  std::unique_ptr<perfmon::FileReader> reader;
  if (!perfmon::FileReader::Create(get_file_name,
                                   session_result_spec_->num_traces,
                                   &reader)) {
    return false;
  }

  constexpr uint32_t kCurrentTraceUnset = ~0;
  uint32_t current_trace = kCurrentTraceUnset;
  RecordCounts counts{};

  uint32_t trace;
  perfmon::SampleRecord record;
  perfmon::ReaderStatus status;
  while ((status = reader->ReadNextRecord(&trace, &record)) ==
         perfmon::ReaderStatus::kOk) {
    if (trace != current_trace) {
      current_trace = trace;
    }

    switch (record.type()) {
    case perfmon::kRecordTypeTime:
      ++counts.time_records;
      break;
    case perfmon::kRecordTypeTick:
      ++counts.tick_records;
      break;
    case perfmon::kRecordTypeCount:
      ++counts.count_records;
      break;
    case perfmon::kRecordTypeValue:
      ++counts.value_records;
      break;
    case perfmon::kRecordTypePc:
      ++counts.pc_records;
      break;
    case perfmon::kRecordTypeLastBranch:
      ++counts.last_branch_records;
      break;
    default:
      // The reader shouldn't be returning records of unknown types.
      // But rather than FXL_DCHECK which will terminate the test, just
      // flag an error.
      FXL_LOG(ERROR) << "Unknown record type: " << record.type()
                     << ", trace " << current_trace << ", offset "
                     << reader->GetLastRecordOffset();
      // Don't keep reading, we don't know what size the record is.
      return false;
    }

    if (!VerifyRecord(record)) {
      FXL_LOG(ERROR) << "Record verification failed: trace " << current_trace
                     << ", offset " << reader->GetLastRecordOffset();
      // If one record is wrong there could a lot of them, reducing the
      // S/N ratio of the output. So just bail.
      return false;
    }
  }

  FXL_LOG(INFO)
    << fxl::StringPrintf("Counts: %zu time, %zu tick",
                         counts.time_records, counts.tick_records);
  FXL_LOG(INFO)
    << fxl::StringPrintf("Counts: %zu count, %zu value",
                         counts.count_records, counts.value_records);
  FXL_LOG(INFO)
    << fxl::StringPrintf("Counts: %zu pc",
                         counts.pc_records);

  if (status != perfmon::ReaderStatus::kNoMoreRecords) {
    FXL_LOG(ERROR) << "Error occurred in record reader: "
                   << perfmon::ReaderStatusToString(status);
    return false;
  }

  return VerifyTrace(counts);
}

void Verifier::Verify() {
  for (size_t iter = 0;
       iter < session_result_spec_->num_iterations;
       ++iter) {
    FXL_LOG(INFO) << "Verifying iteration " << iter;
    EXPECT_TRUE(VerifyIteration(iter));
  }
}

bool Verifier::LookupEventByName(const char* group_name,
                                 const char* event_name,
                                 const perfmon::EventDetails** out_details) {
  GetModelEventManager();
  return model_event_manager_->LookupEventByName(group_name, event_name,
                                                 out_details);
}

void Verifier::GetModelEventManager() {
  if (!model_event_manager_) {
    model_event_manager_ = perfmon::ModelEventManager::Create(
      session_result_spec_->model_name);
    ASSERT_TRUE(model_event_manager_);
  }
}

static std::unique_ptr<Verifier> LookupVerifier(
    const cpuperf::SessionResultSpec* spec) {
  for (size_t i = 0; i < kTestSpecCount; ++i) {
    const TestSpec* test = kTestSpecs[i];
    if (strcmp(spec->config_name.c_str(), test->config_name) == 0) {
      return test->make_verifier(spec);
    }
  }
  return nullptr;
}

void VerifySpec(const std::string& spec_file_path) {
  FXL_VLOG(1) << "Verifying " << spec_file_path;

  std::string content;
  ASSERT_TRUE(files::ReadFileToString(spec_file_path, &content));
  cpuperf::SessionSpec session_spec;
  ASSERT_TRUE(cpuperf::DecodeSessionSpec(content, &session_spec));

  ASSERT_TRUE(files::ReadFileToString(session_spec.session_result_spec_path,
                                      &content));
  cpuperf::SessionResultSpec session_result_spec;
  ASSERT_TRUE(cpuperf::DecodeSessionResultSpec(content, &session_result_spec));

  std::unique_ptr<Verifier> verifier = LookupVerifier(&session_result_spec);
  ASSERT_TRUE(verifier);
  verifier->Verify();
}
