// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_number_conversions.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "garnet/lib/perfmon/file_reader.h"

#include "raw_printer.h"

namespace cpuperf {

bool RawPrinter::Create(
    const SessionResultSpec* session_result_spec, const Config& config,
    std::unique_ptr<RawPrinter>* out_printer) {
  const std::string& output_file_name = config.output_file_name;
  FILE* out_file = stdout;
  if (output_file_name != "") {
    out_file = fopen(output_file_name.c_str(), "w");
    if (!out_file) {
      FXL_LOG(ERROR) << "Unable to open file for writing: "
                     << output_file_name;
      return false;
    }
  }

  out_printer->reset(new RawPrinter(out_file, session_result_spec, config));
  return true;
}

RawPrinter::RawPrinter(FILE* out_file,
                       const SessionResultSpec* session_result_spec,
                       const Config& config)
    : out_file_(out_file),
      session_result_spec_(session_result_spec),
      config_(config) {}

RawPrinter::~RawPrinter() {
  if (config_.output_file_name != "")
    fclose(out_file_);
}

// TODO(dje): attribute format
void RawPrinter::Printf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(out_file_, format, args);
  va_end(args);
}

void RawPrinter::PrintHeader(const perfmon::SampleRecord& record) {
  // There's no need to print the type here, caller does that.
  Printf("Event 0x%x", record.header->event);
}

void RawPrinter::PrintTimeRecord(const perfmon::SampleRecord& record) {
  Printf("Time: %" PRIu64 "\n", record.time->time);
}

void RawPrinter::PrintTickRecord(const perfmon::SampleRecord& record) {
  Printf("Tick: ");
  PrintHeader(record);
  Printf("\n");
}

void RawPrinter::PrintCountRecord(const perfmon::SampleRecord& record) {
  Printf("Count: ");
  PrintHeader(record);
  Printf(", %" PRIu64 "\n", record.count->count);
}

void RawPrinter::PrintValueRecord(const perfmon::SampleRecord& record) {
  Printf("Value: ");
  PrintHeader(record);
  Printf(", %" PRIu64 "\n", record.value->value);
}

void RawPrinter::PrintPcRecord(const perfmon::SampleRecord& record) {
  Printf("PC: ");
  PrintHeader(record);
  Printf(", aspace 0x%" PRIx64 ", pc 0x%" PRIx64 "\n",
         record.pc->aspace, record.pc->pc);
}

void RawPrinter::PrintLastBranchRecord(const perfmon::SampleRecord& record) {
  Printf("LastBranch: ");
  PrintHeader(record);
  Printf(", aspace 0x%" PRIx64 ", %u branches\n",
         record.last_branch->aspace, record.last_branch->num_branches);
  // TODO(dje): Print each branch, but it's a lot so maybe only if verbose?
}

uint64_t RawPrinter::PrintOneTrace(uint32_t iter_num) {
  uint64_t total_records = 0;

  auto get_file_name = [this, &iter_num] (uint32_t trace_num) -> std::string {
    return session_result_spec_->GetTraceFilePath(iter_num, trace_num);
  };

  std::unique_ptr<perfmon::FileReader> reader;
  if (!perfmon::FileReader::Create(get_file_name,
                          session_result_spec_->num_traces,
                          &reader)) {
    return 0;
  }

  uint32_t current_trace = ~0;

  uint32_t trace;
  perfmon::SampleRecord record;
  while (reader->ReadNextRecord(&trace, &record) == perfmon::ReaderStatus::kOk) {
    ++total_records;

    if (trace != current_trace) {
      current_trace = trace;
      Printf("\nTrace %u\n", current_trace);
      // No, the number of -s doesn't line up, it's close enough.
      Printf("--------\n");
    }

    Printf("%04zx: ", reader->GetLastRecordOffset());

    switch (record.type()) {
    case PERFMON_RECORD_TIME:
      PrintTimeRecord(record);
      break;
    case PERFMON_RECORD_TICK:
      PrintTickRecord(record);
      break;
    case PERFMON_RECORD_COUNT:
      PrintCountRecord(record);
      break;
    case PERFMON_RECORD_VALUE:
      PrintValueRecord(record);
      break;
    case PERFMON_RECORD_PC:
      PrintPcRecord(record);
      break;
    case PERFMON_RECORD_LAST_BRANCH:
      PrintLastBranchRecord(record);
      break;
    default:
      // The reader shouldn't be returning unknown records.
      FXL_NOTREACHED();
      break;
    }
  }

  return total_records;
}

uint64_t RawPrinter::PrintFiles() {
  uint64_t total_records = 0;

  for (uint32_t iter = 0;
       iter < session_result_spec_->num_iterations;
       ++iter) {
    Printf("\nIteration %u\n", iter);
    // No, the number of =s doesn't line up, it's close enough.
    Printf("==============\n");
    total_records += PrintOneTrace(iter);
  }

  Printf("\n");

  return total_records;
}

}  // namespace cpuperf
