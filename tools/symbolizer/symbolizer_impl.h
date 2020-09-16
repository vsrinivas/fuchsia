// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOLIZER_SYMBOLIZER_IMPL_H_
#define TOOLS_SYMBOLIZER_SYMBOLIZER_IMPL_H_

#include <iostream>
#include <string_view>

#include "tools/symbolizer/printer.h"
#include "tools/symbolizer/symbolizer.h"

namespace symbolizer {

// This is the core logic of the symbolizer. We provide a MockSymbolizer and a SymbolizerImpl for
// better testing.
class SymbolizerImpl : public Symbolizer {
 public:
  explicit SymbolizerImpl(Printer* printer) : printer_(printer) {}

  void Reset() override;
  void Module(uint64_t id, std::string_view name, std::string_view build_id) override;
  void MMap(uint64_t address, uint64_t size, uint64_t module_id, uint64_t module_offset) override;
  void Backtrace(int frame_id, uint64_t address, AddressType type,
                 std::string_view message) override;

 private:
  Printer* printer_;
};

}  // namespace symbolizer

#endif  // TOOLS_SYMBOLIZER_SYMBOLIZER_IMPL_H_
