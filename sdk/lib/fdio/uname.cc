// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <string.h>
#include <sys/utsname.h>
#include <zircon/types.h>

#include <vector>

#include "fdio_unistd.h"
#include "internal.h"

extern "C" __EXPORT int uname(utsname* uts) {
  if (!uts) {
    return ERRNO(EFAULT);
  }

  // Avoid overwriting caller's memory until after all fallible operations have succeeded.
  auto& name_provider = get_client<fuchsia_device::NameProvider>();
  if (name_provider.is_error()) {
    return ERROR(name_provider.status_value());
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
