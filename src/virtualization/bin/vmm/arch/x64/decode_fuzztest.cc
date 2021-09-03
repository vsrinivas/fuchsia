// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls/hypervisor.h>

#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "src/virtualization/bin/vmm/arch/x64/decode.h"

namespace {

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);
  std::vector<uint8_t> inst_buf =
      provider.ConsumeBytes<uint8_t>(provider.ConsumeIntegralInRange<uint32_t>(0, 32));
  uint8_t default_operand_size = provider.ConsumeBool() ? 2 : 4;
  zx_vcpu_state_t vcpu_state = {
      provider.ConsumeIntegral<uint64_t>(), provider.ConsumeIntegral<uint64_t>(),
      provider.ConsumeIntegral<uint64_t>(), provider.ConsumeIntegral<uint64_t>(),
      provider.ConsumeIntegral<uint64_t>(), provider.ConsumeIntegral<uint64_t>(),
      provider.ConsumeIntegral<uint64_t>(), provider.ConsumeIntegral<uint64_t>(),
      provider.ConsumeIntegral<uint64_t>(), provider.ConsumeIntegral<uint64_t>(),
      provider.ConsumeIntegral<uint64_t>(), provider.ConsumeIntegral<uint64_t>(),
      provider.ConsumeIntegral<uint64_t>(), provider.ConsumeIntegral<uint64_t>(),
      provider.ConsumeIntegral<uint64_t>(), provider.ConsumeIntegral<uint64_t>(),
      provider.ConsumeIntegral<uint64_t>(),
  };
  Instruction inst = {};
  inst_decode(inst_buf.data(), static_cast<uint32_t>(inst_buf.size()), default_operand_size,
              &vcpu_state, &inst);
  return 0;
}

}  // namespace
