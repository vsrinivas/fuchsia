// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/integration_tests/so_wrapper.h"

#include <dlfcn.h>
#include <link.h>
#include <stdlib.h>

#include <string>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace debug_agent {

// This callback will be called by dl_iterate_phdr for each module loaded into
// the current process. We use this to search for the module opened through
// dlopen.
//
// dl_iterate_phdr iterates over all the modules until one of them returns
// non-zero (signal to stop) or when there are no more modules left.
int SoWrapper::IteratePhdrCallback(struct ::dl_phdr_info* info, size_t size,
                                   void* user) {
  SoWrapper* so_wrapper = reinterpret_cast<SoWrapper*>(user);
  so_wrapper->module_offsets_[info->dlpi_name] = info->dlpi_addr;

  // Continue the iteration.
  return 0;
}

bool SoWrapper::Init(std::string so_name) {
  so_ = dlopen(so_name.data(), RTLD_GLOBAL);
  if (!so_)
    return false;
  so_name_ = std::move(so_name);

  // Load all the libraries
  dl_iterate_phdr(SoWrapper::IteratePhdrCallback, this);

  return true;
}

SoWrapper::~SoWrapper() {
  if (so_)
    dlclose(so_);
}

uint64_t SoWrapper::GetModuleStartAddress(const char* module_name) const {
  // We try to get the module we're asking for.
  auto module_it = module_offsets_.find(module_name);
  if (module_it == module_offsets_.end())
    return 0;
  return module_it->second;
}

uint64_t SoWrapper::GetSymbolAddress(const char* symbol_name) const {
  // We look for the symbol in our address space.
  void* symbol = dlsym(so_, symbol_name);
  return reinterpret_cast<uint64_t>(symbol);
}

}  // namespace debug_agent
