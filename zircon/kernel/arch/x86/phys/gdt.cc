// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/standard-segments.h>

#include <phys/main.h>

namespace {

// Global GDT and TSS.
arch::X86StandardSegments gStandardSegments;

}  // namespace

void ArchSetUp(void* zbi) { gStandardSegments.Load(); }
