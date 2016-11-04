// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/application_manager/application_runner_holder.h"

#include <mx/vmo.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <utility>

namespace modular {
namespace {

// TODO(abarth): mxio should be able to do this for us.
// TODO(abarth): This copy should be asynchronous.
mx::vmo CopyToVMO(int fd) {
  struct stat64 st;
  fstat64(fd, &st);
  size_t size = st.st_size;

  mx::vmo result;
  if (!mx::vmo::create(size, 0, &result))
    return mx::vmo();

  constexpr size_t kBufferSize = 1 << 16;
  char buffer[kBufferSize];
  size_t offset = 0;
  while (offset < size) {
    ssize_t bytes_read = read(fd, buffer, kBufferSize);
    if (bytes_read < 0)
      return mx::vmo();
    mx_size_t actual = 0;
    mx_status_t rv = result.write(buffer, offset, bytes_read, &actual);
    if (rv < 0 || actual != static_cast<mx_size_t>(bytes_read))
      return mx::vmo();
    offset += bytes_read;
  }

  return result;
}

}  // namespace

ApplicationRunnerHolder::ApplicationRunnerHolder(
    ServiceProviderPtr services,
    ApplicationControllerPtr controller)
    : services_(std::move(services)), controller_(std::move(controller)) {
  services->ConnectToService(ApplicationRunner::Name_,
                             fidl::GetProxy(&runner_).PassMessagePipe());
}

ApplicationRunnerHolder::~ApplicationRunnerHolder() = default;

void ApplicationRunnerHolder::StartApplication(
    ftl::UniqueFD fd,
    ApplicationStartupInfoPtr startup_info,
    fidl::InterfaceRequest<ApplicationController> controller) {
  mx::vmo data = CopyToVMO(fd.get());
  if (!data) {
    FTL_LOG(ERROR) << "Cannot run " << startup_info->url
                   << " because URL is unreadable.";
    return;
  }

  ApplicationPackagePtr package = ApplicationPackage::New();
  package->data = std::move(data);
  runner_->StartApplication(std::move(package), std::move(startup_info),
                            std::move(controller));
}

}  // namespace modular
