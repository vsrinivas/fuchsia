// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/vfs/cpp/pseudo_file.h>

#include <lib/vfs/cpp/flags.h>
#include <lib/vfs/cpp/internal/file_connection.h>
#include <zircon/assert.h>

#include <sstream>

namespace vfs {

BufferedPseudoFile::BufferedPseudoFile(ReadHandler read_handler,
                                       WriteHandler write_handler,
                                       size_t buffer_capacity)
    : read_handler_(std::move(read_handler)),
      write_handler_(std::move(write_handler)),
      buffer_capacity_(buffer_capacity) {
  ZX_DEBUG_ASSERT(read_handler_ != nullptr);
}

BufferedPseudoFile::~BufferedPseudoFile() = default;

zx_status_t BufferedPseudoFile::CreateConnection(
    uint32_t flags, std::unique_ptr<Connection>* connection) {
  std::vector<uint8_t> output;
  if (Flags::IsReadable(flags)) {
    zx_status_t status = read_handler_(&output);
    if (status != ZX_OK) {
      return status;
    }
  }
  *connection = std::make_unique<BufferedPseudoFile::Content>(
      this, flags, std::move(output));
  return ZX_OK;
}

uint32_t BufferedPseudoFile::GetAdditionalAllowedFlags() const {
  auto allowed_flags = fuchsia::io::OPEN_RIGHT_READABLE;
  if (write_handler_ != nullptr) {
    allowed_flags |=
        fuchsia::io::OPEN_RIGHT_WRITABLE | fuchsia::io::OPEN_FLAG_TRUNCATE;
  }
  return allowed_flags;
}

uint32_t BufferedPseudoFile::GetProhibitiveFlags() const {
  return fuchsia::io::OPEN_FLAG_APPEND;
}

BufferedPseudoFile::Content::Content(BufferedPseudoFile* file, uint32_t flags,
                                     std::vector<uint8_t> content)
    : Connection(flags),
      file_(file),
      buffer_(std::move(content)),
      flags_(flags) {
  SetInputLength(buffer_.size());
}

BufferedPseudoFile::Content::~Content() {
  if (!dirty_) {
    return;
  }
  file_->write_handler_(std::move(buffer_));
};

zx_status_t BufferedPseudoFile::Content::ReadAt(
    uint64_t count, uint64_t offset, std::vector<uint8_t>* out_data) {
  if (offset >= buffer_.size()) {
    return ZX_OK;
  }
  size_t actual = std::min(buffer_.size() - offset, count);
  out_data->resize(actual);
  std::copy_n(buffer_.begin() + offset, actual, out_data->begin());
  return ZX_OK;
}

uint32_t BufferedPseudoFile::Content::GetAdditionalAllowedFlags() const {
  return file_->GetAdditionalAllowedFlags();
}

uint32_t BufferedPseudoFile::Content::GetProhibitiveFlags() const {
  return file_->GetProhibitiveFlags();
}

zx_status_t BufferedPseudoFile::Content::WriteAt(std::vector<uint8_t> data,
                                                 uint64_t offset,
                                                 uint64_t* out_actual) {
  if (offset >= file_->buffer_capacity_) {
    *out_actual = 0u;
    return ZX_OK;
  }

  size_t actual = std::min(data.size(), file_->buffer_capacity_ - offset);
  if (actual == 0) {
    *out_actual = 0u;
    return ZX_OK;
  }

  dirty_ = true;
  if (actual > buffer_.size() - offset) {
    SetInputLength(offset + actual);
  }

  std::copy_n(data.begin(), actual, buffer_.begin() + offset);
  *out_actual = actual;
  return ZX_OK;
}

zx_status_t BufferedPseudoFile::Content::Truncate(uint64_t length) {
  if (length > file_->buffer_capacity_) {
    return ZX_ERR_NO_SPACE;
  }

  dirty_ = true;
  SetInputLength(length);
  return ZX_OK;
}

void BufferedPseudoFile::Content::SetInputLength(size_t length) {
  ZX_DEBUG_ASSERT(length <= file_->buffer_capacity_);

  buffer_.resize(length);
}

zx_status_t BufferedPseudoFile::Content::Bind(zx::channel request,
                                              async_dispatcher_t* dispatcher) {
  // only one connection allowed per content
  ZX_DEBUG_ASSERT(connections().size() == 0);

  std::unique_ptr<Connection> connection;
  zx_status_t status = CreateConnection(flags_, &connection);
  if (status != ZX_OK) {
    SendOnOpenEventOnError(flags_, std::move(request), status);
    return status;
  }
  status = connection->Bind(std::move(request), dispatcher);
  AddConnection(std::move(connection));
  return status;
}

zx_status_t BufferedPseudoFile::Content::Close(Connection* connection) {
  Node::Close(connection);
  return file_->Close(this);
}

void BufferedPseudoFile::Content::SendOnOpenEvent(zx_status_t status) {
  ZX_DEBUG_ASSERT(connections().size() == 1);

  connections()[0]->SendOnOpenEvent(status);
}

}  // namespace vfs