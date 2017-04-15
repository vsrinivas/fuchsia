// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <magenta/process.h>
#include <unistd.h>

#include "application/lib/app/connect.h"
#include "application_context.h"
#include "lib/ftl/logging.h"
#include "magma/src/magma_util/platform/magenta/magenta_platform_ioctl.h"
#include "magma_util/macros.h"

namespace faux {

ApplicationContext::ApplicationContext(fidl::InterfaceHandle<app::ServiceProvider> service_provider)
    : environment_services_(app::ServiceProviderPtr::Create(std::move(service_provider)))
{
}

ApplicationContext::~ApplicationContext() = default;

std::unique_ptr<ApplicationContext> ApplicationContext::CreateFromStartupInfo()
{
    auto startup_info = CreateFromStartupInfoNotChecked();
    FTL_CHECK(startup_info && startup_info->environment_services().get() != nullptr)
        << "Faux ApplicationContext has no service_provider";
    return startup_info;
}

std::unique_ptr<ApplicationContext> ApplicationContext::CreateFromStartupInfoNotChecked()
{
    // Get a channel handle from magma
    int fd = open("/dev/class/display/000", O_RDWR);
    if (fd < 0)
        return DRETP(nullptr, "couldn't open display device");

    mx_handle_t handle;
    int ret =
        mxio_ioctl(fd, IOCTL_MAGMA_GET_TRACE_MANAGER_CHANNEL, nullptr, 0, &handle, sizeof(handle));
    close(fd);

    if (ret < 0)
        return DRETP(nullptr, "IOCTL_MAGMA_GET_TRACE_MANAGER_CHANNEL failed %d\n", ret);

    return std::make_unique<ApplicationContext>(
        fidl::InterfaceHandle<app::ServiceProvider>(mx::channel(handle), 0u));
}

} // namespace faux
