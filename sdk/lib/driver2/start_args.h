// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER2_START_ARGS_H_
#define LIB_DRIVER2_START_ARGS_H_

#include <fidl/fuchsia.component.runner/cpp/wire.h>
#include <fidl/fuchsia.data/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/zx/status.h>

#include <vector>

namespace driver {

template <typename T>
zx::status<T> SymbolValue(const fuchsia_driver_framework::wire::DriverStartArgs& args,
                          std::string_view name) {
  if (!args.has_symbols()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  const fidl::VectorView<fuchsia_driver_framework::wire::NodeSymbol>& symbols = args.symbols();
  static_assert(sizeof(T) == sizeof(zx_vaddr_t), "T must match zx_vaddr_t in size");
  for (auto& symbol : symbols) {
    if (std::equal(name.begin(), name.end(), symbol.name().begin())) {
      T value;
      memcpy(&value, &symbol.address(), sizeof(zx_vaddr_t));
      return zx::ok(value);
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

template <typename T>
zx::status<T> SymbolValue(
    const std::optional<std::vector<fuchsia_driver_framework::NodeSymbol>>& symbols,
    std::string_view name) {
  if (!symbols.has_value()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  static_assert(sizeof(T) == sizeof(zx_vaddr_t), "T must match zx_vaddr_t in size");
  for (auto& symbol : *symbols) {
    if (name == symbol.name().value()) {
      T value;
      memcpy(&value, &symbol.address().value(), sizeof(zx_vaddr_t));
      return zx::ok(value);
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

template <typename T>
T GetSymbol(const fuchsia_driver_framework::wire::DriverStartArgs& args, std::string_view name,
            T default_value = nullptr) {
  auto value = driver::SymbolValue<T>(args, name);
  return value.is_ok() ? *value : default_value;
}

template <typename T>
T GetSymbol(const std::optional<std::vector<fuchsia_driver_framework::NodeSymbol>>& symbols,
            std::string_view name, T default_value = nullptr) {
  auto value = driver::SymbolValue<T>(symbols, name);
  return value.is_ok() ? *value : default_value;
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

inline zx::status<std::string> ProgramValue(const std::optional<fuchsia_data::Dictionary>& program,
                                            std::string_view key) {
  if (program.has_value() && program->entries().has_value()) {
    for (const auto& entry : *program->entries()) {
      if (key != entry.key()) {
        continue;
      }
      auto value = entry.value()->str();
      if (!value.has_value()) {
        return zx::error(ZX_ERR_WRONG_TYPE);
      }
      return zx::ok(value.value());
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

// Returns the list of values for |key| as a vector of strings.
inline zx::status<std::vector<std::string>> ProgramValueAsVector(
    const fuchsia_data::wire::Dictionary& program, std::string_view key) {
  if (program.has_entries()) {
    for (auto& entry : program.entries()) {
      if (!std::equal(key.begin(), key.end(), entry.key.begin())) {
        continue;
      }
      if (!entry.value.is_str_vec()) {
        return zx::error(ZX_ERR_WRONG_TYPE);
      }
      auto& values = entry.value.str_vec();
      std::vector<std::string> result;
      result.reserve(values.count());
      for (auto& value : values) {
        result.emplace_back(std::string{value.data(), value.size()});
      }
      return zx::ok(result);
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

}  // namespace driver

#endif  // LIB_DRIVER2_START_ARGS_H_
