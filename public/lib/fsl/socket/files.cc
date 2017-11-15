// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/socket/files.h"

#include <async/auto_wait.h>
#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include "lib/fxl/files/file_descriptor.h"

namespace fsl {
namespace {

// CopyToFileHandler -----------------------------------------------------------

class CopyToFileHandler {
 public:
  CopyToFileHandler(zx::socket source,
                    fxl::UniqueFD destination,
                    async_t* async,
                    const std::function<void(bool, fxl::UniqueFD)>& callback);

 private:
  ~CopyToFileHandler();

  void SendCallback(bool value);
  async_wait_result_t OnHandleReady(
      async_t* async, zx_status_t status, const zx_packet_signal_t* signal);

  zx::socket source_;
  fxl::UniqueFD destination_;
  std::function<void(bool, fxl::UniqueFD)> callback_;
  async::AutoWait wait_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CopyToFileHandler);
};

CopyToFileHandler::CopyToFileHandler(
    zx::socket source,
    fxl::UniqueFD destination,
    async_t* async,
    const std::function<void(bool, fxl::UniqueFD)>& callback)
    : source_(std::move(source)),
      destination_(std::move(destination)),
      callback_(callback),
      wait_(async, source_.get(), ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED) {
  wait_.set_handler(fbl::BindMember(this, &CopyToFileHandler::OnHandleReady));
  zx_status_t status = wait_.Begin();
  FXL_CHECK(status == ZX_OK);
}

CopyToFileHandler::~CopyToFileHandler() = default;

void CopyToFileHandler::SendCallback(bool value) {
  auto callback = callback_;
  auto destination = std::move(destination_);
  delete this;
  callback(value, std::move(destination));
}

async_wait_result_t CopyToFileHandler::OnHandleReady(
    async_t* async, zx_status_t status, const zx_packet_signal_t* signal) {
  if (signal->observed & ZX_ERR_PEER_CLOSED) {
    SendCallback(true);
    return ASYNC_WAIT_FINISHED;
  }

  std::vector<char> buffer(64 * 1024);
  size_t size = 0;
  status = source_.read(0u, buffer.data(), buffer.size(), &size);

  if (status == ZX_ERR_SHOULD_WAIT) {
    return ASYNC_WAIT_AGAIN;
  }

  if (status == ZX_ERR_PEER_CLOSED) {
    SendCallback(true);
    return ASYNC_WAIT_FINISHED;
  }

  if (status != ZX_OK
      || !fxl::WriteFileDescriptor(destination_.get(), buffer.data(), size)) {
    SendCallback(false);
    return ASYNC_WAIT_FINISHED;
  }

  return ASYNC_WAIT_AGAIN;
}

// CopyFromFileHandler ---------------------------------------------------------

class CopyFromFileHandler {
 public:
  CopyFromFileHandler(fxl::UniqueFD source,
                      zx::socket destination,
                      async_t* async,
                      const std::function<void(bool, fxl::UniqueFD)>& callback);

 private:
  ~CopyFromFileHandler();

  void SendCallback(bool value);
  async_wait_result_t OnHandleReady(
    async_t* async, zx_status_t status, const zx_packet_signal_t* signal);

  fxl::UniqueFD source_;
  zx::socket destination_;
  async::AutoWait wait_;
  std::function<void(bool, fxl::UniqueFD)> callback_;
  std::vector<char> buffer_;
  size_t buffer_offset_;
  size_t buffer_end_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CopyFromFileHandler);
};

CopyFromFileHandler::CopyFromFileHandler(
    fxl::UniqueFD source,
    zx::socket destination,
    async_t* async,
    const std::function<void(bool, fxl::UniqueFD)>& callback)
    : source_(std::move(source)),
      destination_(std::move(destination)),
      wait_(async, destination_.get(), ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED),
      callback_(callback),
      buffer_(64 * 1024) {
  wait_.set_handler(fbl::BindMember(this, &CopyFromFileHandler::OnHandleReady));
  zx_status_t status = wait_.Begin();
  FXL_CHECK(status == ZX_OK);
}

CopyFromFileHandler::~CopyFromFileHandler() = default;

void CopyFromFileHandler::SendCallback(bool value) {
  auto callback = callback_;
  auto source = std::move(source_);
  delete this;
  callback(value, std::move(source));
}

async_wait_result_t CopyFromFileHandler::OnHandleReady(
    async_t* async, zx_status_t status, const zx_packet_signal_t* signal) {
  if (signal->observed & ZX_ERR_PEER_CLOSED) {
    SendCallback(false);
    return ASYNC_WAIT_FINISHED;
  }

  if (buffer_offset_ == buffer_end_) {
    ssize_t bytes_read =
    fxl::ReadFileDescriptor(source_.get(), buffer_.data(), buffer_.size());
    if (bytes_read <= 0) {
      SendCallback(bytes_read == 0);
      return ASYNC_WAIT_FINISHED;
    }
    buffer_offset_ = 0;
    buffer_end_ = bytes_read;
  }

  size_t bytes_written = 0;
  status = destination_.write(0u, buffer_.data() + buffer_offset_,
                              buffer_end_ - buffer_offset_, &bytes_written);

  if (status == ZX_ERR_SHOULD_WAIT) {
    return ASYNC_WAIT_AGAIN;
  }

  if (status != ZX_OK) {
    SendCallback(false);
    return ASYNC_WAIT_FINISHED;
  }

  buffer_offset_ += bytes_written;
  return ASYNC_WAIT_AGAIN;
}

}  // namespace

void CopyToFileDescriptor(
    zx::socket source,
    fxl::UniqueFD destination,
    async_t* async,
    const std::function<void(bool, fxl::UniqueFD)>& callback) {
  new CopyToFileHandler(std::move(source), std::move(destination), async,
                        callback);
}

void CopyFromFileDescriptor(
    fxl::UniqueFD source,
    zx::socket destination,
    async_t* async,
    const std::function<void(bool, fxl::UniqueFD)>& callback) {
  new CopyFromFileHandler(std::move(source), std::move(destination),
                          async, callback);
}

}  // namespace fsl
