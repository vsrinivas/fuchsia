// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/socket/files.h"

#include <lib/async/cpp/wait.h>
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
  CopyToFileHandler(zx::socket source, fxl::UniqueFD destination,
                    async_dispatcher_t* dispatcher,
                    const std::function<void(bool, fxl::UniqueFD)>& callback);

 private:
  ~CopyToFileHandler();

  void SendCallback(bool value);
  void OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal_t* signal);

  zx::socket source_;
  fxl::UniqueFD destination_;
  std::function<void(bool, fxl::UniqueFD)> callback_;
  async::WaitMethod<CopyToFileHandler, &CopyToFileHandler::OnHandleReady> wait_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CopyToFileHandler);
};

CopyToFileHandler::CopyToFileHandler(
    zx::socket source, fxl::UniqueFD destination, async_dispatcher_t* dispatcher,
    const std::function<void(bool, fxl::UniqueFD)>& callback)
    : source_(std::move(source)),
      destination_(std::move(destination)),
      callback_(callback),
      wait_(this, source_.get(), ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED) {
  zx_status_t status = wait_.Begin(dispatcher);
  FXL_CHECK(status == ZX_OK);
}

CopyToFileHandler::~CopyToFileHandler() = default;

void CopyToFileHandler::SendCallback(bool value) {
  auto callback = callback_;
  auto destination = std::move(destination_);
  delete this;
  callback(value, std::move(destination));
}

void CopyToFileHandler::OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                      zx_status_t status,
                                      const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    SendCallback(false);
    return;
  }

  if (signal->observed & ZX_ERR_PEER_CLOSED) {
    SendCallback(true);
    return;
  }

  std::vector<char> buffer(64 * 1024);
  size_t size = 0;
  status = source_.read(0u, buffer.data(), buffer.size(), &size);

  if (status == ZX_ERR_PEER_CLOSED) {
    SendCallback(true);
    return;
  }

  if (status != ZX_ERR_SHOULD_WAIT) {
    if (status != ZX_OK ||
        !fxl::WriteFileDescriptor(destination_.get(), buffer.data(), size)) {
      SendCallback(false);
      return;
    }
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    SendCallback(false);
  }
}

// CopyFromFileHandler ---------------------------------------------------------

class CopyFromFileHandler {
 public:
  CopyFromFileHandler(fxl::UniqueFD source, zx::socket destination,
                      async_dispatcher_t* dispatcher,
                      const std::function<void(bool, fxl::UniqueFD)>& callback);

 private:
  ~CopyFromFileHandler();

  void SendCallback(bool value);
  void OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal_t* signal);

  fxl::UniqueFD source_;
  zx::socket destination_;
  async::WaitMethod<CopyFromFileHandler, &CopyFromFileHandler::OnHandleReady>
      wait_;
  std::function<void(bool, fxl::UniqueFD)> callback_;
  std::vector<char> buffer_;
  size_t buffer_offset_;
  size_t buffer_end_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CopyFromFileHandler);
};

CopyFromFileHandler::CopyFromFileHandler(
    fxl::UniqueFD source, zx::socket destination, async_dispatcher_t* dispatcher,
    const std::function<void(bool, fxl::UniqueFD)>& callback)
    : source_(std::move(source)),
      destination_(std::move(destination)),
      wait_(this, destination_.get(),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED),
      callback_(callback),
      buffer_(64 * 1024) {
  zx_status_t status = wait_.Begin(dispatcher);
  FXL_CHECK(status == ZX_OK);
}

CopyFromFileHandler::~CopyFromFileHandler() = default;

void CopyFromFileHandler::SendCallback(bool value) {
  auto callback = callback_;
  auto source = std::move(source_);
  delete this;
  callback(value, std::move(source));
}

void CopyFromFileHandler::OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                        zx_status_t status,
                                        const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    SendCallback(false);
    return;
  }

  if (signal->observed & ZX_ERR_PEER_CLOSED) {
    SendCallback(false);
    return;
  }

  if (buffer_offset_ == buffer_end_) {
    ssize_t bytes_read =
        fxl::ReadFileDescriptor(source_.get(), buffer_.data(), buffer_.size());
    if (bytes_read <= 0) {
      SendCallback(bytes_read == 0);
      return;
    }
    buffer_offset_ = 0;
    buffer_end_ = bytes_read;
  }

  size_t bytes_written = 0;
  status = destination_.write(0u, buffer_.data() + buffer_offset_,
                              buffer_end_ - buffer_offset_, &bytes_written);

  if (status != ZX_ERR_SHOULD_WAIT) {
    if (status != ZX_OK) {
      SendCallback(false);
      return;
    }
    buffer_offset_ += bytes_written;
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    SendCallback(false);
  }
}

}  // namespace

void CopyToFileDescriptor(
    zx::socket source, fxl::UniqueFD destination, async_dispatcher_t* dispatcher,
    const std::function<void(bool, fxl::UniqueFD)>& callback) {
  new CopyToFileHandler(std::move(source), std::move(destination), dispatcher,
                        callback);
}

void CopyFromFileDescriptor(
    fxl::UniqueFD source, zx::socket destination, async_dispatcher_t* dispatcher,
    const std::function<void(bool, fxl::UniqueFD)>& callback) {
  new CopyFromFileHandler(std::move(source), std::move(destination), dispatcher,
                          callback);
}

}  // namespace fsl
