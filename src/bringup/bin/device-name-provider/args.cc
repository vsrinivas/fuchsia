// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "args.h"

#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <stdlib.h>

#include <cstring>

namespace {
int ParseCommonArgs(int argc, char** argv, const char** error, std::string* interface) {
  while (argc > 1) {
    if (!strncmp(argv[1], "--interface", 11)) {
      if (argc < 3) {
        *error = "netsvc: missing argument to --interface";
        return -1;
      }
      *interface = argv[2];
      argv++;
      argc--;
    }
    argv++;
    argc--;
  }
  return 0;
}
}  // namespace

int ParseArgs(int argc, char** argv, const zx::channel& svc_root, const char** error,
              DeviceNameProviderArgs* out) {
  // Reset the args.
  *out = DeviceNameProviderArgs();

  // First parse from kernel args, then use use cmdline args as overrides.
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    *error = "netsvc: unable to create channel";
    return -1;
  }

  status = fdio_service_connect_at(svc_root.get(), llcpp::fuchsia::boot::Arguments::Name,
                                   remote.release());
  if (status != ZX_OK) {
    *error = "netsvc: unable to connect to fuchsia.boot.Arguments";
    return -1;
  }

  llcpp::fuchsia::boot::Arguments::SyncClient client(std::move(local));
  fidl::StringView string_keys[]{
      fidl::StringView{"netsvc.interface"},
      fidl::StringView{"zircon.nodename"},
  };
  auto string_resp = client.GetStrings(fidl::unowned_vec(string_keys));
  if (string_resp.ok()) {
    auto& values = string_resp->values;
    out->interface = std::string{values[0].data(), values[0].size()};
    out->nodename = std::string{values[1].data(), values[1].size()};
  }

  int err = ParseCommonArgs(argc, argv, error, &out->interface);
  if (err) {
    return err;
  }

  out->ethdir = std::string("/dev/class/ethernet");

  while (argc > 1) {
    if (!strncmp(argv[1], "--nodename", 10)) {
      if (argc < 3) {
        *error = "netsvc: missing argument to --nodename";
        return -1;
      }
      out->nodename = argv[2];
      argv++;
      argc--;
    }
    if (!strncmp(argv[1], "--ethdir", 12)) {
      if (argc < 3) {
        *error = "netsvc: missing argument to --ethdir";
        return -1;
      }
      out->ethdir = argv[2];
      argv++;
      argc--;
    }
    argv++;
    argc--;
  }
  return 0;
}
