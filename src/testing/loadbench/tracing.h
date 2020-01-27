// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTING_LOADBENCH_TRACING_H_
#define SRC_TESTING_LOADBENCH_TRACING_H_

#include <lib/zircon-internal/ktrace.h>

#include <string>

#include "utility.h"

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

  // Reads trace buffer and converts output into human-readable format. Stores in location defined
  // by <filepath>. Will overwrite any existing files with same name.
  bool WriteHumanReadable(std::ostream& filepath);

  bool running() { return running_; }

 private:
  typedef enum { kTag16B, kTag32B, kTagNAME } TagType;

  typedef struct {
    uint32_t num;
    uint32_t group;
    TagType type;
    const char* name;
  } TagDefinition;

  static constexpr TagDefinition kTags[] = {
#define KTRACE_DEF(num, type, name, group) [num] = {num, KTRACE_GRP_##group, kTag##type, #name},
#include <lib/zircon-internal/ktrace-def.h>
  };

  // Returns a string with human-readable translations of tag name, event, and any possible flags.
  std::string InterpretTag(const uint32_t tag, const TagDefinition* info);

  // Writes human-readable translation for 16 byte records into file specified by <file>.
  void Write16B(std::ostream& file, const TagDefinition* info, const ktrace_header_t* record);

  // Writes human-readable translation for 32 byte records into file specified by <file>.
  void Write32B(std::ostream& file, const TagDefinition* info, const ktrace_rec_32b_t* record);

  // Writes human-readable translation name type records into file specified by <file>.
  void WriteName(std::ostream& file, const TagDefinition* info, const ktrace_rec_name_t* record);

  // Performs same action as zx_ktrace_read, but returns bytes_read.
  virtual size_t ReadAndReturnBytesRead(zx_handle_t handle, void* data, uint32_t offset, size_t len,
                                        size_t* bytes_read);

  bool running_ = false;
  zx_handle_t root_resource_ = GetRootResource()->get();
};

#endif  // SRC_TESTING_LOADBENCH_TRACING_H_
