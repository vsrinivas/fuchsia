// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "unwindstack/Maps.h"

#include "src/lib/fxl/logging.h"

namespace unwindstack {

Maps::~Maps() {
  for (auto& map : maps_) {
    delete map;
  }
}

MapInfo* Maps::Find(uint64_t pc) {
  if (maps_.empty()) {
    return nullptr;
  }
  size_t first = 0;
  size_t last = maps_.size();
  while (first < last) {
    size_t index = (first + last) / 2;
    MapInfo* cur = maps_[index];
    if (pc >= cur->start && pc < cur->end) {
      return cur;
    } else if (pc < cur->start) {
      last = index;
    } else {
      first = index + 1;
    }
  }
  return nullptr;
}

bool Maps::Parse() {
  // Unimplemented. The Fuchsia port requires the embedding code to manually
  // populate this with the libraries loaded by the process via Add().
  return false;
}

void Maps::Add(uint64_t start, uint64_t end, uint64_t offset, uint64_t flags,
               const std::string& name, uint64_t load_bias) {
  MapInfo* map_info = new MapInfo(this, start, end, offset, flags, name);
  map_info->load_bias = load_bias;
  maps_.push_back(map_info);
}

}  // namespace unwindstack
