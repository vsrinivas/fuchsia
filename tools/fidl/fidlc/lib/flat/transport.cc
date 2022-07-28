// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/flat/transport.h"

namespace fidl::flat {

std::string_view HandleClassName(HandleClass handle_class) {
  switch (handle_class) {
    case HandleClass::kZircon:
      return "zx.handle";
    case HandleClass::kDriver:
      return "fdf.handle";
    case HandleClass::kBanjo:
      return "[banjo]";
  };
}

std::optional<HandleClass> HandleClassFromName(std::string_view name) {
  if (name == "zx.handle") {
    return HandleClass::kZircon;
  }
  if (name == "fdf.handle") {
    return HandleClass::kDriver;
  }
  return std::nullopt;
}

bool Transport::IsCompatible(HandleClass handle_class) const {
  return compatible_handle_classes.find(handle_class) != compatible_handle_classes.end();
}

std::optional<Transport> Transport::FromTransportName(std::string_view transport_name) {
  for (const Transport& transport : transports) {
    if (transport.name == transport_name) {
      return transport;
    }
  }
  return std::nullopt;
}

std::set<std::string_view> Transport::AllTransportNames() {
  std::set<std::string_view> names;
  for (const auto& entry : transports) {
    names.insert(entry.name);
  }
  return names;
}

std::vector<Transport> Transport::transports = {
    Transport{
        .kind = Kind::kZirconChannel,
        .name = "Channel",
        .handle_class = HandleClass::kZircon,
        .compatible_handle_classes = {HandleClass::kZircon},
    },
    Transport{
        .kind = Kind::kDriverChannel,
        .name = "Driver",
        .handle_class = HandleClass::kDriver,
        .compatible_handle_classes = {HandleClass::kZircon, HandleClass::kDriver},
    },
    Transport{
        .kind = Kind::kBanjo,
        .name = "Banjo",
        .handle_class = HandleClass::kBanjo,
        .compatible_handle_classes = {HandleClass::kZircon, HandleClass::kBanjo},
    },
    Transport{
        .kind = Kind::kSyscall,
        .name = "Syscall",
        .compatible_handle_classes = {HandleClass::kZircon},
    },
};

}  // namespace fidl::flat
