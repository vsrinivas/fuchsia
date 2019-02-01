// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

struct dl_phdr_info;

namespace debug_agent {

// SoWrapper functionalities are two-fold:
// - Manages a given .so as a resource and exposes a way to expose the addresses
//   of symbols within that .so.
// - Queries all the modules loaded within the current process. The main purpose
//   of this is to find the same module that was loaded by Init and be able to
//   start address.
//
// With that the offset is calculable and we can know how far inside a
// particular module a symbol is. That can then be used to place breakpoints or
// other address specific tools.
class SoWrapper {
 public:
  // Fails if |so_name| doesn't point to a valid .so.
  bool Init(std::string so_name);
  ~SoWrapper();

  // GetSymbolAddress - GetModuleStartAddress.
  uint64_t GetSymbolOffset(const char* module, const char* symbol) const;

  // Gets the start address of a module loaded in the current process.
  // Returns 0 if not found.
  uint64_t GetModuleStartAddress(const char* module_name) const;

  // Looks for the address where a particular symbol from the loaded .so is
  // loaded in the current address space.
  // Returns 0 if not found.
  uint64_t GetSymbolAddress(const char* symbol_name) const;

  const std::string& so_name() const { return so_name_; }

 private:
  // Callback to be used by dl_iterate_phdr to find the module offsets.
  // This callback is called for each module loaded into the current address
  // space. This will log each module name and address start into an instance
  // of SoWrapper given in through |user|.
  static int IteratePhdrCallback(struct ::dl_phdr_info*, size_t, void* user);

  std::string so_name_;
  void* so_;
  std::map<std::string, uint64_t> module_offsets_;
};

}  // namespace debug_agent
