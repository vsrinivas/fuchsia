// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <zircon/types.h>
#include <fbl/ref_ptr.h>

class Dispatcher;
class Handle;

// Creates a handle attached to |dispatcher| and with |rights| from a
// specific arena which makes their addresses come from a fixed range.
Handle* MakeHandle(fbl::RefPtr<Dispatcher> dispatcher, zx_rights_t rights);

// Duplicate a handle created by MakeHandle().
Handle* DupHandle(Handle* source, zx_rights_t rights);

// Get the number of outstanding handles for a given dispatcher.
uint32_t GetHandleCount(const fbl::RefPtr<Dispatcher>& dispatcher);

// Deletes a handle created by MakeHandle() or DupHandle().
void DeleteHandle(Handle* handle);

// Maps an integer obtained by Handle->base_value() back to a Handle.
Handle* MapU32ToHandle(uint32_t value);

// To be called once during bringup.
void HandleTableInit();

namespace internal {
// Dumps internal details of the handle table using printf().
// Should only be called by diagnostics.cpp.
void DumpHandleTableInfo();

// Returns the number of outstanding handles.
// Should only be called by diagnostics.cpp.
size_t OutstandingHandles();
} // namespace internal
