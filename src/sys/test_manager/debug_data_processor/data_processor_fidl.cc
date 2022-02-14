// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "data_processor_fidl.h"

#include <lib/fdio/fd.h>
#include <lib/syslog/cpp/macros.h>

#include "common.h"

namespace ftest_debug = fuchsia::test::debug;

using DataProcessorInitializer =
    fit::function<std::unique_ptr<AbstractDataProcessor>(fbl::unique_fd)>;

DataProcessorFidl::DataProcessorFidl(
    fidl::InterfaceRequest<ftest_debug::DebugDataProcessor> request, OnDoneCallback callback,
    DataProcessorInitializer initializer, async_dispatcher_t* dispatcher)
    : binding_(this),
      on_done_(std::move(callback)),
      processor_initializer_(std::move(initializer)),
      dispatcher_(dispatcher) {
  binding_.set_error_handler([this](zx_status_t status) { TearDown(status); });
  binding_.Bind(std::move(request), dispatcher);
}

void DataProcessorFidl::SetDirectory(fidl::InterfaceHandle<fuchsia::io::Directory> directory) {
  fbl::unique_fd fd;
  auto status = fdio_fd_create(directory.TakeChannel().release(), fd.reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create file descriptor: " << status;
    TearDown(ZX_ERR_NO_RESOURCES);
    return;
  }

  std::unique_ptr<AbstractDataProcessor> processor = processor_initializer_(std::move(fd));
  data_processor_.swap(processor);
}

void DataProcessorFidl::AddDebugVmos(::std::vector<::fuchsia::test::debug::DebugVmo> vmos,
                                     AddDebugVmosCallback callback) {
  if (!data_processor_) {
    FX_LOGS(ERROR) << "Debug VMOs sent before directory set";
    TearDown(ZX_ERR_INVALID_ARGS);
    return;
  }
  for (auto& debug_vmo : vmos) {
    DataSinkDump dump{.data_sink = std::move(debug_vmo.data_sink), .vmo = std::move(debug_vmo.vmo)};
    data_processor_->ProcessData(std::move(debug_vmo.test_url), std::move(dump));
  }
  callback();
}

void DataProcessorFidl::Finish(FinishCallback callback) {
  wait_for_completion_.set_object(data_processor_->GetIdleEvent()->get());
  wait_for_completion_.set_trigger(IDLE_SIGNAL);
  wait_for_completion_.Begin(
      dispatcher_, [&, this, callback = std::move(callback)](
                       async_dispatcher_t* dispatcher, async::WaitOnce* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
        callback();
        FX_LOGS(INFO) << "finish returned";
        TearDown(ZX_ERR_PEER_CLOSED);
      });
}

void DataProcessorFidl::TearDown(zx_status_t epitaph) {
  binding_.Close(epitaph);
  if (on_done_) {
    on_done_();
  }
}
