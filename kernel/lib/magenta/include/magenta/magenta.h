// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/handle.h>
#include <magenta/syscalls/resource.h>
#include <magenta/types.h>

#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

class Dispatcher;
class ExceptionPort;
class ProcessDispatcher;
class JobDispatcher;
class PolicyManager;

// Creates a handle attached to |dispatcher| and with |rights| from a
// specific arena which makes their addresses come from a fixed range.
Handle* MakeHandle(mxtl::RefPtr<Dispatcher> dispatcher, mx_rights_t rights);

// Duplicate a handle created by MakeHandle(). If |is_replace| is true
// then the logic to triger MX_SIGNAL_LAST_HANDLE is not executed.
Handle* DupHandle(Handle* source, mx_rights_t rights, bool is_replace);

// Deletes a handle created by MakeHandle() or DupHandle(). This might
// trigger MX_SIGNAL_LAST_HANDLE.
void DeleteHandle(Handle* handle);

// Maps an integer obtained by Handle->base_value() back to a Handle.
Handle* MapU32ToHandle(uint32_t value);

// Set/get the system exception port.
mx_status_t SetSystemExceptionPort(mxtl::RefPtr<ExceptionPort> eport);
// Returns true if a port had been set.
bool ResetSystemExceptionPort();
mxtl::RefPtr<ExceptionPort> GetSystemExceptionPort();

mxtl::RefPtr<JobDispatcher> GetRootJobDispatcher();

PolicyManager* GetSystemPolicyManager();

bool magenta_rights_check(const Handle* handle, mx_rights_t desired);

mx_status_t magenta_sleep(mx_time_t deadline);

// Determines if this handle is to the root resource object.
// Used to provide access to privileged syscalls.
// Later, Resource objects will be finer-grained.
mx_status_t validate_resource_handle(mx_handle_t handle);

// Validates a resource based on type and low/high range;
mx_status_t validate_ranged_resource(mx_handle_t handle, uint32_t kind, uint64_t low,
                                     uint64_t high);

// Validates mapping an MMIO range based on a resource handle
static inline mx_status_t validate_resource_mmio(mx_handle_t handle, uintptr_t base,
                                                 size_t length) {
    if (length < 1 || UINT64_MAX - base < length) {
        return MX_ERR_INVALID_ARGS;
    }
    return validate_ranged_resource(handle, MX_RSRC_KIND_MMIO, base, base + length - 1);
}

// Validates creation of an interrupt object based on a resource handle
static inline mx_status_t validate_resource_irq(mx_handle_t handle, uint32_t irq) {
    return validate_ranged_resource(handle, MX_RSRC_KIND_IRQ, irq, irq);
}

// Convenience function to get go from process handle to process.
mx_status_t get_process(ProcessDispatcher* up,
                        mx_handle_t proc_handle,
                        mxtl::RefPtr<ProcessDispatcher>* proc);

namespace internal {
// Dumps internal details of the handle table using printf().
// Should only be called by diagnostics.cpp.
void DumpHandleTableInfo();

// Returns the number of outstanding handles.
// Should only be called by diagnostics.cpp.
size_t OutstandingHandles();
} // namespace internal
