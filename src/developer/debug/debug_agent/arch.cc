// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch.h"

#include <memory>

namespace debug_agent {
namespace arch {

ArchProvider::ArchProvider() = default;
ArchProvider::~ArchProvider() = default;

zx_status_t ArchProvider::GetInfo(const zx::thread& thread, zx_object_info_topic_t topic,
                                  void* buffer, size_t buffer_size, size_t* actual, size_t* avail) {
  return thread.get_info(topic, buffer, buffer_size, actual, avail);
}

}  // namespace arch
}  // namespace debug_agent
