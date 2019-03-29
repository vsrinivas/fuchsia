// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "src/lib/files/file.h"
#include <src/lib/fxl/logging.h>

#include "file_reader.h"

namespace perfmon {

bool FileReader::Create(FileNameProducer file_name_producer,
                        uint32_t num_traces,
                        std::unique_ptr<FileReader>* out_reader) {
  out_reader->reset(new FileReader(std::move(file_name_producer), num_traces));
  return true;
}

FileReader::FileReader(FileNameProducer file_name_producer,
                       uint32_t num_traces)
    : Reader(num_traces),
      file_name_producer_(std::move(file_name_producer)) {
}

bool FileReader::MapBuffer(const std::string& name, uint32_t trace_num) {
  if (!UnmapBuffer()) {
    return false;
  }

  std::string file_name = file_name_producer_(trace_num);
  int raw_fd = open(file_name.c_str(), O_RDONLY);
  if (raw_fd < 0) {
    FXL_LOG(ERROR) << name << ": Unable to open buffer file: " << file_name
                   << ": " << strerror(errno);
    return false;
  }
  fxl::UniqueFD fd(raw_fd);
  file_size_ = lseek(raw_fd, 0, SEEK_END);
  lseek(raw_fd, 0, SEEK_SET);
#ifdef __Fuchsia__
  // Mmap can currently fail if the file is on minfs, so just punt.
  void* buffer = reinterpret_cast<void*>(-1);
#else
  void* buffer = mmap(nullptr, file_size_, PROT_READ, MAP_PRIVATE, raw_fd, 0);
  if (buffer == reinterpret_cast<void*>(-1)) {
    FXL_VLOG(2) << name << ": Unable to map buffer file: " << file_name
                << ": " << strerror(errno);
  }
#endif
  if (buffer == reinterpret_cast<void*>(-1)) {
    // Workaround this by just reading in the file.
    std::pair<uint8_t*, intptr_t> bytes =
      files::ReadFileDescriptorToBytes(raw_fd);
    if (bytes.first == nullptr) {
      FXL_LOG(ERROR) << "Error reading: " << file_name;
      return false;
    }
    if (static_cast<size_t>(bytes.second) != file_size_) {
      FXL_LOG(ERROR) << "Error reading: " << file_name
                     << ": got " << bytes.second
                     << " bytes instead of expected " << file_size_;
      return false;
    }
    buffer = reinterpret_cast<void*>(bytes.first);
    file_is_mmapped_ = false;
  } else {
    file_is_mmapped_ = true;
  }
  buffer_contents_ = buffer;

  ReaderStatus status = BufferReader::Create(name, buffer_contents_,
                                             file_size_, &buffer_reader_);
  if (status != ReaderStatus::kOk) {
    return false;
  }

  return true;
}

bool FileReader::UnmapBuffer() {
  if (buffer_contents_) {
    buffer_reader_.reset();
    if (file_is_mmapped_) {
      auto buffer = const_cast<void*>(buffer_contents_);
      int error = munmap(buffer, file_size_);
      if (error != 0) {
        FXL_LOG(ERROR) << "Unable to unmap buffer: " << strerror(errno);
        return false;
      }
    } else {
      free(const_cast<void*>(buffer_contents_));
    }
    buffer_contents_ = nullptr;
  }
  return true;
}

}  // namespace perfmon
