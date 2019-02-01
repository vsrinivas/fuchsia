// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_CPUPERF_RECORDS_H_
#define GARNET_LIB_CPUPERF_RECORDS_H_

#include <cstddef>
#include <cstdint>

#include <lib/zircon-internal/device/cpu-trace/cpu-perf.h>

namespace cpuperf {

// When reading sample data, the record we read is one of these.
// To avoid unnecessary copying of the larger records we just return a
// pointer to the record, which will remain valid until the next record
// is read.
union SampleRecord {
  const cpuperf_record_header_t* header;
  const cpuperf_time_record_t* time;
  const cpuperf_tick_record_t* tick;
  const cpuperf_count_record_t* count;
  const cpuperf_value_record_t* value;
  const cpuperf_pc_record_t* pc;
  const cpuperf_last_branch_record_t* last_branch;

  // Ideally this would return the enum type, but we don't make any
  // assumptions about the validity of the trace data.
  uint8_t type() const { return header->type; }
  cpuperf_event_id_t event() const { return header->event; }
};

// Returns IPM_RECORD_RESERVED for an invalid record type.
cpuperf_record_type_t RecordType(const cpuperf_record_header_t* hdr);

// Returns 0 for an invalid record type or invalid record.
size_t RecordSize(const cpuperf_record_header_t* hdr);

}  // namespace cpuperf

#endif  // GARNET_LIB_CPUPERF_RECORDS_H_
