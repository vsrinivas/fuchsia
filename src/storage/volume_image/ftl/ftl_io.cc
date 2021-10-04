// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/ftl/ftl_io.h"

#include <iostream>
#include <memory>

namespace storage::volume_image {

class FtlReader final : public Reader {
 public:
  explicit FtlReader(std::shared_ptr<FtlInstance> instance, std::shared_ptr<ftl::Volume> volume,
                     uint64_t length)
      : instance_(std::move(instance)), volume_(std::move(volume)), length_(length) {}

  uint64_t length() const final { return length_; }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    if (offset % instance_->page_size() != 0) {
      return fpromise::error("FtlReader requires aligned offset(" + std::to_string(offset) +
                             ") at page boundaries(" + std::to_string(instance_->page_size()) +
                             ").");
    }
    if (buffer.size() % instance_->page_size() != 0) {
      return fpromise::error("FtlReader requires aligned page buffer(size " +
                             std::to_string(buffer.size()) + ") at page boundaries(" +
                             std::to_string(instance_->page_size()) + ").");
    }

    uint32_t page_offset = static_cast<uint32_t>(offset / instance_->page_size());
    int page_count = static_cast<int>(buffer.size() / instance_->page_size());

    if (page_count == 0) {
      return fpromise::ok();
    }
    uint64_t page_range_end = page_offset + page_count;

    if (page_range_end > instance_->page_count()) {
      return fpromise::error("FtlReader::Read out of bounds. Offset " + std::to_string(offset) +
                             " (Page: " + std::to_string(page_offset) + ")" +
                             " attempting to write " + std::to_string(buffer.size()) +
                             " bytes (Page Count: " + std::to_string(page_count) +
                             "), exceeds maximum offset of " + std::to_string(offset) +
                             "(Page Size: " + std::to_string(instance_->page_size()) +
                             ", Page Count: " + std::to_string(instance_->page_count()) + ").");
    }
    if (auto result = volume_->Read(page_offset, page_count, buffer.data()); result != ZX_OK) {
      return fpromise::error("Failed to read " + std::to_string(page_count) +
                             " pages starting at " + std::to_string(page_offset) +
                             ". More specifically: " + std::to_string(result) + ".");
    }

    return fpromise::ok();
  }

 private:
  std::shared_ptr<FtlInstance> instance_;
  std::shared_ptr<ftl::Volume> volume_;
  uint64_t length_ = 0;
};

class FtlWriter final : public Writer {
 public:
  explicit FtlWriter(std::shared_ptr<FtlInstance> instance, std::shared_ptr<ftl::Volume> volume)
      : instance_(std::move(instance)), volume_(std::move(volume)) {}

  ~FtlWriter() final { volume_->Flush(); }

  fpromise::result<void, std::string> Write(uint64_t offset,
                                            cpp20::span<const uint8_t> buffer) final {
    if (offset % instance_->page_size() != 0) {
      return fpromise::error("FtlWriter requires aligned offset(" + std::to_string(offset) +
                             ") at page boundaries(" + std::to_string(instance_->page_size()) +
                             ").");
    }
    if (buffer.size() % instance_->page_size() != 0) {
      return fpromise::error("FtlWriter requires aligned page buffer(size " +
                             std::to_string(buffer.size()) + ") at page boundaries(" +
                             std::to_string(instance_->page_size()) + ").");
    }

    uint32_t page_offset = static_cast<uint32_t>(offset / instance_->page_size());
    int page_count = static_cast<int>(buffer.size() / instance_->page_size());

    if (page_count == 0) {
      return fpromise::ok();
    }
    uint64_t page_range_end = page_offset + page_count;

    if (page_range_end > instance_->page_count()) {
      return fpromise::error("FtlWriter::Write out of bounds. Offset " + std::to_string(offset) +
                             " (Page: " + std::to_string(page_offset) + ")" +
                             " attempting to write " + std::to_string(buffer.size()) +
                             " bytes (Page Count: " + std::to_string(page_count) +
                             "), exceeds maximum offset of " + std::to_string(offset) +
                             "(Page Size: " + std::to_string(instance_->page_size()) +
                             ", Page Count: " + std::to_string(instance_->page_count()) + ").");
    }

    if (auto result = volume_->Write(page_offset, page_count, buffer.data()); result != ZX_OK) {
      return fpromise::error("Failed to write " + std::to_string(page_count) +
                             " pages starting at " + std::to_string(page_offset) +
                             ". More specifically: " + std::to_string(result) + ".");
    }
    return fpromise::ok();
  }

 private:
  std::shared_ptr<FtlInstance> instance_;
  std::shared_ptr<ftl::Volume> volume_;
};

fpromise::result<void, std::string> FtlHandle::Init(std::unique_ptr<ftl::NdmDriver> driver) {
  auto* error = volume_->Init(std::move(driver));
  if (error != nullptr) {
    return fpromise::error("FtlHandle::Init failed. More specifically: " + std::string(error) +
                           ".");
  }

  return fpromise::ok();
}

std::unique_ptr<Reader> FtlHandle::MakeReader() {
  return std::make_unique<FtlReader>(instance_, volume_,
                                     instance_->page_count() * instance_->page_size());
}

std::unique_ptr<Writer> FtlHandle::MakeWriter() {
  return std::make_unique<FtlWriter>(instance_, volume_);
}

}  // namespace storage::volume_image
