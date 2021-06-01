// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/unwind.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <set>
#include <unordered_map>

#include "src/developer/debug/unwinder/dwarf_cfi.h"
#include "src/developer/debug/unwinder/error.h"
#include "src/developer/debug/unwinder/registers.h"

namespace unwinder {

namespace {

class CFIUnwinder {
 public:
  CFIUnwinder(Memory* memory, const std::vector<uint64_t>& modules)
      : memory_(memory), module_addresses_(modules.begin(), modules.end()) {}

  Error Step(Registers current, Registers& next, bool is_return_address) {
    uint64_t pc;
    if (auto err = current.GetPC(pc); err.has_err()) {
      return err;
    }

    // is_return_address indicates whether pc in the current registers is a return address.
    // If it is, we need to subtract 1 to find the call site because "call" could be the last
    // instruction of a nonreturn function.
    if (is_return_address) {
      pc -= 1;
      current.SetPC(pc);
    }

    auto module_address_it = module_addresses_.upper_bound(pc);
    if (module_address_it == module_addresses_.begin()) {
      return Error("%#" PRIx64 " is not covered by any module", pc);
    }
    uint64_t module_address = *(--module_address_it);

    auto module_map_it = module_map_.find(module_address);
    if (module_map_it == module_map_.end()) {
      module_map_it = module_map_.emplace(module_address, memory_).first;
      if (auto err = module_map_it->second.Load(module_address); err.has_err()) {
        return err;
      }
    }
    if (auto err = module_map_it->second.Step(current, next); err.has_err()) {
      return err;
    }
    return Success();
  }

 private:
  Memory* memory_;
  std::set<uint64_t> module_addresses_;
  std::unordered_map<uint64_t, DwarfCfi> module_map_;
};

}  // namespace

std::vector<Frame> Unwind(Memory* memory, const std::vector<uint64_t>& modules,
                          const Registers& registers, size_t max_depth) {
  std::vector<Frame> res = {{registers, Frame::Trust::kContext, Success()}};
  CFIUnwinder cfi_unwinder(memory, modules);

  while (max_depth--) {
    Registers next(registers.arch());

    Frame& current = res.back();
    current.error = cfi_unwinder.Step(current.regs, next, current.trust != Frame::Trust::kContext);

    if (current.error.has_err()) {
      // TODO(74320): add more unwinders
      break;
    }
    res.emplace_back(std::move(next), Frame::Trust::kCFI, Success());

    // An undefined PC (e.g. on Linux) or 0 PC (e.g. on Fuchsia) marks the end of the unwinding.
    if (uint64_t pc; res.back().regs.GetPC(pc).has_err() || pc == 0) {
      break;
    }
  }

  return res;
}

}  // namespace unwinder
