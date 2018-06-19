// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>

#include <lib/user_copy/user_ptr.h>

#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/vm_object_dispatcher.h>

#include <fbl/ref_ptr.h>

#include "priv.h"

#define LOCAL_TRACE 0

static_assert(ZX_CACHE_POLICY_CACHED == ARCH_MMU_FLAG_CACHED,
              "Cache policy constant mismatch - CACHED");
static_assert(ZX_CACHE_POLICY_UNCACHED == ARCH_MMU_FLAG_UNCACHED,
              "Cache policy constant mismatch - UNCACHED");
static_assert(ZX_CACHE_POLICY_UNCACHED_DEVICE == ARCH_MMU_FLAG_UNCACHED_DEVICE,
              "Cache policy constant mismatch - UNCACHED_DEVICE");
static_assert(ZX_CACHE_POLICY_WRITE_COMBINING == ARCH_MMU_FLAG_WRITE_COMBINING,
              "Cache policy constant mismatch - WRITE_COMBINING");
static_assert(ZX_CACHE_POLICY_MASK == ARCH_MMU_FLAG_CACHE_MASK,
              "Cache policy constant mismatch - CACHE_MASK");

zx_status_t sys_vmo_create(uint64_t size, uint32_t options,
                           user_out_handle* out) {
    LTRACEF("size %#" PRIx64 "\n", size);

    switch (options) {
    case 0: options = VmObjectPaged::kResizable; break;
    case ZX_VMO_NON_RESIZABLE: options = 0u; break;
    default: return ZX_ERR_INVALID_ARGS;
    }

    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t res = up->QueryPolicy(ZX_POL_NEW_VMO);
    if (res != ZX_OK)
        return res;

    // create a vm object
    fbl::RefPtr<VmObject> vmo;
    res = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, options, size, &vmo);
    if (res != ZX_OK)
        return res;

    // create a Vm Object dispatcher
    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;
    zx_status_t result = VmObjectDispatcher::Create(fbl::move(vmo), &dispatcher, &rights);
    if (result != ZX_OK)
        return result;

    // create a handle and attach the dispatcher to it
    return out->make(fbl::move(dispatcher), rights);
}

zx_status_t sys_vmo_read(zx_handle_t handle, user_out_ptr<void> _data,
                         uint64_t offset, size_t len) {
    LTRACEF("handle %x, data %p, offset %#" PRIx64 ", len %#zx\n",
            handle, _data.get(), offset, len);

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    fbl::RefPtr<VmObjectDispatcher> vmo;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &vmo);
    if (status != ZX_OK)
        return status;

    // Force map the range, even if it crosses multiple mappings.
    // TODO(ZX-730): This is a workaround for this bug.  If we start decommitting
    // things, the bug will come back.  We should fix this more properly.
    {
        uint8_t byte = 0;
        auto int_data = _data.reinterpret<uint8_t>();
        for (size_t i = 0; i < len; i += PAGE_SIZE) {
            status = int_data.copy_array_to_user(&byte, 1, i);
            if (status != ZX_OK) {
                return status;
            }
        }
        if (len > 0) {
            status = int_data.copy_array_to_user(&byte, 1, len - 1);
            if (status != ZX_OK) {
                return status;
            }
        }
    }

    return vmo->Read(_data, len, offset);
}

zx_status_t sys_vmo_write(zx_handle_t handle, user_in_ptr<const void> _data,
                          uint64_t offset, size_t len) {
    LTRACEF("handle %x, data %p, offset %#" PRIx64 ", len %#zx\n",
            handle, _data.get(), offset, len);

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    fbl::RefPtr<VmObjectDispatcher> vmo;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &vmo);
    if (status != ZX_OK)
        return status;

    // Force map the range, even if it crosses multiple mappings.
    // TODO(ZX-730): This is a workaround for this bug.  If we start decommitting
    // things, the bug will come back.  We should fix this more properly.
    {
        uint8_t byte = 0;
        auto int_data = _data.reinterpret<const uint8_t>();
        for (size_t i = 0; i < len; i += PAGE_SIZE) {
            status = int_data.copy_array_from_user(&byte, 1, i);
            if (status != ZX_OK) {
                return status;
            }
        }
        if (len > 0) {
            status = int_data.copy_array_from_user(&byte, 1, len - 1);
            if (status != ZX_OK) {
                return status;
            }
        }
    }

    return vmo->Write(_data, len, offset);
}

zx_status_t sys_vmo_get_size(zx_handle_t handle, user_out_ptr<uint64_t> _size) {
    LTRACEF("handle %x, sizep %p\n", handle, _size.get());

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    fbl::RefPtr<VmObjectDispatcher> vmo;
    zx_status_t status = up->GetDispatcher(handle, &vmo);
    if (status != ZX_OK)
        return status;

    // no rights check, anyone should be able to get the size

    // do the operation
    uint64_t size = 0;
    status = vmo->GetSize(&size);

    // copy the size back, even if it failed
    status = _size.copy_to_user(size);
    if (status != ZX_OK)
        return status;

    return status;
}

zx_status_t sys_vmo_set_size(zx_handle_t handle, uint64_t size) {
    LTRACEF("handle %x, size %#" PRIx64 "\n", handle, size);

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    fbl::RefPtr<VmObjectDispatcher> vmo;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &vmo);
    if (status != ZX_OK)
        return status;

    // do the operation
    return vmo->SetSize(size);
}

zx_status_t sys_vmo_op_range(zx_handle_t handle, uint32_t op, uint64_t offset, uint64_t size,
                             user_inout_ptr<void> _buffer, size_t buffer_size) {
    LTRACEF("handle %x op %u offset %#" PRIx64 " size %#" PRIx64
            " buffer %p buffer_size %zu\n",
            handle, op, offset, size, _buffer.get(), buffer_size);

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    // save the rights and pass down into the dispatcher for further testing
    fbl::RefPtr<VmObjectDispatcher> vmo;
    zx_rights_t rights;
    zx_status_t status = up->GetDispatcherAndRights(handle, &vmo, &rights);
    if (status != ZX_OK) {
        return status;
    }

    return vmo->RangeOp(op, offset, size, _buffer, buffer_size, rights);
}

zx_status_t sys_vmo_set_cache_policy(zx_handle_t handle, uint32_t cache_policy) {
    fbl::RefPtr<VmObjectDispatcher> vmo;
    zx_status_t status = ZX_OK;
    auto up = ProcessDispatcher::GetCurrent();

    // Sanity check the cache policy.
    if (cache_policy & ~ZX_CACHE_POLICY_MASK) {
        return ZX_ERR_INVALID_ARGS;
    }

    // lookup the dispatcher from handle.
    status = up->GetDispatcherWithRights(handle, ZX_RIGHT_MAP, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    return vmo->SetMappingCachePolicy(cache_policy);
}

zx_status_t sys_vmo_clone(zx_handle_t handle, uint32_t options,
                          uint64_t offset, uint64_t size,
                          user_out_handle* out_handle) {
    LTRACEF("handle %x options %#x offset %#" PRIx64 " size %#" PRIx64 "\n",
            handle, options, offset, size);

    auto up = ProcessDispatcher::GetCurrent();

    zx_status_t status;
    fbl::RefPtr<VmObject> clone_vmo;
    zx_rights_t in_rights;

    {
        // lookup the dispatcher from handle, save a copy of the rights for later
        fbl::RefPtr<VmObjectDispatcher> vmo;
        status = up->GetDispatcherWithRights(handle, ZX_RIGHT_DUPLICATE | ZX_RIGHT_READ, &vmo, &in_rights);
        if (status != ZX_OK)
            return status;

        // clone the vmo into a new one
        status = vmo->Clone(options, offset, size, in_rights & ZX_RIGHT_GET_PROPERTY,  &clone_vmo);
        if (status != ZX_OK)
            return status;

        DEBUG_ASSERT(clone_vmo);
    }

    // create a Vm Object dispatcher
    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t default_rights;
    zx_status_t result = VmObjectDispatcher::Create(fbl::move(clone_vmo), &dispatcher, &default_rights);
    if (result != ZX_OK)
        return result;

    // Set the rights to the new handle to no greater than the input
    // handle, plus WRITE if making a COW clone, and always allow
    // GET/SET_PROPERTY so the user can set ZX_PROP_NAME on the new clone.
    zx_rights_t rights =
        in_rights | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_SET_PROPERTY;
    if (options & ZX_VMO_CLONE_COPY_ON_WRITE)
        rights |= ZX_RIGHT_WRITE;

    // make sure we're somehow not elevating rights beyond what a new vmo should have
    DEBUG_ASSERT((default_rights & rights) == rights);

    // create a handle and attach the dispatcher to it
    return out_handle->make(fbl::move(dispatcher), rights);
}
