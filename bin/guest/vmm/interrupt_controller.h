// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_INTERRUPT_CONTROLLER_H_
#define GARNET_BIN_GUEST_VMM_INTERRUPT_CONTROLLER_H_

#if __x86_64__

#include "garnet/bin/guest/vmm/arch/x86/io_apic.h"

using InterruptController = IoApic;

#elif __aarch64__

#include "garnet/bin/guest/vmm/arch/arm64/gic_distributor.h"

using InterruptController = GicDistributor;

#endif  // __aarch64__

#endif  // GARNET_BIN_GUEST_VMM_INTERRUPT_CONTROLLER_H_
