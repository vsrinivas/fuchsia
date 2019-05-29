// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/vfs.h>
#include <lib/vfs/cpp/flags.h>
#include <lib/vfs/cpp/internal/file_connection.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <sstream>

namespace vfs {

PseudoFile::PseudoFile(size_t max_file_size, ReadHandler read_handler,
                       WriteHandler write_handler)
    : read_handler_(std::move(read_handler)),
      write_handler_(std::move(write_handler)),
      max_file_size_(max_file_size) {
  ZX_DEBUG_ASSERT(read_handler_ != nullptr);
}

PseudoFile::~PseudoFile() = default;

zx_status_t PseudoFile::CreateConnection(
    uint32_t flags, std::unique_ptr<vfs::internal::Connection>* connection) {
  std::vector<uint8_t> output;
  if (Flags::IsReadable(flags)) {
    zx_status_t status = read_handler_(&output, max_file_size_);
    if (status != ZX_OK) {
      return status;
    }
    if (output.size() > max_file_size_) {
      return ZX_ERR_FILE_BIG;
    }
  }
  *connection =
      std::make_unique<PseudoFile::Content>(this, flags, std::move(output));
  return ZX_OK;
}

zx_status_t PseudoFile::GetAttr(
    fuchsia::io::NodeAttributes* out_attributes) const {
  out_attributes->mode = fuchsia::io::MODE_TYPE_FILE;
  if (read_handler_ != nullptr)
    out_attributes->mode |= V_IRUSR;
  if (write_handler_)
    out_attributes->mode |= V_IWUSR;
  out_attributes->id = fuchsia::io::INO_UNKNOWN;
  out_attributes->content_size = 0;
  out_attributes->storage_size = 0;
  out_attributes->link_count = 1;
  out_attributes->creation_time = 0;
  out_attributes->modification_time = 0;
  return ZX_OK;
}

NodeKind::Type PseudoFile::GetKind() const {
  auto kind = File::GetKind() | NodeKind::kReadable;
  if (write_handler_ != nullptr) {
    kind |= NodeKind::kWritable | NodeKind::kCanTruncate;
  }
  return kind;
}

uint64_t PseudoFile::GetLength() {
  // this should never be called
  ZX_ASSERT(false);

  return 0u;
}

size_t PseudoFile::GetCapacity() {
  // this should never be called
  ZX_DEBUG_ASSERT(false);

  return max_file_size_;
}

PseudoFile::Content::Content(PseudoFile* file, uint32_t flags,
                             std::vector<uint8_t> content)
    : Connection(flags),
      file_(file),
      buffer_(std::move(content)),
      flags_(flags) {
  SetInputLength(buffer_.size());
}

PseudoFile::Content::~Content() { TryFlushIfRequired(); }

zx_status_t PseudoFile::Content::TryFlushIfRequired() {
  if (!dirty_) {
    return ZX_OK;
  }
  dirty_ = false;
  return file_->write_handler_(std::move(buffer_));
}

zx_status_t PseudoFile::Content::PreClose(Connection* connection) {
  return TryFlushIfRequired();
}

NodeKind::Type PseudoFile::Content::GetKind() const { return file_->GetKind(); }

zx_status_t PseudoFile::Content::ReadAt(uint64_t count, uint64_t offset,
                                        std::vector<uint8_t>* out_data) {
  if (offset >= buffer_.size()) {
    return ZX_OK;
  }
  size_t actual = std::min(buffer_.size() - offset, count);
  out_data->resize(actual);
  std::copy_n(buffer_.begin() + offset, actual, out_data->begin());
  return ZX_OK;
}

zx_status_t PseudoFile::Content::GetAttr(
    fuchsia::io::NodeAttributes* out_attributes) const {
  return file_->GetAttr(out_attributes);
}

zx_status_t PseudoFile::Content::WriteAt(std::vector<uint8_t> data,
                                         uint64_t offset,
                                         uint64_t* out_actual) {
  if (offset >= file_->max_file_size_) {
    *out_actual = 0u;
    return ZX_OK;
  }

  size_t actual = std::min(data.size(), file_->max_file_size_ - offset);
  if (actual == 0) {
    *out_actual = 0u;
    return ZX_OK;
  }

  dirty_ = true;
  if (actual + offset > buffer_.size()) {
    SetInputLength(offset + actual);
  }

  std::copy_n(data.begin(), actual, buffer_.begin() + offset);
  *out_actual = actual;
  return ZX_OK;
}

zx_status_t PseudoFile::Content::Truncate(uint64_t length) {
  if (length > file_->max_file_size_) {
    return ZX_ERR_NO_SPACE;
  }

  dirty_ = true;
  SetInputLength(length);
  return ZX_OK;
}

uint64_t PseudoFile::Content::GetLength() { return buffer_.size(); }

size_t PseudoFile::Content::GetCapacity() { return file_->max_file_size_; }

void PseudoFile::Content::SetInputLength(size_t length) {
  ZX_ASSERT_MSG(length <= file_->max_file_size_,
                "Should not happen. Please report a bug.");

  buffer_.resize(length);
}

zx_status_t PseudoFile::Content::BindInternal(zx::channel request,
                                              async_dispatcher_t* dispatcher) {
  std::unique_ptr<Connection> connection;
  zx_status_t status = CreateConnection(flags_, &connection);
  if (status != ZX_OK) {
    SendOnOpenEventOnError(flags_, std::move(request), status);
    return status;
  }
  status = connection->Bind(std::move(request), dispatcher);

  AddConnection(std::move(connection));

  // only one connection allowed per content
  ZX_DEBUG_ASSERT(GetConnectionCount() == 1);

  return status;
}

std::unique_ptr<vfs::internal::Connection> PseudoFile::Content::Close(
    Connection* connection) {
  File::Close(connection);
  return file_->Close(this);
}

void PseudoFile::Content::Clone(uint32_t flags, uint32_t parent_flags,
                                zx::channel request,
                                async_dispatcher_t* dispatcher) {
  file_->Clone(flags, parent_flags, std::move(request), dispatcher);
}

void PseudoFile::Content::SendOnOpenEvent(zx_status_t status) {
  // not needed as underlying connection should do this
}

}  // namespace vfs
