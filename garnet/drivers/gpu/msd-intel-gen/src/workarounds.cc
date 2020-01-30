// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "workarounds.h"

#include "instructions.h"
#include "registers.h"

namespace {
constexpr uint32_t kRegisterWriteCount = 1;
}

uint64_t Workarounds::InstructionBytesRequired() {
  const uint32_t num_dwords =
      MiLoadDataImmediate::dword_count(kRegisterWriteCount) + MiNoop::kDwordCount;
  return num_dwords * sizeof(uint32_t);
}

bool Workarounds::Init(magma::InstructionWriter* writer, EngineCommandStreamerId engine_id) {
  if (engine_id != RENDER_COMMAND_STREAMER)
    return DRETF(false, "Only render engine is supported");

  std::vector<uint32_t> offsets;
  std::vector<uint32_t> values;

  // Resolves a GPU hang seen in Vulkan conformance
  //(dEQP-VK.renderpass.suballocation.multisample.d24_unorm_s8_uint.samples_2)
  offsets.push_back(registers::CacheMode1::kOffset);
  uint32_t value = registers::CacheMode1::k4x4StcOptimizationDisable |
                   registers::CacheMode1::kPartialResolveInVcDisable;
  values.push_back((value << 16) | value);

  DASSERT(offsets.size() == kRegisterWriteCount);
  DASSERT(values.size() == offsets.size());

  MiLoadDataImmediate::write(writer, offsets.size(), offsets.data(), values.data());

  MiNoop::write(writer);

  return true;
}
