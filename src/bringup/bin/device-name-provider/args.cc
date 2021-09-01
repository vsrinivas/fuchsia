// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "args.h"

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/service/llcpp/service.h>
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

uint32_t NamegenParse(const std::string& str) {
  if (str == "0") {
    return 0;
  }
  return 1;
}

}  // namespace

int ParseArgs(int argc, char** argv, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
              const char** error, DeviceNameProviderArgs* out) {
  // Reset the args.
  *out = DeviceNameProviderArgs();

  // First parse from kernel args, then use use cmdline args as overrides.
  zx::status client_end = service::ConnectAt<fuchsia_boot::Arguments>(svc_root);
  if (client_end.is_error()) {
    *error = "netsvc: unable to connect to fuchsia.boot.Arguments";
    return -1;
  }

  fidl::StringView string_keys[]{
      fidl::StringView{"netsvc.interface"},
      fidl::StringView{"zircon.nodename"},
      fidl::StringView{"zircon.namegen"},
  };
  std::string namegen_str("");
  auto string_resp = fidl::WireCall(client_end.value())
                         .GetStrings(fidl::VectorView<fidl::StringView>::FromExternal(string_keys));
  if (string_resp.ok()) {
    auto& values = string_resp->values;
    out->interface = std::string{values[0].data(), values[0].size()};
    out->nodename = std::string{values[1].data(), values[1].size()};
    namegen_str = std::string{values[2].data(), values[2].size()};
  }

  out->namegen = NamegenParse(namegen_str);

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
    if (!strncmp(argv[1], "--namegen", 10)) {
      if (argc < 3) {
        *error = "netsvc: missing argument to --namegen";
        return -1;
      }
      out->namegen = NamegenParse(std::string{argv[2]});
      argv++;
      argc--;
    }
    argv++;
    argc--;
  }
  return 0;
}
