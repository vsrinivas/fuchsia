// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_CPUPERF_READER_H_
#define GARNET_LIB_CPUPERF_READER_H_

#include <zircon/device/cpu-trace/cpu-perf.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include "lib/fxl/macros.h"

namespace cpuperf {

class Reader {
public:
  // When reading sample data, the record we read is one of these.
  union SampleRecord {
    cpuperf_record_header_t header;
    cpuperf_tick_record_t tick;
    cpuperf_count_record_t count;
    cpuperf_value_record_t value;
    cpuperf_pc_record_t pc;

    // Ideally this would return the enum type, but we don't make any
    // assumptions about the validity of the trace data.
    uint8_t type() const { return header.type; }
    cpuperf_event_id_t event() const { return header.event; }
    zx_time_t time() const { return header.time; }
  };

  // |fd| is borrowed.
  Reader(int fd, uint32_t buffer_size);

  bool is_valid() { return vmar_.is_valid(); }

  uint32_t num_cpus() const { return num_cpus_; }

  // The returned value is zero until the first call to ReadNextRecord(),
  // after which it contains the value used by the trace.
  uint64_t ticks_per_second() const { return ticks_per_second_; }

  bool GetProperties(cpuperf_properties_t* props);

  bool GetConfig(cpuperf_config_t* config);

  // Note: The returned value in |*ticks_per_second| could be bogus, including
  // zero. We just pass on what the trace told us.
  bool ReadNextRecord(uint32_t* cpu, uint64_t* ticks_per_second,
                      SampleRecord* record);

  // Returns IPM_RECORD_RESERVED for an invalid record type.
  static cpuperf_record_type_t RecordType(const cpuperf_record_header_t* hdr);

  // Returns 0 for an invalid record type.
  static size_t RecordSize(const cpuperf_record_header_t* hdr);

private:
  bool MapBufferVmo(zx_handle_t vmo);

  int fd_; // borrowed
  const uint32_t buffer_size_;
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

}  // namespace cpuperf

#endif  // GARNET_LIB_CPUPERF_READER_H_
