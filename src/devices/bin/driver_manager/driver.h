// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_H_

#include <lib/fit/function.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <ddk/binding.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>

struct Driver {
  Driver() = default;

  fbl::String name;
  std::unique_ptr<const zx_bind_inst_t[]> binding;
  // Binding size in number of bytes, not number of entries
  // TODO: Change it to number of entries
  uint32_t binding_size = 0;
  uint32_t flags = 0;
  zx::vmo dso_vmo;

  fbl::DoublyLinkedListNodeState<Driver*> node;
  struct Node {
    static fbl::DoublyLinkedListNodeState<Driver*>& node_state(Driver& obj) { return obj.node; }
  };

  fbl::String libname;

  // If true, this driver never tries to match against new devices.
  bool never_autoselect = false;
};

#define DRIVER_NAME_LEN_MAX 64

using DriverLoadCallback = fit::function<void(Driver* driver, const char* version)>;

void load_driver(const char* path, DriverLoadCallback func);
void find_loadable_drivers(const char* path, DriverLoadCallback func);

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_H_
