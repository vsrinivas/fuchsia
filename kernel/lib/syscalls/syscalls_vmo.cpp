// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_address_region.h>

#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/user_copy.h>
#include <magenta/vm_object_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_vmo_create(uint64_t size, uint32_t options, user_ptr<mx_handle_t> out) {
    LTRACEF("size %#" PRIx64 "\n", size);

    if (options)
        return ERR_INVALID_ARGS;

    // create a vm object
    mxtl::RefPtr<VmObject> vmo = VmObjectPaged::Create(0, size);
    if (!vmo)
        return ERR_NO_MEMORY;

    // create a Vm Object dispatcher
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_status_t result = VmObjectDispatcher::Create(mxtl::move(vmo), &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    // create a handle and attach the dispatcher to it
    HandleUniquePtr handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    if (out.copy_to_user(up->MapHandleToValue(handle.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));

    return NO_ERROR;
}

mx_status_t sys_vmo_read(mx_handle_t handle, user_ptr<void> data,
                         uint64_t offset, size_t len, user_ptr<size_t> actual) {
    LTRACEF("handle %d, data %p, offset %#" PRIx64 ", len %#zx\n",
            handle, data.get(), offset, len);

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    mx_status_t status = up->GetDispatcher(handle, &vmo, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    // do the read operation
    size_t nread;
    status = vmo->Read(data, len, offset, &nread);
    if (status == NO_ERROR)
        status = actual.copy_to_user(nread);

    return status;
}

mx_status_t sys_vmo_write(mx_handle_t handle, user_ptr<const void> data,
                          uint64_t offset, size_t len, user_ptr<size_t> actual) {
    LTRACEF("handle %d, data %p, offset %#" PRIx64 ", len %#zx\n",
            handle, data.get(), offset, len);

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    mx_status_t status = up->GetDispatcher(handle, &vmo, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    // do the write operation
    size_t nwritten;
    status = vmo->Write(data, len, offset, &nwritten);
    if (status == NO_ERROR)
        status = actual.copy_to_user(nwritten);

    return status;
}

mx_status_t sys_vmo_get_size(mx_handle_t handle, user_ptr<uint64_t> _size) {
    LTRACEF("handle %d, sizep %p\n", handle, _size.get());

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    mx_status_t status = up->GetDispatcher(handle, &vmo);
    if (status != NO_ERROR)
        return status;

    // no rights check, anyone should be able to get the size

    // do the operation
    uint64_t size = 0;
    status = vmo->GetSize(&size);

    // copy the size back, even if it failed
    if (_size.copy_to_user(size) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return status;
}

mx_status_t sys_vmo_set_size(mx_handle_t handle, uint64_t size) {
    LTRACEF("handle %d, size %#" PRIx64 "\n", handle, size);

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    mx_status_t status = up->GetDispatcher(handle, &vmo, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    // do the operation
    return vmo->SetSize(size);
}

mx_status_t sys_vmo_op_range(mx_handle_t handle, uint32_t op, uint64_t offset, uint64_t size,
                             user_ptr<void> buffer, size_t buffer_size) {
    LTRACEF("handle %d op %u offset %#" PRIx64 " size %#" PRIx64
            " buffer %p buffer_size %zu\n",
            handle, op, offset, size, buffer.get(), buffer_size);

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    mx_rights_t vmo_rights;
    mx_status_t status = up->GetDispatcher(handle, &vmo, &vmo_rights);
    if (status != NO_ERROR)
        return status;

    return vmo->RangeOp(op, offset, size, buffer, buffer_size, vmo_rights);
}
