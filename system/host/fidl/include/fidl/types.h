// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPES_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPES_H_

namespace fidl {
namespace types {

enum struct Nullability {
    Nullable,
    Nonnullable,
};

enum struct HandleSubtype {
    Handle,
    Process,
    Thread,
    Vmo,
    Channel,
    Event,
    Port,
    Interrupt,
    Log,
    Socket,
    Resource,
    Eventpair,
    Job,
    Vmar,
    Fifo,
    Guest,
    Timer,
};

enum struct PrimitiveSubtype {
    Bool,
    Status,
    Int8,
    Int16,
    Int32,
    Int64,
    Uint8,
    Uint16,
    Uint32,
    Uint64,
    Float32,
    Float64,
};

enum struct MessageKind {
    kRequest,
    kResponse,
    kEvent,
};

} // namespace types
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPES_H_
