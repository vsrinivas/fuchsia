// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/data_pipe/files.h"

#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <utility>

#include "lib/fidl/c/waiter/async_waiter.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/ftl/files/file_descriptor.h"

namespace mtl {
namespace {

// CopyToFileHandler -----------------------------------------------------------

class CopyToFileHandler {
 public:
  CopyToFileHandler(mx::datapipe_consumer source,
                    ftl::UniqueFD destination,
                    ftl::RefPtr<ftl::TaskRunner> task_runner,
                    const std::function<void(bool, ftl::UniqueFD)>& callback);

 private:
  ~CopyToFileHandler();

  void SendCallback(bool value);
  void OnHandleReady(mx_status_t result);
  static void WaitComplete(mx_status_t result,
                           mx_signals_t pending,
                           void* context);

  mx::datapipe_consumer source_;
  ftl::UniqueFD destination_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  std::function<void(bool, ftl::UniqueFD)> callback_;
  const FidlAsyncWaiter* waiter_;
  FidlAsyncWaitID wait_id_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CopyToFileHandler);
};

CopyToFileHandler::CopyToFileHandler(
    mx::datapipe_consumer source,
    ftl::UniqueFD destination,
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    const std::function<void(bool, ftl::UniqueFD)>& callback)
    : source_(std::move(source)),
      destination_(std::move(destination)),
      task_runner_(std::move(task_runner)),
      callback_(callback),
      waiter_(fidl::GetDefaultAsyncWaiter()),
      wait_id_(0) {
  task_runner_->PostTask([this]() { OnHandleReady(NO_ERROR); });
}

CopyToFileHandler::~CopyToFileHandler() {}

void CopyToFileHandler::SendCallback(bool value) {
  FTL_DCHECK(!wait_id_);
  auto callback = callback_;
  auto destination = std::move(destination_);
  delete this;
  callback(value, std::move(destination));
}

void CopyToFileHandler::OnHandleReady(mx_status_t result) {
  if (result == NO_ERROR) {
    const void* buffer = nullptr;
    mx_size_t size = 0;
    result =
        source_.begin_read(0u, reinterpret_cast<uintptr_t*>(&buffer), &size);
    if (result == NO_ERROR) {
      bool write_success = ftl::WriteFileDescriptor(
          destination_.get(), static_cast<const char*>(buffer), size);
      result = source_.end_read(size);
      if (!write_success || result != NO_ERROR) {
        SendCallback(false);
      } else {
        task_runner_->PostTask([this]() { OnHandleReady(NO_ERROR); });
      }
      return;
    }
  }
  if (result == ERR_REMOTE_CLOSED) {
    SendCallback(true);
    return;
  }
  if (result == ERR_SHOULD_WAIT) {
    wait_id_ = waiter_->AsyncWait(source_.get(),
                                  MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                  MX_TIME_INFINITE, &WaitComplete, this);
    return;
  }
  SendCallback(false);
}

void CopyToFileHandler::WaitComplete(mx_status_t result,
                                     mx_signals_t pending,
                                     void* context) {
  CopyToFileHandler* handler = static_cast<CopyToFileHandler*>(context);
  handler->wait_id_ = 0;
  handler->OnHandleReady(result);
}

// CopyFromFileHandler ---------------------------------------------------------

class CopyFromFileHandler {
 public:
  CopyFromFileHandler(ftl::UniqueFD source,
                      mx::datapipe_producer destination,
                      ftl::RefPtr<ftl::TaskRunner> task_runner,
                      const std::function<void(bool, ftl::UniqueFD)>& callback);

 private:
  ~CopyFromFileHandler();

  void SendCallback(bool value);
  void OnHandleReady(mx_status_t result);
  static void WaitComplete(mx_status_t result,
                           mx_signals_t pending,
                           void* context);

  ftl::UniqueFD source_;
  mx::datapipe_producer destination_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  std::function<void(bool, ftl::UniqueFD)> callback_;
  const FidlAsyncWaiter* waiter_;
  FidlAsyncWaitID wait_id_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CopyFromFileHandler);
};

CopyFromFileHandler::CopyFromFileHandler(
    ftl::UniqueFD source,
    mx::datapipe_producer destination,
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    const std::function<void(bool, ftl::UniqueFD)>& callback)
    : source_(std::move(source)),
      destination_(std::move(destination)),
      task_runner_(std::move(task_runner)),
      callback_(callback),
      waiter_(fidl::GetDefaultAsyncWaiter()),
      wait_id_(0) {
  task_runner_->PostTask([this]() { OnHandleReady(NO_ERROR); });
}

CopyFromFileHandler::~CopyFromFileHandler() {}

void CopyFromFileHandler::SendCallback(bool value) {
  FTL_DCHECK(!wait_id_);
  auto callback = callback_;
  auto source = std::move(source_);
  delete this;
  callback(value, std::move(source));
}

void CopyFromFileHandler::OnHandleReady(mx_status_t result) {
  if (result == NO_ERROR) {
    void* buffer = nullptr;
    mx_size_t size = 0;
    result = destination_.begin_write(0u, reinterpret_cast<uintptr_t*>(&buffer),
                                      &size);
    if (result == NO_ERROR) {
      FTL_DCHECK(size < static_cast<uint32_t>(std::numeric_limits<int>::max()));
      ssize_t bytes_read = ftl::ReadFileDescriptor(
          source_.get(), static_cast<char*>(buffer), size);
      result = destination_.end_write(std::max<ssize_t>(0l, bytes_read));
      if (bytes_read < 0 || result != NO_ERROR) {
        SendCallback(false);
      } else if (static_cast<mx_size_t>(bytes_read) < size) {
        // Reached EOF. Stop the process.
        SendCallback(true);
      } else {
        task_runner_->PostTask([this]() { OnHandleReady(NO_ERROR); });
      }
      return;
    }
  }
  if (result == ERR_SHOULD_WAIT) {
    wait_id_ = waiter_->AsyncWait(destination_.get(),
                                  MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED,
                                  MX_TIME_INFINITE, &WaitComplete, this);
    return;
  }
  SendCallback(false);
}

void CopyFromFileHandler::WaitComplete(mx_status_t result,
                                       mx_signals_t pending,
                                       void* context) {
  CopyFromFileHandler* handler = static_cast<CopyFromFileHandler*>(context);
  handler->wait_id_ = 0;
  handler->OnHandleReady(result);
}

}  // namespace

void CopyToFileDescriptor(
    mx::datapipe_consumer source,
    ftl::UniqueFD destination,
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    const std::function<void(bool, ftl::UniqueFD)>& callback) {
  new CopyToFileHandler(std::move(source), std::move(destination), task_runner,
                        callback);
}

void CopyFromFileDescriptor(
    ftl::UniqueFD source,
    mx::datapipe_producer destination,
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    const std::function<void(bool, ftl::UniqueFD)>& callback) {
  new CopyFromFileHandler(std::move(source), std::move(destination),
                          task_runner, callback);
}

}  // namespace mtl
