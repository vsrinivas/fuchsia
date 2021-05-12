// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_START_ARGS_H_
#define SRC_DEVICES_LIB_DRIVER2_START_ARGS_H_

#include <fuchsia/component/runner/llcpp/fidl.h>
#include <fuchsia/data/llcpp/fidl.h>
#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <lib/zx/status.h>

namespace start_args {

template <typename T>
zx::status<T> SymbolValue(
    const fidl::VectorView<fuchsia_driver_framework::wire::NodeSymbol>& symbols,
    std::string_view path) {
  static_assert(sizeof(T) == sizeof(zx_vaddr_t), "T must match zx_vaddr_t in size");
  for (auto& symbol : symbols) {
    if (std::equal(path.begin(), path.end(), symbol.name().begin())) {
      T value;
      memcpy(&value, &symbol.address(), sizeof(zx_vaddr_t));
      return zx::ok(value);
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

inline zx::status<std::string> ProgramValue(const fuchsia_data::wire::Dictionary& program,
                                            std::string_view key) {
  if (program.has_entries()) {
    for (auto& entry : program.entries()) {
      if (!std::equal(key.begin(), key.end(), entry.key.begin())) {
        continue;
      }
      if (!entry.value.is_str()) {
        return zx::error(ZX_ERR_WRONG_TYPE);
      }
      auto& value = entry.value.str();
      return zx::ok(std::string{value.data(), value.size()});
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

inline zx::status<fidl::UnownedClientEnd<fuchsia_io::Directory>> NsValue(
    const fidl::VectorView<fuchsia_component_runner::wire::ComponentNamespaceEntry>& entries,
    std::string_view path) {
  for (auto& entry : entries) {
    if (std::equal(path.begin(), path.end(), entry.path().begin())) {
      return zx::ok<fidl::UnownedClientEnd<fuchsia_io::Directory>>(entry.directory());
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

}  // namespace start_args

#endif  // SRC_DEVICES_LIB_DRIVER2_START_ARGS_H_
