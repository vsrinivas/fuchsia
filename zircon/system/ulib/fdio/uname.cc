// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mutex>

#include <errno.h>
#include <string.h>
#include <vector>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <threads.h>
#include <unistd.h>
#include "unistd.h"

#include <fuchsia/device/llcpp/fidl.h>

#include <zircon/types.h>
#include <zircon/status.h>

#include "private.h"

static zx_status_t get_name_provider(llcpp::fuchsia::device::NameProvider::SyncClient** out) {
    static llcpp::fuchsia::device::NameProvider::SyncClient* saved;

    {
        static std::once_flag once;
        static zx_status_t status;
        std::call_once(once, [&]() {
            zx::channel out;
            status = fdio_service_connect_by_name(llcpp::fuchsia::device::NameProvider::Name_, &out);
            if (status != ZX_OK) {
                return;
            }
            static llcpp::fuchsia::device::NameProvider::SyncClient client(std::move(out));
            saved = &client;
        });
        if (status != ZX_OK) {
            return status;
        }
    }

    *out = saved;
    return ZX_OK;
}

extern "C"
__EXPORT
int uname(utsname* uts) {
    if (!uts) {
        errno = EFAULT;
        return -1;
    }
    strcpy(uts->sysname, "Fuchsia");
    strcpy(uts->nodename, "");
    strcpy(uts->release, "");
    strcpy(uts->version, "");
#if defined(__x86_64__)
    strcpy(uts->machine, "x86_64");
#elif defined(__aarch64__)
    strcpy(uts->machine, "aarch64");
#else
    strcpy(uts->machine, "");
#endif

#ifdef _GNU_SOURCE
    strcpy(uts->domainname, "");
#else
    strcpy(uts->__domainname, "");
#endif

    llcpp::fuchsia::device::NameProvider::SyncClient* name_provider;
    zx_status_t status = get_name_provider(&name_provider);
    if (status != ZX_OK) {
        printf("get_name_provider error %s\n", zx_status_get_string(status));
        return ERROR(status);
    }

    // TODO: can we use response_buffer.size() (unsigned long) as an argument to BytePart (uint32_t)?
    uint32_t bufsize(128);
    std::vector<uint8_t> response_buffer(bufsize);
    auto response = name_provider->GetDeviceName(fidl::BytePart(&response_buffer[0], bufsize));
    if (response.status() == ZX_ERR_BAD_HANDLE) {
      printf("name_provider->GetDeviceName(_) = %s\n", zx_status_get_string(status));
      // The component calling uname probably doens't have fuchsia.device.NameProvider in its sandbox.
      strlcpy(uts->nodename, llcpp::fuchsia::device::DEFAULT_DEVICE_NAME, HOST_NAME_MAX);
      return ZX_OK;
    }
    if (response.status() != ZX_OK) {
        printf("name_provider->GetDeviceName(_).status = %s\n", zx_status_get_string(response.status()));
        return ERROR(response.status());
    }

    auto result = std::move(response.Unwrap()->result);
    if (result.is_err()) {
        printf("name_provider->GetDeviceName(_).Unwrap()->result = %s\n", zx_status_get_string(result.err()));
        return ERROR(result.err());
    }

    auto nodename = result.response().name;
    strlcpy(uts->nodename, nodename.data(), nodename.size());
    return ZX_OK;
}
