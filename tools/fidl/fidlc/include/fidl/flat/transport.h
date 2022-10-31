// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TRANSPORT_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TRANSPORT_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fidl::flat {

// The class / namespace of the handle, used for compatibility checking with
// transports.
enum class HandleClass {
  kZircon,  // zx.handle
  kDriver,  // fdf.handle
  kBanjo,   // only referenced by client_end / server_end
};

std::string_view HandleClassName(HandleClass handle_class);
std::optional<HandleClass> HandleClassFromName(std::string_view name);

struct Transport {
  enum class Kind {
    kZirconChannel,  // @transport("Channel")
    kDriverChannel,  // @transport("Driver")
    kBanjo,          // @transport("Banjo")
    kSyscall,        // @transport("Syscall")
  };

  // e.g. kZirconChannel.
  Kind kind;
  // e.g. "Channel".
  std::string_view name;
  // The class of handle used to represent client and server endpoints of this transport
  // (e.g. zx.handle for @transport("Channel")).
  std::optional<HandleClass> handle_class;
  // The classes of handles that can be used in this transport.
  std::set<HandleClass> compatible_handle_classes;

  bool IsCompatible(HandleClass) const;
  static std::optional<Transport> FromTransportName(std::string_view transport_name);
  static std::set<std::string_view> AllTransportNames();

 private:
  static std::vector<Transport> transports;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TRANSPORT_H_
