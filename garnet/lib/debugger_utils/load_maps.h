// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <zircon/types.h>

#include "lib/fxl/macros.h"

namespace debugger_utils {

struct LoadMap {
  zx_koid_t pid;
  uint64_t base_addr;
  uint64_t load_addr;
  uint64_t end_addr;
  std::string name;
  std::string so_name;
  std::string build_id;
};

class LoadMapTable {
 public:
  LoadMapTable() = default;

  bool ReadLogListenerOutput(const std::string& file);

  const LoadMap* LookupLoadMap(zx_koid_t pid, uint64_t addr);

 private:
  void Clear();
  void AddLoadMap(const LoadMap& map);

  std::vector<LoadMap> maps_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LoadMapTable);
};

}  // namespace debugger_utils
