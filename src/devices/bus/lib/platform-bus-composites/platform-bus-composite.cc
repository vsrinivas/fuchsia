// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

#include <lib/ddk/binding_priv.h>

namespace platform_bus_composite {

fidl::VectorView<fuchsia_device_manager::wire::DeviceFragment> MakeFidlFragment(
    fidl::AnyArena& arena, const device_fragment_t* fragments, size_t fragment_count) {
  fidl::VectorView<fuchsia_device_manager::wire::DeviceFragment> view;
  view.Allocate(arena, fragment_count);

  for (size_t i = 0; i < fragment_count; i++) {
    auto& dest = view[i];
    auto& src = fragments[i];
    dest.name = fidl::StringView(arena, src.name);
    dest.parts.Allocate(arena, src.parts_count);

    for (size_t j = 0; j < src.parts_count; j++) {
      auto& src_part = src.parts[j];
      auto& dst_part = dest.parts[j];

      dst_part.match_program.Allocate(arena, src_part.instruction_count);
      for (size_t k = 0; k < src_part.instruction_count; k++) {
        dst_part.match_program[k].arg = src_part.match_program[k].arg;
        dst_part.match_program[k].debug = src_part.match_program[k].debug;
        dst_part.match_program[k].op = src_part.match_program[k].op;
      }
    }
  }

  return view;
}

}  // namespace platform_bus_composite
