// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "boot-args.h"

#include <lib/zx/vmar.h>

namespace devmgr {

zx_status_t BootArgs::Create(zx::vmo vmo, size_t size, BootArgs* out) {
  // If we have no valid data in the VMO, return early success.
  if (size == 0) {
    return ZX_OK;
  }

  uintptr_t addr;
  zx_status_t status = zx::vmar::root_self()->map(0, vmo, 0, size, ZX_VM_PERM_READ, &addr);
  if (status != ZX_OK) {
    return status;
  }

  // Build boot arguments map for fast lookup.
  std::string_view view(reinterpret_cast<char*>(addr), size);
  ArgsMap args;
  for (size_t begin = 0, end; (end = view.find_first_of('\0', begin)) != std::string_view::npos;
       begin = end + 1) {
    size_t sep = view.find_first_of('=', begin);
    std::string_view key;
    const char* value;
    if (sep < end) {
      // Handle arguments of the form "key=value".
      key = std::string_view(&view[begin], sep - begin);
      value = &view[sep + 1];
    } else {
      // Handle arguments of the form "key".
      key = std::string_view(&view[begin], end - begin);
      value = &view[end];
    }
    args.insert_or_assign(key, value);
  }

  out->addr_ = addr;
  out->size_ = size;
  out->args_ = std::move(args);
  return ZX_OK;
}

BootArgs::~BootArgs() {
  if (addr_ != 0) {
    zx::vmar::root_self()->unmap(addr_, size_);
  }
}

const char* BootArgs::Get(std::string_view name) const {
  auto it = args_.find(name);
  if (it == args_.end()) {
    return nullptr;
  }
  return it->second;
}

bool BootArgs::GetBool(std::string_view name, bool default_value) const {
  const char* value = Get(name);
  if (value == nullptr) {
    return default_value;
  } else if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 || strcmp(value, "off") == 0) {
    return false;
  } else {
    return true;
  }
}

void BootArgs::Collect(std::string_view prefix, fbl::Vector<const char*>* out) const {
  std::string_view view(reinterpret_cast<char*>(addr_), size_);
  for (size_t pos = 0; (pos = view.find(prefix, pos)) != std::string_view::npos;
       pos = view.find_first_of('\0', pos)) {
    out->push_back(&view[pos]);
  }
}

}  // namespace devmgr
