// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zircon-internal/thread_annotations.h>

#include <cinttypes>
#include <fstream>
#include <mutex>
#include <string_view>

// Helper class to record file information into a JSON output.
class JsonRecorder {
 public:
  JsonRecorder();
  ~JsonRecorder();

  // Open a file to record entries into.
  bool OpenFile(const char* const path);

  // If a JSON file was opened, record that the file |path|, with digest
  // |digest| of length |bytes| occupied |size| bytes.
  void Append(std::string_view path, std::string_view digest, std::uint64_t bytes,
              std::size_t size);

 private:
  std::mutex sizes_stream_lock_;
  std::ofstream sizes_stream_ TA_GUARDED(sizes_stream_lock_);
  bool needs_comma_ = false;
};
