// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TYPES_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TYPES_H_

#include <zircon/types.h>

namespace fidl {
namespace types {

enum struct Nullability {
  kNullable,
  kNonnullable,
};

enum struct Strictness {
  kFlexible,
  kStrict,
};

// Note: must keep in sync with userspace lib internal.h FidlHandleSubtype.
enum struct HandleSubtype : zx_obj_type_t {
  // special case to indicate subtype is not specified.
  kHandle = ZX_OBJ_TYPE_NONE,

  kBti = ZX_OBJ_TYPE_BTI,
  kChannel = ZX_OBJ_TYPE_CHANNEL,
  kEvent = ZX_OBJ_TYPE_EVENT,
  kEventpair = ZX_OBJ_TYPE_EVENTPAIR,
  kException = ZX_OBJ_TYPE_EXCEPTION,
  kFifo = ZX_OBJ_TYPE_FIFO,
  kGuest = ZX_OBJ_TYPE_GUEST,
  kInterrupt = ZX_OBJ_TYPE_INTERRUPT,
  kIommu = ZX_OBJ_TYPE_IOMMU,
  kJob = ZX_OBJ_TYPE_JOB,
  kLog = ZX_OBJ_TYPE_LOG,
  kPager = ZX_OBJ_TYPE_PAGER,
  kPciDevice = ZX_OBJ_TYPE_PCI_DEVICE,
  kPmt = ZX_OBJ_TYPE_PMT,
  kPort = ZX_OBJ_TYPE_PORT,
  kProcess = ZX_OBJ_TYPE_PROCESS,
  kProfile = ZX_OBJ_TYPE_PROFILE,
  kResource = ZX_OBJ_TYPE_RESOURCE,
  kSocket = ZX_OBJ_TYPE_SOCKET,
  kStream = ZX_OBJ_TYPE_STREAM,
  kSuspendToken = ZX_OBJ_TYPE_SUSPEND_TOKEN,
  kThread = ZX_OBJ_TYPE_THREAD,
  kTimer = ZX_OBJ_TYPE_TIMER,
  kVcpu = ZX_OBJ_TYPE_VCPU,
  kVmar = ZX_OBJ_TYPE_VMAR,
  kVmo = ZX_OBJ_TYPE_VMO,
};

enum struct PrimitiveSubtype {
  kBool,
  kInt8,
  kInt16,
  kInt32,
  kInt64,
  kUint8,
  kUint16,
  kUint32,
  kUint64,
  kFloat32,
  kFloat64,
};

enum struct MessageKind {
  kRequest,
  kResponse,
  kEvent,
};

}  // namespace types
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TYPES_H_
