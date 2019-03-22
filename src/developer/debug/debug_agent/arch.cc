// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "src/developer/debug/debug_agent/arch.h"

namespace debug_agent {
namespace arch {

namespace {

std::unique_ptr<ArchProvider> arch_provider = nullptr;

}  // namespace

ArchProvider& ArchProvider::Get() {
  if (!arch_provider) {
    arch_provider = std::make_unique<ArchProvider>();
  }
  return *arch_provider.get();
}

void ArchProvider::Set(std::unique_ptr<ArchProvider> arch) {
  arch_provider = std::move(arch);
}

ArchProvider::~ArchProvider() = default;

}  // namespace arch
}  // namespace debug_agent
