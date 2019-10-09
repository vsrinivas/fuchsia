// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include "elflib.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  auto elf = elflib::ElfLib::Create(data, size);
  if (elf) {
    elf->ProbeHasDebugInfo();
    elf->ProbeHasProgramBits();
    elf->GetSectionData("bogus");
    elf->GetSegmentHeaders();
    elf->GetAllSymbols();
    elf->GetAllDynamicSymbols();
    elf->GetPLTOffsets();
    elf->GetAndClearWarnings();
  }
  return 0;
}
