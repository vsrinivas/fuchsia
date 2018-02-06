// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPES_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPES_H_

namespace fidl {
namespace types {

enum struct HandleSubtype {
    Handle,
    Process,
    Thread,
    Vmo,
    Channel,
    Event,
    Port,
    Interrupt,
    Iomap,
    Pci,
    Log,
    Socket,
    Resource,
    Eventpair,
    Job,
    Vmar,
    Fifo,
    Hypervisor,
    Guest,
    Timer,
};

} // namespace types
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPES_H_
