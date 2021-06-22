// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/unwind.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <set>
#include <unordered_map>
#include <utility>

#include "src/developer/debug/unwinder/dwarf_cfi.h"
#include "src/developer/debug/unwinder/error.h"
#include "src/developer/debug/unwinder/registers.h"

namespace unwinder {

namespace {

class CFIUnwinder {
 public:
  CFIUnwinder(Memory* stack, std::map<uint64_t, Memory*> module_map)
      : stack_(stack), module_map_(std::move(module_map)) {}

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

    auto module_it = module_map_.upper_bound(pc);
    if (module_it == module_map_.begin()) {
      return Error("%#" PRIx64 " is not covered by any module", pc);
    }
    module_it--;
    uint64_t module_address = module_it->first;

    auto cfi_it = cfi_map_.find(module_address);
    if (cfi_it == cfi_map_.end()) {
      cfi_it = cfi_map_.emplace(module_address, DwarfCfi(module_it->second, module_address)).first;
      if (auto err = cfi_it->second.Load(); err.has_err()) {
        return err;
      }
    }
    if (auto err = cfi_it->second.Step(stack_, current, next); err.has_err()) {
      return err;
    }
    return Success();
  }

 private:
  Memory* stack_;
  std::map<uint64_t, Memory*> module_map_;
  std::map<uint64_t, DwarfCfi> cfi_map_;
};

}  // namespace

std::string Frame::Describe() const {
  std::string res = "registers={" + regs.Describe() + "}  trust=";
  switch (trust) {
    case Trust::kScan:
      res += "Scan";
      break;
    case Trust::kFP:
      res += "FP";
      break;
    case Trust::kSSC:
      res += "SSC";
      break;
    case Trust::kCFI:
      res += "CFI";
      break;
    case Trust::kContext:
      res += "Context";
      break;
  }
  if (error.has_err()) {
    res += "  error=\"" + error.msg() + "\"";
  }
  return res;
}

std::vector<Frame> Unwind(Memory* memory, const std::vector<uint64_t>& modules,
                          const Registers& registers, size_t max_depth) {
  std::map<uint64_t, Memory*> module_maps;
  for (auto address : modules) {
    module_maps.emplace(address, memory);
  }
  return Unwind(memory, module_maps, registers, max_depth);
}

std::vector<Frame> Unwind(Memory* stack, const std::map<uint64_t, Memory*>& module_map,
                          const Registers& registers, size_t max_depth) {
  std::vector<Frame> res = {{registers, Frame::Trust::kContext, /* placeholder */ Success()}};
  CFIUnwinder cfi_unwinder(stack, module_map);

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
