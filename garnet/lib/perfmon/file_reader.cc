// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_reader.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"

namespace perfmon {

bool FileReader::Create(FileNameProducer file_name_producer, uint32_t num_traces,
                        std::unique_ptr<FileReader>* out_reader) {
  out_reader->reset(new FileReader(std::move(file_name_producer), num_traces));
  return true;
}

FileReader::FileReader(FileNameProducer file_name_producer, uint32_t num_traces)
    : Reader(num_traces), file_name_producer_(std::move(file_name_producer)) {}

bool FileReader::MapBuffer(const std::string& name, uint32_t trace_num) {
  if (!UnmapBuffer()) {
    return false;
  }

  std::string file_name = file_name_producer_(trace_num);
  int raw_fd = open(file_name.c_str(), O_RDONLY);
  if (raw_fd < 0) {
    FXL_LOG(ERROR) << name << ": Unable to open buffer file: " << file_name << ": "
                   << strerror(errno);
    return false;
  }
  fbl::unique_fd fd(raw_fd);
  file_size_ = lseek(raw_fd, 0, SEEK_END);
  lseek(raw_fd, 0, SEEK_SET);
#ifdef __Fuchsia__
  // Mmap can currently fail if the file is on minfs, so just punt.
  void* mapped_buffer = reinterpret_cast<void*>(-1);
#else
  void* mapped_buffer = mmap(nullptr, file_size_, PROT_READ, MAP_PRIVATE, raw_fd, 0);
  if (mapped_buffer == reinterpret_cast<void*>(-1)) {
    FXL_VLOG(2) << name << ": Unable to map buffer file: " << file_name << ": " << strerror(errno);
  }
#endif
  if (mapped_buffer == reinterpret_cast<void*>(-1)) {
    std::vector<uint8_t> data;
    if (!files::ReadFileDescriptorToVector(raw_fd, &data)) {
      FXL_LOG(ERROR) << "Error reading: " << file_name;
      return false;
    }
    if (data.size() != file_size_) {
      FXL_LOG(ERROR) << "Error reading: " << file_name << ": got " << data.size()
                     << " bytes instead of expected " << file_size_;
      return false;
    }
    buffer_ = std::move(data);
    buffer_ptr_ = buffer_.data();
    file_is_mmapped_ = false;
  } else {
    buffer_ptr_ = mapped_buffer;
    file_is_mmapped_ = true;
  }

  ReaderStatus status = BufferReader::Create(name, buffer_ptr_, file_size_, &buffer_reader_);
  return status == ReaderStatus::kOk;
}

bool FileReader::UnmapBuffer() {
  if (buffer_ptr_) {
    buffer_reader_.reset();
    if (file_is_mmapped_) {
      auto buffer = const_cast<void*>(buffer_ptr_);
      int error = munmap(buffer, file_size_);
      if (error != 0) {
        FXL_LOG(ERROR) << "Unable to unmap buffer: " << strerror(errno);
        return false;
      }
    }
    buffer_ptr_ = nullptr;
    buffer_ = std::vector<uint8_t>();  // Ensure the vector's data is freed
  }
  return true;
}

}  // namespace perfmon
