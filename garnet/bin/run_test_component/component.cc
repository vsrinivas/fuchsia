// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/component.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/fd.h>
#include <lib/fpromise/barrier.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/handle.h>
#include <unistd.h>
#include <zircon/processargs.h>

#include <memory>

#include <fbl/unique_fd.h>

#include "garnet/bin/run_test_component/output_collector.h"

namespace run {

namespace {

void AddOutputFileDescriptor(int fileno, FILE* out_file, async_dispatcher_t* dispatcher,
                             std::unique_ptr<OutputCollector>* out_output_collector,
                             fuchsia::sys::FileDescriptorPtr* out_file_descriptor) {
  auto output_collector = OutputCollector::Create();
  fuchsia::sys::FileDescriptorPtr file_descriptor =
      std::make_unique<fuchsia::sys::FileDescriptor>();
  file_descriptor->type0 = PA_HND(PA_FD, fileno);
  file_descriptor->handle0 = output_collector->TakeServer();
  *out_file_descriptor = std::move(file_descriptor);
  output_collector->CollectOutput(
      [out_file, dispatcher](std::string s) {
        async::PostTask(dispatcher, [s = std::move(s), out_file]() {
          // don't use fprintf as that truncates the output on first zero.
          fwrite(s.data(), 1, s.length(), out_file);
          fflush(out_file);
        });
      },
      dispatcher);

  *out_output_collector = std::move(output_collector);
}

}  // namespace

std::unique_ptr<Component> Component::Launch(const fuchsia::sys::LauncherPtr& launcher,
                                             fuchsia::sys::LaunchInfo launch_info,
                                             async_dispatcher_t* dispatcher) {
  std::unique_ptr<OutputCollector> out, err;

  if (!launch_info.out) {
    AddOutputFileDescriptor(STDOUT_FILENO, stdout, dispatcher, &out, &launch_info.out);
  }

  if (!launch_info.err) {
    AddOutputFileDescriptor(STDERR_FILENO, stderr, dispatcher, &err, &launch_info.err);
  }

  auto svc = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  fuchsia::sys::ComponentControllerPtr contoller;
  launcher->CreateComponent(std::move(launch_info), contoller.NewRequest());
  return std::make_unique<Component>(std::move(out), std::move(err), std::move(contoller),
                                     std::move(svc));
}

fpromise::promise<> Component::SignalWhenOutputCollected() {
  return stderr_->SignalWhenDone().and_then(stdout_->SignalWhenDone());
}

Component::Component(std::unique_ptr<OutputCollector> out, std::unique_ptr<OutputCollector> err,
                     fuchsia::sys::ComponentControllerPtr controller,
                     std::shared_ptr<sys::ServiceDirectory> svc)
    : stdout_(std::move(out)),
      stderr_(std::move(err)),
      controller_(std::move(controller)),
      svc_(std::move(svc)) {}

}  // namespace run
