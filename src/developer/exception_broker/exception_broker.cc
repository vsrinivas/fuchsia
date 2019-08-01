// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/exception_broker/exception_broker.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/status.h>

namespace fuchsia {
namespace exception {

std::unique_ptr<ExceptionBroker> ExceptionBroker::Create(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services) {
  return std::unique_ptr<ExceptionBroker>(new ExceptionBroker(services));
}

ExceptionBroker::ExceptionBroker(std::shared_ptr<sys::ServiceDirectory> services)
    : services_(std::move(services)), weak_factory_(this) {
  FXL_DCHECK(services_);
}

fxl::WeakPtr<ExceptionBroker> ExceptionBroker::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void ExceptionBroker::OnException(zx::exception exception, ExceptionInfo info,
                                  OnExceptionCallback cb) {
  // For now, pass on through the exception to fuchsia.crash.Analyzer.
  FXL_DCHECK(services_);

  zx::process process;
  if (zx_status_t status = exception.get_process(&process); status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Could not get process from exception.";
    return;
  }

  zx::thread thread;
  if (zx_status_t status = exception.get_thread(&thread); status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Could not get thread from exception.";
    return;
  }

  // Create a new connection to crashpad and give a unique id to it.
  uint64_t id = next_connection_id_++;
  auto analyzer_ptr = services_->Connect<fuchsia::crash::Analyzer>();

  // We insert the connection and keep track of it.
  connections_[id] = std::move(analyzer_ptr);

  // Get the ref.
  auto& analyzer = connections_[id];

  analyzer.set_error_handler([id, broker = GetWeakPtr()](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.crash.Analyzer";

    // If the broker is not there anymore, there is nothing more we can do.
    if (!broker)
      return;

    // Remove the connection.
    broker->connections_.erase(id);
  });

  // We pass the ownership of the exception to the lambda, otherwise the exception would be closed
  // on destruction before the callback for |OnNativeException| gets a change to run.
  analyzer->OnNativeException(std::move(process), std::move(thread),
                              [id, broker = GetWeakPtr(), exception = std::move(exception)](
                                  fuchsia::crash::Analyzer_OnNativeException_Result result) {
                                if (result.is_err()) {
                                  FX_PLOGS(WARNING, result.err())
                                      << "Failed to pass on the exception to the analyzer.";
                                }

                                // If the broker is not there anymore, there is nothing more
                                // we can do.
                                if (!broker)
                                  return;

                                // Remove the connection.
                                broker->connections_.erase(id);
                              });

  // Tell the caller that we're done.
  cb();
}

}  // namespace exception
}  // namespace fuchsia
