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

// Duplicate a handle created by MakeHandle(). If |is_replace| is true
// then the logic to triger ZX_SIGNAL_LAST_HANDLE is not executed.
Handle* DupHandle(Handle* source, zx_rights_t rights, bool is_replace);

// Deletes a handle created by MakeHandle() or DupHandle(). This might
// trigger ZX_SIGNAL_LAST_HANDLE.
void DeleteHandle(Handle* handle);

// Maps an integer obtained by Handle->base_value() back to a Handle.
Handle* MapU32ToHandle(uint32_t value);

namespace internal {
// Dumps internal details of the handle table using printf().
// Should only be called by diagnostics.cpp.
void DumpHandleTableInfo();

// Returns the number of outstanding handles.
// Should only be called by diagnostics.cpp.
size_t OutstandingHandles();
} // namespace internal
