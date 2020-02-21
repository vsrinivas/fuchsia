// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tracing.h"

#include <lib/zircon-internal/ktrace.h>

#include <fstream>

#include "src/lib/fxl/logging.h"

std::optional<KTraceRecord> KTraceRecord::ParseRecord(uint8_t* data_buf, size_t buf_len) {
  if (buf_len < KTRACE_HDRSIZE)
    return std::nullopt;

  KTraceRecord kr;

  ktrace_header_t* record = reinterpret_cast<ktrace_header_t*>(data_buf);

  if (buf_len < KTRACE_LEN(record->tag))
    return std::nullopt;

  kr.data_buf_len_ = buf_len;
  kr.is_named_ = KTRACE_FLAGS(record->tag);

  if (kr.is_named_) {
    kr.is_probe_group_ = KTRACE_GROUP(record->tag) & KTRACE_GRP_PROBE;
    kr.is_flow_ = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_FLOW;
    kr.is_begin_ = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_BEGIN;
    kr.is_end_ = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_END;
    kr.is_duration_ = !kr.is_flow_ && (kr.is_begin_ | kr.is_end_);

    // Beginning and end states are mutually exclusive.
    if (kr.is_begin_ && kr.is_end_) {
      return std::nullopt;
    }
  } else {
    kr.event_ = KTRACE_EVENT(record->tag);

    if (kr.event_ >= countof(kTags)) {
      kr.has_unexpected_event_ = true;
      return kr;
    }

    kr.info_ = &kTags[kr.event_];

    if (kr.info_->name == nullptr) {
      kr.has_unexpected_event_ = true;
      return kr;
    }
  }

  kr.rec_16b_ = record;

  return kr;
}

bool KTraceRecord::Get16BRecord(ktrace_header_t** record) const {
  // Incorrect record type.
  if (rec_16b_ == nullptr || KTRACE_LEN(rec_16b_->tag) > data_buf_len_ ||
      (!is_named_ && (info_->type == kTag32B || info_->type == kTagNAME))) {
    return false;
  }

  *record = rec_16b_;
  return true;
}

bool KTraceRecord::Get32BRecord(ktrace_rec_32b_t** record) const {
  // Incorrect record type.
  if (rec_16b_ == nullptr || KTRACE_LEN(rec_16b_->tag) > data_buf_len_ || is_named_ ||
      info_->type != kTag32B) {
    return false;
  }

  *record = reinterpret_cast<ktrace_rec_32b_t*>(rec_16b_);
  return true;
}

bool KTraceRecord::GetNameRecord(ktrace_rec_name_t** record) const {
  // Incorrect record type.
  if (rec_16b_ == nullptr || KTRACE_LEN(rec_16b_->tag) > data_buf_len_ || is_named_ ||
      info_->type != kTagNAME) {
    return false;
  }

  *record = reinterpret_cast<ktrace_rec_name_t*>(rec_16b_);
  return true;
}

std::optional<std::array<uint32_t, 2>> KTraceRecord::Get64BitPayload() const {
  // Incorrect record type.
  if (rec_16b_ == nullptr || KTRACE_LEN(rec_16b_->tag) > data_buf_len_ || !is_named_ ||
      KTRACE_LEN(rec_16b_->tag) != KTRACE_HDRSIZE + sizeof(uint32_t) * 2) {
    return std::nullopt;
  }

  return std::array<uint32_t, 2>{reinterpret_cast<const uint32_t*>(rec_16b_ + 1)[0],
                                 reinterpret_cast<const uint32_t*>(rec_16b_ + 1)[1]};
}

std::optional<std::array<uint64_t, 2>> KTraceRecord::Get128BitPayload() const {
  // Incorrect record type.
  if (rec_16b_ == nullptr || KTRACE_LEN(rec_16b_->tag) > data_buf_len_ || !is_named_ || is_flow_ ||
      KTRACE_LEN(rec_16b_->tag) != 32) {
    return std::nullopt;
  }

  return std::array<uint64_t, 2>{reinterpret_cast<const uint64_t*>(rec_16b_ + 1)[0],
                                 reinterpret_cast<const uint64_t*>(rec_16b_ + 1)[1]};
}

std::optional<const uint64_t> KTraceRecord::GetFlowID() const {
  // Incorrect record type.
  if (rec_16b_ == nullptr || KTRACE_LEN(rec_16b_->tag) > data_buf_len_ || !is_named_ || !is_flow_ ||
      KTRACE_LEN(rec_16b_->tag) != KTRACE_HDRSIZE + sizeof(uint64_t) * 2) {
    return std::nullopt;
  }

  return reinterpret_cast<const uint64_t*>(rec_16b_ + 1)[0];
}

std::optional<const uint64_t> KTraceRecord::GetAssociatedThread() const {
  // Incorrect record type.
  if (rec_16b_ == nullptr || !is_named_ || !is_flow_ || KTRACE_LEN(rec_16b_->tag) != 32) {
    return std::nullopt;
  }

  return reinterpret_cast<const uint64_t*>(rec_16b_ + 1)[1];
}

// Performs same action as zx_ktrace_read and does necessary checks.
void Tracing::ReadKernelBuffer(zx_handle_t handle, void* data_buf, uint32_t offset, size_t len,
                               size_t* bytes_read) {
  const auto status = zx_ktrace_read(handle, data_buf, offset, len, bytes_read);
  FXL_CHECK(status == ZX_OK) << "zx_ktrace_read failed.";
}

// Fetches record from kernel buffer if available. Returns false if errors were encountered.
// Returns two booleans, first signals whether read was successful, second signals whether entire
// kernel trace buffer has been read.
std::tuple<bool, bool> Tracing::FetchRecord(zx_handle_t handle, uint8_t* data_buf, uint32_t* offset,
                                            size_t* bytes_read, size_t buf_len) {
  if (buf_len < KTRACE_HDRSIZE) {
    FXL_LOG(ERROR) << "Data buffer too small.";
    return {false, false};
  }

  // Read record header.
  ReadKernelBuffer(handle, data_buf, *offset, KTRACE_HDRSIZE, bytes_read);

  // Try reading more before assuming error.
  if (*bytes_read < KTRACE_HDRSIZE) {
    // Compute updated values to continue reading trace buffer into data buffer.
    const size_t bytes_read_originally = *bytes_read;
    const uint32_t updated_offset = *offset + bytes_read_originally;
    const size_t remaining_len = KTRACE_HDRSIZE - bytes_read_originally;

    ReadKernelBuffer(handle, data_buf + bytes_read_originally, updated_offset, remaining_len,
                     bytes_read);

    // Record is incomplete because it is presumably at the end of the trace buffer. Update offset
    // and redo read to make sure this is actually the case.
    if (*bytes_read == 0) {
      *offset += bytes_read_originally;
      return {true, true};
    }

    *bytes_read += bytes_read_originally;
  }

  // Reading less bytes than defined by ktrace_header_t can lead to reading uninitialized memory.
  if (*bytes_read < KTRACE_HDRSIZE) {
    FXL_LOG(ERROR) << "Error reading traces, trace read stopped.";
    return {false, false};
  }

  ktrace_header_t* record = reinterpret_cast<ktrace_header_t*>(data_buf);

  // Make sure there's enough space in buffer.
  if (buf_len < KTRACE_LEN(record->tag)) {
    FXL_LOG(ERROR) << "Data buffer too small for payload.";
    return {false, false};
  }

  // If the record has zero length, something is wrong and the rest of the data will be junk.
  if (KTRACE_LEN(record->tag) == 0) {
    FXL_LOG(ERROR) << "Error reading traces, trace read stopped.";
    return {false, false};
  }

  // Read trace payload.
  if (KTRACE_LEN(record->tag) > *bytes_read) {
    // Compute updated values to read trace payload into data buffer.
    const size_t bytes_read_before_payload = *bytes_read;
    const size_t payload_len = KTRACE_LEN(record->tag) - *bytes_read;
    const size_t payload_offset = *offset + *bytes_read;

    ReadKernelBuffer(handle, data_buf + *bytes_read, payload_offset, payload_len, bytes_read);

    *bytes_read += bytes_read_before_payload;
  }

  *offset += *bytes_read;

  return {true, false};
}

// Rewinds kernel trace buffer.
void Tracing::Rewind() {
  const auto status = zx_ktrace_control(root_resource_, KTRACE_ACTION_REWIND, 0, nullptr);
  FXL_CHECK(status == ZX_OK) << "Failed to rewind kernel trace buffer.";
}

// Starts kernel tracing.
void Tracing::Start(uint32_t group_mask) {
  const auto status = zx_ktrace_control(root_resource_, KTRACE_ACTION_START, group_mask, nullptr);
  FXL_CHECK(status == ZX_OK) << "Failed to start tracing.";

  running_ = true;
}

// Stops kernel tracing.
void Tracing::Stop() {
  const auto status = zx_ktrace_control(root_resource_, KTRACE_ACTION_STOP, 0, nullptr);
  FXL_CHECK(status == ZX_OK) << "Failed to stop tracing.";

  running_ = false;
}

// Returns a string with human-readable translations of tag name, event, and any possible flags.
std::string Tracing::InterpretTag(const uint32_t tag, const TagDefinition& info) {
  uint32_t event = KTRACE_EVENT(tag);
  uint32_t flags = KTRACE_FLAGS(tag);
  std::stringstream output;
  output << info.name << "(0x" << std::hex << event << ")";

  if (flags != 0)
    output << ", flags 0x" << std::hex << flags;

  return output.str();
}

// Writes human-readable translation for 16 byte records into file specified by <file>.
void Tracing::Write16B(const KTraceRecord record, std::ostream* file) {
  ktrace_header_t* rec;

  if (!record.Get16BRecord(&rec)) {
    *file << "Malformed record.\n";
    return;
  }

  const TagDefinition info = *record.GetInfo();

  *file << std::dec << rec->ts << ": " << InterpretTag(rec->tag, info) << ", arg 0x" << std::hex
        << rec->tid << "\n";
}

// Writes human-readable translation for 32 byte records into file specified by <file>.
void Tracing::Write32B(const KTraceRecord record, std::ostream* file) {
  ktrace_rec_32b_t* rec;

  if (!record.Get32BRecord(&rec)) {
    *file << "Malformed record.\n";
    return;
  }

  const TagDefinition info = *record.GetInfo();

  *file << std::dec << rec->ts << ": " << InterpretTag(rec->tag, info) << ", tid 0x" << std::hex
        << rec->tid << ", a 0x" << std::hex << rec->a << ", b 0x" << std::hex << rec->b << ", c 0x"
        << std::hex << rec->c << ", d 0x" << std::hex << rec->d << "\n";
}

// Writes human-readable translation for name type records into file specified by <file>.
void Tracing::WriteName(const KTraceRecord record, std::ostream* file) {
  ktrace_rec_name_t* rec;

  if (!record.GetNameRecord(&rec)) {
    *file << "Malformed record.\n";
    return;
  }

  const TagDefinition info = *record.GetInfo();

  *file << InterpretTag(rec->tag, info) << ", id 0x" << std::hex << rec->id << ", arg 0x"
        << std::hex << rec->arg << ", " << rec->name << "\n";
}

// Writes human-readable translation for probe records into file specified by <file>.
void Tracing::WriteProbeRecord(const KTraceRecord record, std::ostream* file) {
  ktrace_header_t* rec;

  if (!record.Get16BRecord(&rec)) {
    *file << "Malformed record.\n";
    return;
  }

  const uint32_t event_name_id = KTRACE_EVENT_NAME_ID(rec->tag);
  const size_t record_len = KTRACE_LEN(rec->tag);

  if (record_len == KTRACE_HDRSIZE) {
    *file << "PROBE: tag 0x" << std::hex << TAG_PROBE_16(event_name_id) << ", event_name_id 0x"
          << event_name_id << ", tid 0x" << std::hex << rec->tid << ", ts " << rec->ts << "\n";
  } else if (record_len == KTRACE_HDRSIZE + sizeof(uint32_t) * 2) {
    const auto a = record.Get64BitPayload().value()[0];
    const auto b = record.Get64BitPayload().value()[1];
    *file << "PROBE: tag 0x" << std::hex << TAG_PROBE_24(event_name_id) << ", event_name_id 0x"
          << event_name_id << ", tid 0x" << std::hex << rec->tid << ", ts " << std::dec << rec->ts
          << ", a 0x" << std::hex << a << ", b 0x" << b << "\n";
  } else if (record_len == KTRACE_HDRSIZE + sizeof(uint64_t) * 2) {
    const auto a = record.Get128BitPayload().value()[0];
    const auto b = record.Get128BitPayload().value()[1];
    *file << "PROBE: tag 0x" << std::hex << TAG_PROBE_32(event_name_id) << ", event_name_id 0x"
          << event_name_id << ", tid 0x" << std::hex << rec->tid << ", ts " << std::dec << rec->ts
          << ", a 0x" << std::hex << a << ", b 0x" << b << "\n";
  } else {
    *file << "Unexpected tag: 0x" << std::hex << rec->tag << "\n";
  }
}

// Writes human-readable translation for probe records into file specified by <file>.
void Tracing::WriteDurationRecord(const KTraceRecord record, const EventState event_state,
                                  std::ostream* file) {
  ktrace_header_t* rec;

  if (!record.Get16BRecord(&rec)) {
    *file << "Malformed record.\n";
    return;
  }

  const size_t record_len = KTRACE_LEN(rec->tag);
  const uint32_t event_name_id = KTRACE_EVENT_NAME_ID(rec->tag);
  const uint32_t group = KTRACE_GROUP(rec->tag);

  if (record_len == KTRACE_HDRSIZE) {
    if (event_state == kBegin) {
      *file << std::dec << rec->ts << ": DURATION BEGIN: tag 0x" << std::hex
            << TAG_BEGIN_DURATION_16(event_name_id, group) << ", id 0x" << std::hex << event_name_id
            << ", tid 0x" << std::hex << rec->tid << "\n";
    } else if (event_state == kEnd) {
      *file << std::dec << rec->ts << ": DURATION END: tag 0x" << std::hex
            << TAG_END_DURATION_16(event_name_id, group) << ", id 0x" << std::hex << event_name_id
            << ", tid 0x" << std::hex << rec->tid << "\n";
    } else {
      *file << "Unexpected tag: 0x" << std::hex << rec->tag << "\n";
    }
  } else if (record_len == KTRACE_HDRSIZE + sizeof(uint64_t) * 2) {
    const auto a = record.Get128BitPayload().value()[0];
    const auto b = record.Get128BitPayload().value()[1];

    if (event_state == kBegin) {
      *file << std::dec << rec->ts << ": DURATION BEGIN: tag 0x" << std::hex
            << TAG_BEGIN_DURATION_32(event_name_id, group) << ", id 0x" << std::hex << event_name_id
            << ", tid 0x" << std::hex << rec->tid << ", a 0x" << std::hex << a << ", b 0x"
            << std::hex << b << "\n";
    } else if (event_state == kEnd) {
      *file << std::dec << rec->ts << ": DURATION END: tag 0x" << std::hex
            << TAG_END_DURATION_32(event_name_id, group) << ", id 0x" << std::hex << event_name_id
            << ", tid 0x" << std::hex << rec->tid << ", a 0x" << std::hex << a << ", b 0x"
            << std::hex << b << "\n";
    } else {
      *file << "Unexpected tag: 0x" << std::hex << rec->tag << "\n";
    }
  } else {
    *file << "Unexpected tag: 0x" << std::hex << rec->tag << "\n";
  }
}

// Writes human-readable translation for flow records into file specified by <file>.
void Tracing::WriteFlowRecord(const KTraceRecord record, const EventState event_state,
                              std::ostream* file) {
  ktrace_header_t* rec;

  if (!record.Get16BRecord(&rec)) {
    *file << "Malformed record.\n";
    return;
  }

  const size_t record_len = KTRACE_LEN(rec->tag);
  const uint32_t event_name_id = KTRACE_EVENT_NAME_ID(rec->tag);
  const uint32_t group = KTRACE_GROUP(rec->tag);

  if (record_len == KTRACE_HDRSIZE + sizeof(uint64_t) * 2) {
    const auto flow_id = record.GetFlowID().value();

    if (event_state == kBegin) {
      *file << std::dec << rec->ts << ": FLOW BEGIN: tag 0x" << std::hex
            << TAG_FLOW_BEGIN(event_name_id, group) << ", id 0x" << std::hex << event_name_id
            << ", tid 0x" << std::hex << rec->tid << ", flow id 0x" << std::hex << flow_id << "\n";
    } else if (event_state == kEnd) {
      *file << std::dec << rec->ts << ": FLOW END: tag 0x" << std::hex
            << TAG_FLOW_END(event_name_id, group) << ", id 0x" << std::hex << event_name_id
            << ", tid 0x" << std::hex << rec->tid << ", flow id 0x" << std::hex << flow_id << "\n";
    } else {
      *file << "Unexpected tag: 0x" << std::hex << rec->tag << "\n";
    }
  } else {
    *file << "Unexpected tag: 0x" << std::hex << rec->tag << "\n";
  }
}

// Reads trace buffer and converts output into human-readable format. Stores in location defined by
// <filepath>. Will overwrite any existing files with same name.
bool Tracing::WriteHumanReadable(std::ostream& human_readable_file) {
  if (running_) {
    FXL_LOG(WARNING) << "Tracing was running when human readable translation was started. Tracing "
                        "stopped.";
    Stop();
  }

  const size_t buf_len = 256;
  uint8_t data_buf[buf_len];
  size_t records_read = 0;
  size_t bytes_read_per_fetch = 0;
  uint32_t offset = 0;

  if (!human_readable_file) {
    FXL_LOG(ERROR) << "Failed to open file.";
    return false;
  }

  bool done = false;
  while (!done) {
    auto [read_success, buffer_end] =
        FetchRecord(root_resource_, data_buf, &offset, &bytes_read_per_fetch, buf_len);

    if (!read_success) {
      return false;
    } else if (buffer_end) {
      done = true;
      continue;
    }

    const auto record_opt = KTraceRecord::ParseRecord(data_buf, buf_len);

    if (!record_opt) {
      return false;
    }

    auto& record = record_opt.value();

    records_read++;

    if (!record.IsNamed()) {
      if (record.HasUnexpectedEvent()) {
        human_readable_file << "Unexpected event: 0x" << std::hex << record.GetEvent() << "\n";
        continue;
      }

      switch (record.GetInfo()->type) {
        case kTag16B:
          Write16B(record, &human_readable_file);
          break;
        case kTag32B:
          Write32B(record, &human_readable_file);
          break;
        case kTagNAME:
          WriteName(record, &human_readable_file);
          break;
        default:
          human_readable_file << "Unexpected tag type: 0x" << std::hex << record.GetInfo() << "\n";
          break;
      }
    } else /* Named event.*/ {
      EventState event_state;

      if ((record.IsFlow() || record.IsDuration()) && record.IsBegin()) {
        event_state = kBegin;
      } else if ((record.IsFlow() || record.IsDuration()) && record.IsEnd()) {
        event_state = kEnd;
      }

      if (record.IsProbeGroup()) {
        WriteProbeRecord(record, &human_readable_file);
      } else if (record.IsDuration()) {
        WriteDurationRecord(record, event_state, &human_readable_file);
      } else if (record.IsFlow()) {
        WriteFlowRecord(record, event_state, &human_readable_file);
      } else {
        ktrace_header_t* rec;
        record.Get16BRecord(&rec);
        human_readable_file << "Unexpected tag: 0x" << std::hex << rec->tag << "\n";
      }
    }
  }

  human_readable_file << "\nTotal records read: " << std::dec << records_read
                      << "\nTotal bytes read: " << std::dec << offset + bytes_read_per_fetch
                      << "\n";

  return true;
}

// Picks out traces pertaining to name in string_ref and populates stats on them. Returns false if
// name not found.
bool Tracing::PopulateDurationStats(std::string string_ref,
                                    std::vector<DurationStats>* duration_stats,
                                    std::map<uint64_t, QueuingStats>* queuing_stats) {
  if (running_) {
    FXL_LOG(WARNING) << "Tracing was running when duration stats were started. Tracing stopped.";
    Stop();
  }

  const size_t buf_len = 256;
  uint8_t data_buf[buf_len];
  size_t bytes_read_per_fetch = 0;
  uint32_t offset = 0;
  bool string_ref_found = false;
  uint32_t desired_event_name_id;

  bool done = false;
  while (!done) {
    auto [read_success, buffer_end] =
        FetchRecord(root_resource_, data_buf, &offset, &bytes_read_per_fetch, buf_len);

    if (!read_success) {
      FXL_LOG(WARNING) << "Error reading traces, trace read stopped.";
      return false;
    } else if (buffer_end) {
      done = true;
      continue;
    }

    const auto record_opt = KTraceRecord::ParseRecord(data_buf, buf_len);

    if (!record_opt) {
      FXL_LOG(WARNING) << "Error reading traces, trace read stopped.";
      return false;
    }

    auto& record = record_opt.value();

    if (!record.IsNamed()) {
      ktrace_rec_name_t* name_record = nullptr;

      if (!string_ref_found && record.GetNameRecord(&name_record)) {
        if (name_record->name == string_ref) {
          desired_event_name_id = name_record->id;
          string_ref_found = true;
        }
      }
    } else if (string_ref_found) /* Named event. */ {
      // Match duration records for given string ref.
      ktrace_header_t* rec;
      if (!record.Get16BRecord(&rec)) {
        FXL_LOG(WARNING) << "Record error.";
        return false;
      }

      if (record.IsDuration() && KTRACE_EVENT_NAME_ID(rec->tag) == desired_event_name_id) {
        if (record.IsBegin()) {
          duration_stats->push_back(DurationStats(rec->ts));
        } else if (!duration_stats->empty()) {
          auto& latest_record = duration_stats->back();

          latest_record.end_ts_ns = rec->ts;
          latest_record.wall_duration_ns = latest_record.end_ts_ns - latest_record.begin_ts_ns;
          latest_record.payload = record.Get128BitPayload();
        }
      } else if (record.IsFlow()) {
        if (!record.GetFlowID() || !record.GetAssociatedThread()) {
          FXL_LOG(WARNING) << "Record error.";
          return false;
        }

        const auto flow_id = record.GetFlowID().value();
        const auto associated_thread = record.GetAssociatedThread().value();

        if (record.IsBegin()) {
          queuing_stats->emplace(flow_id, QueuingStats(rec->ts, associated_thread));
        } else {
          auto flow_iter = queuing_stats->find(flow_id);

          if (flow_iter == queuing_stats->end()) {
            continue;
          } else {
            flow_iter->second.end_ts_ns = rec->ts;
            flow_iter->second.queuing_time_ns =
                flow_iter->second.end_ts_ns - flow_iter->second.begin_ts_ns;
          }
        }
      }
    }
  }

  return string_ref_found;
}
