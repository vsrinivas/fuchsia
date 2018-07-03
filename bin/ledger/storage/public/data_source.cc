// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/public/data_source.h"

#include <lib/fit/function.h>
#include <lib/fsl/socket/socket_drainer.h>
#include <lib/zx/vmar.h>

#include "peridot/lib/convert/convert.h"

namespace storage {

namespace {

template <typename S>
class StringLikeDataChunk : public DataSource::DataChunk {
 public:
  explicit StringLikeDataChunk(S value) : value_(std::move(value)) {}

 private:
  fxl::StringView Get() override { return convert::ExtendedStringView(value_); }

  S value_;
};

template <typename S>
class StringLikeDataSource : public DataSource {
 public:
  explicit StringLikeDataSource(S value)
      : value_(std::move(value)), size_(value_.size()) {}

 private:
  uint64_t GetSize() override { return size_; }

  void Get(fit::function<void(std::unique_ptr<DataChunk>, Status)> callback)
      override {
#ifndef NDEBUG
    FXL_DCHECK(!called_);
    called_ = true;
#endif
    callback(std::make_unique<StringLikeDataChunk<S>>(std::move(value_)),
             Status::DONE);
  }

  S value_;
  uint64_t size_;
#ifndef NDEBUG
  bool called_ = false;
#endif
};

class VmoDataChunk : public DataSource::DataChunk {
 public:
  explicit VmoDataChunk(fsl::SizedVmo vmo) : vmo_(std::move(vmo)) {}

  zx_status_t Init() {
    uintptr_t allocate_address;
    zx_status_t status = zx::vmar::root_self().allocate(
        0, ToFullPages(vmo_.size()), ZX_VM_FLAG_CAN_MAP_READ, &vmar_,
        &allocate_address);
    if (status != ZX_OK) {
      return status;
    }

    return vmar_.map(0, vmo_.vmo(), 0, vmo_.size(), ZX_VM_FLAG_PERM_READ,
                     &mapped_address_);
  }

 private:
  uint64_t ToFullPages(uint64_t value) {
    return (value + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1));
  }

  fxl::StringView Get() override {
    return fxl::StringView(reinterpret_cast<char*>(mapped_address_),
                           vmo_.size());
  }

  fsl::SizedVmo vmo_;
  zx::vmar vmar_;
  uintptr_t mapped_address_;
};

class VmoDataSource : public DataSource {
 public:
  explicit VmoDataSource(fsl::SizedVmo vmo) : vmo_(std::move(vmo)) {
    FXL_DCHECK(vmo_);
  }

 private:
  uint64_t GetSize() override { return vmo_.size(); }

  void Get(fit::function<void(std::unique_ptr<DataChunk>, Status)> callback)
      override {
#ifndef NDEBUG
    FXL_DCHECK(!called_);
    called_ = true;
#endif
    if (!vmo_) {
      callback(nullptr, Status::ERROR);
      return;
    }
    auto data = std::make_unique<VmoDataChunk>(std::move(vmo_));
    if (data->Init() != ZX_OK) {
      callback(nullptr, Status::ERROR);
      return;
    }
    callback(std::move(data), Status::DONE);
  }

  fsl::SizedVmo vmo_;
#ifndef NDEBUG
  bool called_ = false;
#endif
};

class SocketDataSource : public DataSource, public fsl::SocketDrainer::Client {
 public:
  SocketDataSource(zx::socket socket, uint64_t expected_size)
      : socket_(std::move(socket)),
        expected_size_(expected_size),
        remaining_bytes_(expected_size) {
    FXL_DCHECK(socket_);
  }

 private:
  uint64_t GetSize() override { return expected_size_; }

  void Get(fit::function<void(std::unique_ptr<DataChunk>, Status)> callback)
      override {
    FXL_DCHECK(socket_);
    callback_ = std::move(callback);
    socket_drainer_ = std::make_unique<fsl::SocketDrainer>(this);
    socket_drainer_->Start(std::move(socket_));
    socket_.reset();
  }

  void OnDataAvailable(const void* data, size_t num_bytes) override {
    if (num_bytes > remaining_bytes_) {
      FXL_LOG(ERROR) << "Received incorrect number of bytes. Expected: "
                     << expected_size_ << ", but received at least "
                     << (num_bytes - remaining_bytes_) << " more.";
      socket_drainer_.reset();
      callback_(nullptr, Status::ERROR);
      return;
    }

    remaining_bytes_ -= num_bytes;
    callback_(std::make_unique<StringLikeDataChunk<std::string>>(
                  std::string(reinterpret_cast<const char*>(data), num_bytes)),
              Status::TO_BE_CONTINUED);
  }

  void OnDataComplete() override {
    socket_drainer_.reset();
    if (remaining_bytes_ != 0) {
      FXL_LOG(ERROR) << "Received incorrect number of bytes. Expected: "
                     << expected_size_ << ", but received "
                     << (expected_size_ - remaining_bytes_);
      callback_(nullptr, Status::ERROR);
      return;
    }

    callback_(std::make_unique<StringLikeDataChunk<std::string>>(std::string()),
              Status::DONE);
  }

  zx::socket socket_;
  uint64_t expected_size_;
  uint64_t remaining_bytes_;
  std::unique_ptr<fsl::SocketDrainer> socket_drainer_;
  fit::function<void(std::unique_ptr<DataChunk>, Status)> callback_;
};

class FlatBufferDataChunk : public DataSource::DataChunk {
 public:
  explicit FlatBufferDataChunk(
      std::unique_ptr<flatbuffers::FlatBufferBuilder> value)
      : value_(std::move(value)) {}

 private:
  fxl::StringView Get() override {
    return fxl::StringView(reinterpret_cast<char*>(value_->GetBufferPointer()),
                           value_->GetSize());
  }

  std::unique_ptr<flatbuffers::FlatBufferBuilder> value_;
};

}  // namespace

std::unique_ptr<DataSource::DataChunk> DataSource::DataChunk::Create(
    std::string value) {
  return std::make_unique<StringLikeDataChunk<std::string>>(std::move(value));
}

std::unique_ptr<DataSource::DataChunk> DataSource::DataChunk::Create(
    std::unique_ptr<flatbuffers::FlatBufferBuilder> builder) {
  return std::make_unique<FlatBufferDataChunk>(std::move(builder));
}

std::unique_ptr<DataSource> DataSource::Create(std::string value) {
  return std::make_unique<StringLikeDataSource<std::string>>(std::move(value));
}

std::unique_ptr<DataSource> DataSource::Create(std::vector<uint8_t> value) {
  return std::make_unique<StringLikeDataSource<std::vector<uint8_t>>>(
      std::move(value));
}

std::unique_ptr<DataSource> DataSource::Create(fsl::SizedVmo vmo) {
  return std::make_unique<VmoDataSource>(std::move(vmo));
}

std::unique_ptr<DataSource> DataSource::Create(zx::socket socket,
                                               uint64_t size) {
  return std::make_unique<SocketDataSource>(std::move(socket), size);
}

}  // namespace storage
