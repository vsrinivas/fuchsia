// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTING_LOADBENCH_TRACING_H_
#define SRC_TESTING_LOADBENCH_TRACING_H_

#include <lib/zircon-internal/ktrace.h>

#include <array>
#include <string>

#include "utility.h"

typedef enum { kTag16B, kTag32B, kTagNAME } TagType;

typedef struct {
  uint32_t num;
  uint32_t group;
  TagType type;
  const char* name;
} TagDefinition;

static TagDefinition kTags[] = {
#define KTRACE_DEF(num, type, name, group) [num] = {num, KTRACE_GRP_##group, kTag##type, #name},
#include <lib/zircon-internal/ktrace-def.h>
};

class KTraceRecord {
 public:
  static std::optional<KTraceRecord> ParseRecord(uint8_t* data_buf, size_t buf_len);

  bool Get16BRecord(ktrace_header_t** record) const;
  bool Get32BRecord(ktrace_rec_32b_t** record) const;
  bool GetNameRecord(ktrace_rec_name_t** record) const;
  std::optional<std::array<uint32_t, 2>> Get64BitPayload() const;
  std::optional<std::array<uint64_t, 2>> Get128BitPayload() const;
  std::optional<const uint64_t> GetFlowID() const;

  uint32_t GetEvent() const { return event_; }
  TagDefinition* GetInfo() const { return info_; }

  bool IsNamed() const { return is_named_; }
  bool IsProbeGroup() const { return is_probe_group_; }
  bool IsFlow() const { return is_flow_; }
  bool IsBegin() const { return is_begin_; }
  bool IsEnd() const { return is_end_; }
  bool IsDuration() const { return is_duration_; }
  bool HasUnexpectedEvent() const { return has_unexpected_event_; }

 private:
  size_t data_buf_len_;
  ktrace_header_t* rec_16b_ = nullptr;
  uint32_t event_;
  TagDefinition* info_;

  bool is_named_ = false;
  bool is_probe_group_ = false;
  bool is_flow_ = false;
  bool is_begin_ = false;
  bool is_end_ = false;
  bool is_duration_ = false;
  bool has_unexpected_event_ = false;
};

class Tracing {
 public:
  Tracing() = default;

  Tracing(const Tracing&) = delete;
  Tracing& operator=(const Tracing&) = delete;

  Tracing(Tracing&&) = delete;
  Tracing& operator=(Tracing&&) = delete;

  virtual ~Tracing() { Stop(); }

  // Rewinds kernel trace buffer.
  void Rewind();

  // Starts kernel tracing.
  void Start(uint32_t group_mask);

  // Stops kernel tracing.
  void Stop();

  // Fetches record from kernel buffer if available. Returns false if errors were encountered.
  // Returns two booleans, first signals whether read was successful, second signals whether entire
  // kernel trace buffer has been read.
  virtual std::tuple<bool, bool> FetchRecord(zx_handle_t handle, uint8_t* data_buf,
                                             uint32_t* offset, size_t* bytes_read, size_t buf_len);

  // Reads trace buffer and converts output into human-readable format. Stores in location defined
  // by <filepath>. Will overwrite any existing files with same name.
  bool WriteHumanReadable(std::ostream& filepath);

  bool running() const { return running_; }

 private:
  enum EventState {
    kBegin,
    kEnd,
  };

  // Performs same action as zx_ktrace_read and does necessary checks.
  virtual void ReadKernelBuffer(zx_handle_t handle, void* data_buf, uint32_t offset, size_t len,
                                size_t* bytes_read);

  // Returns a string with human-readable translations of tag name, event, and any possible flags.
  std::string InterpretTag(const uint32_t tag, const TagDefinition& info);

  // Writes human-readable translation for 16 byte records into file specified by <file>.
  void Write16B(const KTraceRecord record, std::ostream* file);

  // Writes human-readable translation for 32 byte records into file specified by <file>.
  void Write32B(const KTraceRecord record, std::ostream* file);

  // Writes human-readable translation name type records into file specified by <file>.
  void WriteName(const KTraceRecord record, std::ostream* file);

  // Writes human-readable translation for probe records into file specified by <file>.
  void WriteProbeRecord(const KTraceRecord record, std::ostream* file);

  // Writes human-readable translation for probe records into file specified by <file>.
  void WriteDurationRecord(const KTraceRecord record, const EventState event_state,
                           std::ostream* file);

  // Writes human-readable translation for flow records into file specified by <file>.
  void WriteFlowRecord(const KTraceRecord record, const EventState event_state, std::ostream* file);

  zx_handle_t root_resource_ = GetRootResource()->get();
  bool running_ = false;
};

#endif  // SRC_TESTING_LOADBENCH_TRACING_H_
