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

// Stores a DriverStartArgs table, in order to pass it from a driver host to a
// driver in a language-agnostic way.
struct Storage {
  FIDL_ALIGNDECL
  uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[llcpp::fuchsia::driver::framework::DriverStartArgs::MaxNumHandles];
};

// Encodes |start_args| into |storage|.
zx::status<fidl_msg_t> Encode(Storage* storage,
                              llcpp::fuchsia::driver::framework::DriverStartArgs start_args,
                              const char** error);

// Decodes |msg| and return a DriverStartArgs.
zx::status<llcpp::fuchsia::driver::framework::DriverStartArgs*> Decode(fidl_msg_t* msg,
                                                                       const char** error);

template <typename T>
zx::status<T> symbol_value(
    const fidl::VectorView<llcpp::fuchsia::driver::framework::DriverSymbol>& symbols,
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

inline zx::status<std::string> program_value(const llcpp::fuchsia::data::Dictionary& program,
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

inline zx::status<zx::unowned_channel> ns_value(
    const fidl::VectorView<llcpp::fuchsia::component::runner::ComponentNamespaceEntry>& entries,
    std::string_view path) {
  for (auto& entry : entries) {
    if (std::equal(path.begin(), path.end(), entry.path().begin())) {
      return zx::ok(entry.directory());
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

}  // namespace start_args

#endif  // SRC_DEVICES_LIB_DRIVER2_START_ARGS_H_
