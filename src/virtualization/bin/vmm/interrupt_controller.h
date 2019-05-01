// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_INTERRUPT_CONTROLLER_H_
#define SRC_VIRTUALIZATION_BIN_VMM_INTERRUPT_CONTROLLER_H_

#if __x86_64__

#include "src/virtualization/bin/vmm/arch/x64/io_apic.h"

using InterruptController = IoApic;

#elif __aarch64__

#include "src/virtualization/bin/vmm/arch/arm64/gic_distributor.h"

using InterruptController = GicDistributor;

#endif  // __aarch64__

#endif  // SRC_VIRTUALIZATION_BIN_VMM_INTERRUPT_CONTROLLER_H_
