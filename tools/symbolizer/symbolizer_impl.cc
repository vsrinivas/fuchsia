// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbolizer/symbolizer_impl.h"

#include "src/lib/fxl/strings/string_printf.h"

namespace symbolizer {

// TODO(dangyi): implement the following methods using zxdb library.
void SymbolizerImpl::Reset() { printer_->OutputWithContext("Reset()"); }

void SymbolizerImpl::Module(uint64_t id, std::string_view name, std::string_view build_id) {
  auto info = fxl::StringPrintf("Module(0x%lx, \"%s\", \"%s\")", (unsigned long)id,
                                std::string(name).data(), std::string(build_id).data());
  printer_->OutputWithContext(info);
}

void SymbolizerImpl::MMap(uint64_t address, uint64_t size, uint64_t module_id,
                          uint64_t module_offset) {
  auto info = fxl::StringPrintf("MMap(0x%lx, 0x%lx, 0x%lx, 0x%lx)", (unsigned long)address,
                                (unsigned long)size, (unsigned long)module_id,
                                (unsigned long)module_offset);
  printer_->OutputWithContext(info);
}

void SymbolizerImpl::Backtrace(int frame_index, uint64_t address, AddressType type,
                               std::string_view message) {
  auto info = fxl::StringPrintf("Backtrace(%d, 0x%lx, %d, \"%s\")", frame_index,
                                (unsigned long)address, type, std::string(message).data());
  printer_->OutputWithContext(info);
}

}  // namespace symbolizer
