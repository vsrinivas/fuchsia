// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "read.h"

#include <fuchsia/inspect/deprecated/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>

#include <src/lib/files/path.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/substitute.h"

namespace inspect_deprecated {
namespace {
fit::result<fidl::InterfaceHandle<fuchsia::inspect::deprecated::Inspect>, std::string>
OpenInspectAtPath(const std::string& path) {
  fuchsia::inspect::deprecated::InspectPtr inspect;
  auto endpoint = files::AbsolutePath(path);
  zx_status_t status =
      fdio_service_connect(endpoint.c_str(), inspect.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return fit::error(
        fxl::StringPrintf("Failed to connect at %s with %d\n", endpoint.c_str(), status));
  }

  return fit::ok(inspect.Unbind());
}
}  // namespace

fit::promise<Source, std::string> ReadLocation(Location location, int depth) {
  if (location.type == Location::Type::INSPECT_FIDL) {
    auto handle = OpenInspectAtPath(location.AbsoluteFilePath());

    if (!handle.is_ok()) {
      return fit::make_promise(
          [path = location.AbsoluteFilePath()]() -> fit::result<Source, std::string> {
            return fit::error(fxl::StringPrintf("Failed to open %s\n", path.c_str()));
          });
    }
    return Source::MakeFromFidl(std::move(location),
                                inspect_deprecated::ObjectReader(handle.take_value()), depth);
  } else if (location.type == Location::Type::INSPECT_FILE_FORMAT) {
    fuchsia::io::FilePtr file_ptr;
    zx_status_t status =
        fdio_open(location.AbsoluteFilePath().c_str(), fuchsia::io::OPEN_RIGHT_READABLE,
                  file_ptr.NewRequest().TakeChannel().release());
    if (status != ZX_OK || !file_ptr.is_bound()) {
      return fit::make_promise(
          [path = location.AbsoluteFilePath(), status]() -> fit::result<Source, std::string> {
            return fit::error(
                fxl::StringPrintf("Failed to fdio_open and bind %s %d\n", path.c_str(), status));
          });
    }
    return Source::MakeFromVmo(std::move(location), std::move(file_ptr), depth);
  } else {
    return fit::make_promise([type = location.type]() -> fit::result<Source, std::string> {
      return fit::error(fxl::StringPrintf("Unknown location type %d\n", type));
    });
  }
}

}  // namespace inspect_deprecated
