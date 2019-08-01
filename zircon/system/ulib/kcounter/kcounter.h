// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <lib/counter-vmo-abi.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/time.h>

#include <map>
#include <vector>

#include <fbl/macros.h>
#include <fbl/string.h>
#include <fbl/string_piece.h>

namespace kcounter {

class VmoToInspectMapper final {
 public:
  VmoToInspectMapper();
  ~VmoToInspectMapper() = default;

  zx_status_t GetInspectVMO(zx::vmo* vmo);
  zx_status_t UpdateInspectVMO();

 private:
  bool ShouldInclude(const counters::Descriptor& entry);
  void BuildCounterToMetricVMOMapping();

  zx_status_t initialization_status_ = 1;

  fzl::OwnedVmoMapper desc_mapper_;
  const counters::DescriptorVmo* desc_ = nullptr;

  fzl::OwnedVmoMapper arena_mapper_;
  const volatile int64_t* arena_ = nullptr;

  zx::time last_update_ = zx::time::infinite_past();

  inspect::Inspector inspector_;
  std::map<fbl::String, inspect::Node> intermediate_nodes_;
  std::vector<inspect::IntProperty> metric_by_index_;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(VmoToInspectMapper);
};

}  // namespace kcounter
