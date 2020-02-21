// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_FILE_READER_H_
#define GARNET_LIB_PERFMON_FILE_READER_H_

#include <lib/fit/function.h>
#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <zircon/types.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "reader.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/macros.h"

namespace perfmon {

class FileReader final : public Reader {
 public:
  using FileNameProducer = fit::function<std::string(uint32_t trace_num)>;

  static bool Create(FileNameProducer file_name_producer, uint32_t num_traces,
                     std::unique_ptr<FileReader>* out_reader);

 private:
  FileReader(FileNameProducer file_name_producer, uint32_t num_traces);

  bool MapBuffer(const std::string& name, uint32_t trace_num) override;
  bool UnmapBuffer() override;

  FileNameProducer file_name_producer_;

  std::vector<uint8_t> buffer_;
  const void* buffer_ptr_ = nullptr;
  size_t file_size_ = 0;
  bool file_is_mmapped_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(FileReader);
};

}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_FILE_READER_H_
