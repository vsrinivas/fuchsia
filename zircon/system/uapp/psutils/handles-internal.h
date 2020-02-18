// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UAPP_PSUTILS_HANDLES_INTERNAL_H_
#define ZIRCON_SYSTEM_UAPP_PSUTILS_HANDLES_INTERNAL_H_

#include <zircon/syscalls/object.h>
#include <zircon/types.h>
#include <stdint.h>

#include <vector>

enum Filter : uint64_t {
  kAll = 0,
  kProcess = 1u << (ZX_OBJ_TYPE_PROCESS - 1),
  kThread = 1u << (ZX_OBJ_TYPE_THREAD - 1),
  kVmo = 1u << (ZX_OBJ_TYPE_VMO - 1),
  kChannel = 1u << (ZX_OBJ_TYPE_CHANNEL - 1),
  kEvent = 1u << (ZX_OBJ_TYPE_EVENT - 1),
  kPort = 1u << (ZX_OBJ_TYPE_PORT - 1),
  kInterrupt = 1u << (ZX_OBJ_TYPE_INTERRUPT - 1),
  kPCIDev = 1u << (ZX_OBJ_TYPE_PCI_DEVICE - 1),
  kLog = 1u << (ZX_OBJ_TYPE_LOG - 1),
  kSocket = 1u << (ZX_OBJ_TYPE_SOCKET - 1),
  kResource = 1u << (ZX_OBJ_TYPE_RESOURCE - 1),
  kEventPair = 1u << (ZX_OBJ_TYPE_EVENTPAIR - 1),
  kJob = 1u << (ZX_OBJ_TYPE_JOB - 1),
  kVmar = 1u << (ZX_OBJ_TYPE_VMAR - 1),
  kFifo = 1u << (ZX_OBJ_TYPE_FIFO - 1),
  kGuest = 1u << (ZX_OBJ_TYPE_GUEST - 1),
  kVCpu = 1u << (ZX_OBJ_TYPE_VCPU - 1),
  kTimer = 1u << (ZX_OBJ_TYPE_TIMER - 1),
  kIommu = 1u << (ZX_OBJ_TYPE_IOMMU - 1),
};
// TODO(cpu): Rather than doing this bitshifting, we should just use a std::set of ZX_OBJ.

Filter operator+=(Filter& lhs, const Filter& rhs);
Filter operator~(const Filter& rhs);

size_t print_handles(FILE* f, const std::vector<zx_info_handle_extended_t>& handles, Filter filter);

#endif  // ZIRCON_SYSTEM_UAPP_PSUTILS_HANDLES_INTERNAL_H_
