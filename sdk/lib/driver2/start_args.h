// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER2_START_ARGS_H_
#define LIB_DRIVER2_START_ARGS_H_

#include <fidl/fuchsia.component.runner/cpp/fidl.h>
#include <fidl/fuchsia.data/cpp/fidl.h>
#include <fidl/fuchsia.driver.framework/cpp/fidl.h>
#include <lib/zx/result.h>

#include <vector>

namespace driver {

template <typename T>
zx::result<T> SymbolValue(const fuchsia_driver_framework::wire::DriverStartArgs& args,
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
zx::result<T> SymbolValue(
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
T GetSymbol(const std::optional<std::vector<fuchsia_driver_framework::NodeSymbol>>& symbols,
            std::string_view name, T default_value = nullptr) {
  auto value = driver::SymbolValue<T>(symbols, name);
  return value.is_ok() ? *value : default_value;
}

inline zx::result<std::string> ProgramValue(const fuchsia_data::wire::Dictionary& program,
                                            std::string_view key) {
  if (program.has_entries()) {
    for (auto& entry : program.entries()) {
      if (!std::equal(key.begin(), key.end(), entry.key.begin())) {
        continue;
      }
      if (!entry.value.has_value() || !entry.value->is_str()) {
        return zx::error(ZX_ERR_WRONG_TYPE);
      }
      auto& value = entry.value->str();
      return zx::ok(std::string{value.data(), value.size()});
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

inline zx::result<std::string> ProgramValue(const std::optional<fuchsia_data::Dictionary>& program,
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
inline zx::result<std::vector<std::string>> ProgramValueAsVector(
    const fuchsia_data::wire::Dictionary& program, std::string_view key) {
  if (program.has_entries()) {
    for (auto& entry : program.entries()) {
      if (!std::equal(key.begin(), key.end(), entry.key.begin())) {
        continue;
      }
      if (!entry.value.has_value() || !entry.value->is_str_vec()) {
        return zx::error(ZX_ERR_WRONG_TYPE);
      }
      auto& values = entry.value->str_vec();
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

// Returns the list of values for |key| as a vector of strings.
inline zx::result<std::vector<std::string>> ProgramValueAsVector(
    const fuchsia_data::Dictionary& program, std::string_view key) {
  auto program_entries = program.entries();
  if (program_entries.has_value()) {
    for (auto& entry : program_entries.value()) {
      auto& entry_key = entry.key();
      auto& entry_value = entry.value();

      if (key != entry_key) {
        continue;
      }

      if (entry_value->Which() != fuchsia_data::DictionaryValue::Tag::kStrVec) {
        return zx::error(ZX_ERR_WRONG_TYPE);
      }

      return zx::ok(entry_value->str_vec().value());
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

inline zx::result<fidl::UnownedClientEnd<fuchsia_io::Directory>> NsValue(
    const fidl::VectorView<fuchsia_component_runner::wire::ComponentNamespaceEntry>& entries,
    std::string_view path) {
  for (auto& entry : entries) {
    if (std::equal(path.begin(), path.end(), entry.path().begin())) {
      return zx::ok<fidl::UnownedClientEnd<fuchsia_io::Directory>>(entry.directory());
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

inline zx::result<fidl::UnownedClientEnd<fuchsia_io::Directory>> NsValue(
    const std::vector<fuchsia_component_runner::ComponentNamespaceEntry>& entries,
    std::string_view path) {
  for (auto& entry : entries) {
    auto entry_path = entry.path();
    ZX_ASSERT_MSG(entry_path.has_value(), "The entry's path cannot be empty.");
    if (path == entry_path.value()) {
      auto& entry_directory = entry.directory();
      ZX_ASSERT_MSG(entry_directory.has_value(), "The entry's directory cannot be empty.");
      return zx::ok<fidl::UnownedClientEnd<fuchsia_io::Directory>>(entry_directory.value());
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

}  // namespace driver

#endif  // LIB_DRIVER2_START_ARGS_H_
