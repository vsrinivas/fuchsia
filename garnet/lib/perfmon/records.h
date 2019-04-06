// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_RECORDS_H_
#define GARNET_LIB_PERFMON_RECORDS_H_

#include <cstddef>
#include <cstdint>

#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>

namespace perfmon {

// When reading sample data, the record we read is one of these.
// To avoid unnecessary copying of the larger records we just return a
// pointer to the record, which will remain valid until the next record
// is read.
union SampleRecord {
  const RecordHeader* header;
  const TimeRecord* time;
  const TickRecord* tick;
  const CountRecord* count;
  const ValueRecord* value;
  const PcRecord* pc;
  const LastBranchRecord* last_branch;

  // Ideally this would return the enum type, but we don't make any
  // assumptions about the validity of the trace data.
  uint8_t type() const { return header->type; }
  EventId event() const { return header->event; }
};

// Returns |kRecordTypeInvalid| for an invalid record type.
RecordType GetRecordType(const RecordHeader* hdr);

// Returns 0 for an invalid record type or invalid record.
size_t GetRecordSize(const RecordHeader* hdr);

}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_RECORDS_H_
