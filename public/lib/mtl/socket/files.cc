// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/socket/files.h"

#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include "lib/fidl/c/waiter/async_waiter.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/fxl/files/file_descriptor.h"

namespace mtl {
namespace {

// CopyToFileHandler -----------------------------------------------------------

class CopyToFileHandler {
 public:
  CopyToFileHandler(mx::socket source,
                    fxl::UniqueFD destination,
                    fxl::RefPtr<fxl::TaskRunner> task_runner,
                    const std::function<void(bool, fxl::UniqueFD)>& callback);

 private:
  ~CopyToFileHandler();

  void SendCallback(bool value);
  void OnHandleReady(mx_status_t result);
  static void WaitComplete(mx_status_t result,
                           mx_signals_t pending,
                           uint64_t count,
                           void* context);

  mx::socket source_;
  fxl::UniqueFD destination_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  std::function<void(bool, fxl::UniqueFD)> callback_;
  const FidlAsyncWaiter* waiter_;
  FidlAsyncWaitID wait_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CopyToFileHandler);
};

CopyToFileHandler::CopyToFileHandler(
    mx::socket source,
    fxl::UniqueFD destination,
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    const std::function<void(bool, fxl::UniqueFD)>& callback)
    : source_(std::move(source)),
      destination_(std::move(destination)),
      task_runner_(std::move(task_runner)),
      callback_(callback),
      waiter_(fidl::GetDefaultAsyncWaiter()),
      wait_id_(0) {
  task_runner_->PostTask([this]() { OnHandleReady(MX_OK); });
}

CopyToFileHandler::~CopyToFileHandler() {}

void CopyToFileHandler::SendCallback(bool value) {
  FXL_DCHECK(!wait_id_);
  auto callback = callback_;
  auto destination = std::move(destination_);
  delete this;
  callback(value, std::move(destination));
}

void CopyToFileHandler::OnHandleReady(mx_status_t result) {
  if (result == MX_OK) {
    std::vector<char> buffer(64 * 1024);
    size_t size = 0;
    result = source_.read(0u, buffer.data(), buffer.size(), &size);
    if (result == MX_OK) {
      bool write_success =
          fxl::WriteFileDescriptor(destination_.get(), buffer.data(), size);
      if (!write_success) {
        SendCallback(false);
      } else {
        task_runner_->PostTask([this]() { OnHandleReady(MX_OK); });
      }
      return;
    }
  }
  if (result == MX_ERR_PEER_CLOSED) {
    SendCallback(true);
    return;
  }
  if (result == MX_ERR_SHOULD_WAIT) {
    wait_id_ = waiter_->AsyncWait(source_.get(),
                                  MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED,
                                  MX_TIME_INFINITE, &WaitComplete, this);
    return;
  }
  SendCallback(false);
}

void CopyToFileHandler::WaitComplete(mx_status_t result,
                                     mx_signals_t pending,
                                     uint64_t count,
                                     void* context) {
  CopyToFileHandler* handler = static_cast<CopyToFileHandler*>(context);
  handler->wait_id_ = 0;
  handler->OnHandleReady(result);
}

// CopyFromFileHandler ---------------------------------------------------------

class CopyFromFileHandler {
 public:
  CopyFromFileHandler(fxl::UniqueFD source,
                      mx::socket destination,
                      fxl::RefPtr<fxl::TaskRunner> task_runner,
                      const std::function<void(bool, fxl::UniqueFD)>& callback);

 private:
  ~CopyFromFileHandler();

  void SendCallback(bool value);
  void FillBuffer();
  void OnHandleReady(mx_status_t result);
  static void WaitComplete(mx_status_t result,
                           mx_signals_t pending,
                           uint64_t count,
                           void* context);

  fxl::UniqueFD source_;
  mx::socket destination_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  std::function<void(bool, fxl::UniqueFD)> callback_;
  const FidlAsyncWaiter* waiter_;
  std::vector<char> buffer_;
  size_t buffer_offset_;
  size_t buffer_end_;
  FidlAsyncWaitID wait_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CopyFromFileHandler);
};

CopyFromFileHandler::CopyFromFileHandler(
    fxl::UniqueFD source,
    mx::socket destination,
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    const std::function<void(bool, fxl::UniqueFD)>& callback)
    : source_(std::move(source)),
      destination_(std::move(destination)),
      task_runner_(std::move(task_runner)),
      callback_(callback),
      waiter_(fidl::GetDefaultAsyncWaiter()),
      buffer_(64 * 1024),
      wait_id_(0) {
  task_runner_->PostTask([this]() { FillBuffer(); });
}

CopyFromFileHandler::~CopyFromFileHandler() {}

void CopyFromFileHandler::SendCallback(bool value) {
  FXL_DCHECK(!wait_id_);
  auto callback = callback_;
  auto source = std::move(source_);
  delete this;
  callback(value, std::move(source));
}

void CopyFromFileHandler::FillBuffer() {
  ssize_t bytes_read =
      fxl::ReadFileDescriptor(source_.get(), buffer_.data(), buffer_.size());
  if (bytes_read <= 0) {
    SendCallback(bytes_read == 0);
    return;
  }
  buffer_offset_ = 0;
  buffer_end_ = bytes_read;
  OnHandleReady(MX_OK);
}

void CopyFromFileHandler::OnHandleReady(mx_status_t result) {
  if (result == MX_OK) {
    size_t bytes_written = 0;
    result = destination_.write(0u, buffer_.data() + buffer_offset_,
                                buffer_end_ - buffer_offset_, &bytes_written);
    if (result == MX_OK) {
      buffer_offset_ += bytes_written;
      if (buffer_offset_ == buffer_end_) {
        task_runner_->PostTask([this]() { FillBuffer(); });
      } else {
        task_runner_->PostTask([this]() { OnHandleReady(MX_OK); });
      }
      return;
    }
  }
  if (result == MX_ERR_SHOULD_WAIT) {
    wait_id_ = waiter_->AsyncWait(destination_.get(),
                                  MX_SOCKET_WRITABLE | MX_SOCKET_PEER_CLOSED,
                                  MX_TIME_INFINITE, &WaitComplete, this);
    return;
  }
  SendCallback(false);
}

void CopyFromFileHandler::WaitComplete(mx_status_t result,
                                       mx_signals_t pending,
                                       uint64_t count,
                                       void* context) {
  CopyFromFileHandler* handler = static_cast<CopyFromFileHandler*>(context);
  handler->wait_id_ = 0;
  handler->OnHandleReady(result);
}

}  // namespace

void CopyToFileDescriptor(
    mx::socket source,
    fxl::UniqueFD destination,
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    const std::function<void(bool, fxl::UniqueFD)>& callback) {
  new CopyToFileHandler(std::move(source), std::move(destination), task_runner,
                        callback);
}

void CopyFromFileDescriptor(
    fxl::UniqueFD source,
    mx::socket destination,
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    const std::function<void(bool, fxl::UniqueFD)>& callback) {
  new CopyFromFileHandler(std::move(source), std::move(destination),
                          task_runner, callback);
}

}  // namespace mtl
