// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_PCI_ALLOCATORS_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_PCI_ALLOCATORS_H_

#include <region-alloc/region-alloc.h>

RegionAllocator* Get32BitMmioAllocator();
RegionAllocator* Get64BitMmioAllocator();
RegionAllocator* GetIoAllocator();

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_PCI_ALLOCATORS_H_
