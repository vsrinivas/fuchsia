// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PROVIDER_READER_H_
#define GARNET_BIN_CPUPERF_PROVIDER_READER_H_

#include <zircon/device/cpu-trace/intel-pm.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include "lib/fxl/macros.h"

namespace cpuperf_provider {

// TODO(dje): Temporary while sequencing patches across zircon+garnet.
#if IPM_API_VERSION < 2

typedef enum {
  // Reserved, unused.
  IPM_RECORD_RESERVED = 0,
  // The record is an |zx_x86_ipm_tick_record_t|.
  IPM_RECORD_TICK = 1,
  // The record is an |zx_x86_ipm_value_record_t|.
  IPM_RECORD_VALUE = 2,
  // The record is an |zx_x86_ipm_pc_record_t|.
  IPM_RECORD_PC = 3,
} zx_x86_ipm_record_type_t;

typedef struct {
    uint8_t type;

    // A possible usage of this field is to add some type-specific flags.
    uint8_t reserved_flags;

    uint16_t counter;
// OR'd to the value in |counter| to indicate a fixed counter.
#define IPM_COUNTER_NUMBER_FIXED 0x100

    // TODO(dje): Remove when |time| becomes 32 bits.
    uint32_t reserved;

    // TODO(dje): Reduce this to 32 bits (e.g., by adding clock records to
    // the buffer).
    zx_time_t time;
} zx_x86_ipm_record_header_t;

#endif

class Reader {
public:
  // When reading sample data, the record we read is one of these.
  union SampleRecord {
    zx_x86_ipm_record_header_t header;
#if IPM_API_VERSION >= 2
    zx_x86_ipm_tick_record_t tick;
    zx_x86_ipm_pc_record_t pc;
#endif

    // Ideally this would return the enum type, but we don't make any
    // assumptions about the validity of the trace data.
    uint32_t type() const { return header.type; }
    uint32_t counter() const { return header.counter; }
    zx_time_t time() const { return header.time; }
  };

  Reader(int fd, uint32_t buffer_size);

  bool is_valid() { return vmar_.is_valid(); }

  uint32_t num_cpus() const { return num_cpus_; }

  // The returned value is zero until the first call to ReadNextRecord(),
  // after which it contains the value used by the trace.
  uint64_t ticks_per_second() const { return ticks_per_second_; }

  bool ReadState(zx_x86_ipm_state_t* state);

  bool ReadPerfConfig(zx_x86_ipm_perf_config_t* config);

  bool ReadNextRecord(uint32_t* cpu, zx_x86_ipm_counters_t* counters);

  // Note: The returned value in |*ticks_per_second| could be bogus, including
  // zero. We just pass on what the trace told us.
  bool ReadNextRecord(uint32_t* cpu, uint64_t* ticks_per_second,
                      SampleRecord* record);

  // Returns IPM_RECORD_RESERVED for an invalid record type.
  static zx_x86_ipm_record_type_t RecordType(
      const zx_x86_ipm_record_header_t* hdr);

  // Returns 0 for an invalid record type.
  static size_t RecordSize(const zx_x86_ipm_record_header_t* hdr);

private:
  bool MapBufferVmo(zx_handle_t vmo);

  int fd_; // borrowed
  uint32_t buffer_size_;
  const uint32_t num_cpus_;
  uint32_t current_cpu_ = 0;

  // Note: The following are only used in sampling mode.
  zx::vmar vmar_;
  zx::vmo current_vmo_;
  const uint8_t* buffer_start_ = nullptr;
  const uint8_t* next_record_ = nullptr;
  const uint8_t* capture_end_ = nullptr;

  // Reading of one trace can span multiple cpus, and the ticks-per-second
  // value comes from each cpu's trace. Generally it's all the same value,
  // but there is no uber record to specify that. zx_ticks_per_second() will
  // return a constant value (though not necessarily the same value on each
  // boot), and it's this value we expect in the trace. OTOH, we use what
  // the trace buffer gives us. We don't want each record to encode its own
  // value, so keep track of the value here.
  uint64_t ticks_per_second_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(Reader);
};

}  // namespace cpuperf_provider

#endif  // GARNET_BIN_CPUPERF_PROVIDER_READER_H_
