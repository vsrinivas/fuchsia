// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPES_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPES_H_

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

  kException = ZX_OBJ_TYPE_EXCEPTION,
  kProcess = ZX_OBJ_TYPE_PROCESS,
  kThread = ZX_OBJ_TYPE_THREAD,
  kVmo = ZX_OBJ_TYPE_VMO,
  kChannel = ZX_OBJ_TYPE_CHANNEL,
  kEvent = ZX_OBJ_TYPE_EVENT,
  kPort = ZX_OBJ_TYPE_PORT,
  kInterrupt = ZX_OBJ_TYPE_INTERRUPT,
  kLog = ZX_OBJ_TYPE_LOG,
  kSocket = ZX_OBJ_TYPE_SOCKET,
  kResource = ZX_OBJ_TYPE_RESOURCE,
  kEventpair = ZX_OBJ_TYPE_EVENTPAIR,
  kJob = ZX_OBJ_TYPE_JOB,
  kVmar = ZX_OBJ_TYPE_VMAR,
  kFifo = ZX_OBJ_TYPE_FIFO,
  kGuest = ZX_OBJ_TYPE_GUEST,
  kTimer = ZX_OBJ_TYPE_TIMER,
  kBti = ZX_OBJ_TYPE_BTI,
  kProfile = ZX_OBJ_TYPE_PROFILE,
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

#endif  // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPES_H_
