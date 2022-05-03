// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/boot-cpuid.h>

#include <phys/main.h>

void ArchSetUp(void* zbi) { arch::InitializeBootCpuid(); }
