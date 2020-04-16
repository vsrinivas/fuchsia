// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#include <filesystem>

#include <fs-host/json_recorder.h>

JsonRecorder::JsonRecorder() = default;
JsonRecorder::~JsonRecorder() {
  std::lock_guard<std::mutex> lock(sizes_stream_lock_);
  if (!sizes_stream_.is_open())
    return;
  sizes_stream_ << "\n]\n";
}

bool JsonRecorder::OpenFile(const char* const path) {
  std::lock_guard<std::mutex> lock(sizes_stream_lock_);
  if (sizes_stream_.is_open())
    return false;
  sizes_stream_.open(path);
  sizes_stream_ << "[\n";
  return true;
}

void JsonRecorder::Append(std::string_view path, std::string_view digest,
                         std::uint64_t bytes, std::size_t size) {
  std::lock_guard<std::mutex> lock(sizes_stream_lock_);
  if (!sizes_stream_.is_open())
    return;
  if (likely(needs_comma_))
    sizes_stream_ << ",\n";
  else
    needs_comma_ = true;
  sizes_stream_ << "  {\n";
  sizes_stream_ << "    \"source_path\": " << std::filesystem::relative(std::filesystem::canonical(path)) << ",\n";
  sizes_stream_ << "    \"merkle\": \"" << digest << "\",\n";
  sizes_stream_ << "    \"bytes\": " << bytes << ",\n";
  sizes_stream_ << "    \"size\": " << size << "\n";
  sizes_stream_ << "  }";
}
