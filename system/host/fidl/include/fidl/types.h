// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPES_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPES_H_

namespace fidl {
namespace types {

enum struct Nullability {
    kNullable,
    kNonnullable,
};

enum struct HandleSubtype {
    kHandle,
    kProcess,
    kThread,
    kVmo,
    kChannel,
    kEvent,
    kPort,
    kInterrupt,
    kLog,
    kSocket,
    kResource,
    kEventpair,
    kJob,
    kVmar,
    kFifo,
    kGuest,
    kTimer,
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

} // namespace types
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPES_H_
