// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/impl/vk/pipeline_spec.h"

namespace escher {
namespace impl {

PipelineSpec::PipelineSpec() : type_(0), hash_(0) {}

PipelineSpec::PipelineSpec(size_t type, std::vector<uint8_t> data) : type_(type), data_(data) {
  hash_ = type_;
  for (uint8_t i : data) {
    // TODO: use better hash function
    hash_ = (hash_ + 13 * i) * 7;
  }
}

}  // namespace impl
}  // namespace escher
