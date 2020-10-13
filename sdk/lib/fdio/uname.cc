// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <mutex>
#include <vector>

#include "internal.h"
#include "unistd.h"

static zx_status_t get_name_provider(llcpp::fuchsia::device::NameProvider::SyncClient** out) {
  static llcpp::fuchsia::device::NameProvider::SyncClient* saved;

  {
    static std::once_flag once;
    static zx_status_t status;
    std::call_once(once, [&]() {
      zx::channel out;
      status = fdio_service_connect_by_name(llcpp::fuchsia::device::NameProvider::Name, &out);
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

extern "C" __EXPORT int uname(utsname* uts) {
  if (!uts) {
    return ERRNO(EFAULT);
  }

  // Avoid overwriting caller's memory until after all fallible operations have succeeded.
  llcpp::fuchsia::device::NameProvider::SyncClient* name_provider;
  zx_status_t status = get_name_provider(&name_provider);
  if (status != ZX_OK) {
    return ERROR(status);
  }

  auto response = name_provider->GetDeviceName();
  if (response.status() != ZX_OK) {
    return ERROR(response.status());
  }

  auto result = std::move(response.Unwrap()->result);
  if (result.is_err()) {
    return ERROR(result.err());
  }

  const fidl::StringView& nodename = result.response().name;
  const auto size = std::min(nodename.size(), sizeof(uts->nodename) - 1);
  memcpy(uts->nodename, nodename.data(), size);
  uts->nodename[size] = '\0';

  strcpy(uts->sysname, "Fuchsia");
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

  return 0;
}
