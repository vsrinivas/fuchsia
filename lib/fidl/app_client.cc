// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/app_client.h"

#include <magenta/processargs.h>
#include <mxio/limits.h>
#include <mxio/util.h>

#include <fcntl.h>

#include "lib/app/fidl/flat_namespace.fidl.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/unique_fd.h"

namespace modular {
namespace {

mx::channel CloneChannel(int fd) {
  mx_handle_t handle[MXIO_MAX_HANDLES];
  uint32_t type[MXIO_MAX_HANDLES];

  mx_status_t r = mxio_clone_fd(fd, 0, handle, type);
  if (r < 0 || r == 0) {
    return mx::channel();
  }

  if (type[0] != PA_MXIO_REMOTE) {
    for (int i = 0; i < r; ++i) {
      mx_handle_close(handle[i]);
    }
    return mx::channel();
  }

  // Close any extra handles.
  for (int i = 1; i < r; ++i) {
    mx_handle_close(handle[i]);
  }

  return mx::channel(handle[0]);
}

}  // namespace

AppClientBase::AppClientBase(app::ApplicationLauncher* const launcher,
                             AppConfigPtr config,
                             std::string data_origin)
    : url_(config->url) {
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->services = services_.NewRequest();
  launch_info->url = config->url;
  launch_info->arguments = config->args.Clone();

  if (!data_origin.empty()) {
    if (!files::CreateDirectory(data_origin)) {
      FTL_LOG(ERROR) << "Unable to create directory at " << data_origin;
      return;
    }
    launch_info->flat_namespace = app::FlatNamespace::New();
    launch_info->flat_namespace->paths.resize(1);
    launch_info->flat_namespace->paths[0] = "/data";
    launch_info->flat_namespace->directories.resize(1);

    ftl::UniqueFD dir(open(data_origin.c_str(), O_DIRECTORY | O_RDONLY));
    if (!dir.is_valid()) {
      FTL_LOG(ERROR) << "Unable to open directory at " << data_origin
                     << ". errno: " << errno;
      return;
    }

    launch_info->flat_namespace->directories[0] = CloneChannel(dir.get());
    if (!launch_info->flat_namespace->directories[0]) {
      FTL_LOG(ERROR) << "Unable create a handle from  " << data_origin;
      return;
    }
  }
  launcher->CreateApplication(std::move(launch_info), app_.NewRequest());
}

AppClientBase::~AppClientBase() = default;

void AppClientBase::AppTerminate(const std::function<void()>& done,
                                 ftl::TimeDelta timeout) {
  auto called = std::make_shared<bool>(false);
  auto cont = [this, called, done](const bool from_timeout) {
    if (*called) {
      return;
    }

    *called = true;

    if (from_timeout) {
      FTL_LOG(WARNING) << "AppTerminate() timed out for " << url_;
    }

    app_.reset();
    services_.reset();

    ServiceReset();

    done();
  };

  auto cont_timeout = [cont] {
    cont(true);
  };

  auto cont_normal = [cont] {
    cont(false);
  };

  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(cont_timeout, timeout);
  ServiceTerminate(cont_normal);
}

void AppClientBase::SetAppErrorHandler(
    const std::function<void()>& error_handler) {
  app_.set_connection_error_handler(error_handler);
}

void AppClientBase::ServiceTerminate(const std::function<void()>& /* done */) {}

void AppClientBase::ServiceReset() {}

template <>
void AppClient<Lifecycle>::ServiceTerminate(
    const std::function<void()>& done) {
  SetAppErrorHandler(done);
  if (primary_service())
    primary_service()->Terminate();
}

}  // namespace modular
