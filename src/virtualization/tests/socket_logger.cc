// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket_logger.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

#include <future>

#include <src/lib/fsl/socket/socket_drainer.h>

#include "logger.h"

namespace {

// Posts a task to invoke |handler| and waits for it to complete.
//
// The current thread must not be a dispatcher thread, otherwise
// the call will deadlock.
zx_status_t PostTaskAndWait(async_dispatcher_t* dispatcher, fit::closure handler) {
  std::promise<void> signal;
  std::future<void> done = signal.get_future();
  zx_status_t status = async::PostTask(dispatcher, [&handler, &signal]() {
    handler();
    signal.set_value();
  });
  if (status != ZX_OK) {
    return status;
  }
  done.wait();
  return ZX_OK;
}

}  // namespace

class LogClient : public fsl::SocketDrainer::Client {
 public:
  explicit LogClient(Logger* logger) : logger_(logger) {}

  void OnDataAvailable(const void* data, size_t num_bytes) override {
    logger_->Write(std::string_view(static_cast<const char*>(data), num_bytes));
  }

  void OnDataComplete() override { logger_->Write("<guest serial connection closed>"); }

 private:
  Logger* logger_;
};

SocketLogger::SocketLogger(Logger* logger, zx::socket socket) {
  client_ = std::make_unique<LogClient>(logger);

  // Create a thread for the async loop.
  zx_status_t status = loop_.StartThread("serial_logger_loop");
  FX_CHECK(status == ZX_OK) << "Failed to start logging thread";

  // SocketDrainer (and hence LogClient) is thread-hostile, and must be
  // started and stopped on the remote thread.
  status = PostTaskAndWait(loop_.dispatcher(), [this, socket = std::move(socket)]() mutable {
    drainer_.emplace(client_.get(), loop_.dispatcher());
    drainer_->Start(std::move(socket));
  });
  ZX_ASSERT(status == ZX_OK);
}

SocketLogger::~SocketLogger() {
  // Destroy the drainer on the dispatcher's thread.
  zx_status_t status = PostTaskAndWait(loop_.dispatcher(), [this]() { drainer_.reset(); });
  ZX_ASSERT(status == ZX_OK);
}
