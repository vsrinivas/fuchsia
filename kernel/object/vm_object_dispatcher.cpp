// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/vm_object_dispatcher.h>

#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

#include <zircon/rights.h>

#include <fbl/alloc_checker.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <trace.h>

#define LOCAL_TRACE 0

zx_status_t VmObjectDispatcher::Create(fbl::RefPtr<VmObject> vmo,
                                       fbl::RefPtr<Dispatcher>* dispatcher,
                                       zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto disp = new (&ac) VmObjectDispatcher(fbl::move(vmo));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    disp->vmo()->set_user_id(disp->get_koid());
    *rights = ZX_DEFAULT_VMO_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

VmObjectDispatcher::VmObjectDispatcher(fbl::RefPtr<VmObject> vmo)
    : vmo_(vmo), state_tracker_(0u) {}

VmObjectDispatcher::~VmObjectDispatcher() {
    // Intentionally leave vmo_->user_id() set to our koid even though we're
    // dying and the koid will no longer map to a Dispatcher. koids are never
    // recycled, and it could be a useful breadcrumb.
}

void VmObjectDispatcher::get_name(char out_name[ZX_MAX_NAME_LEN]) const {
    canary_.Assert();
    vmo_->get_name(out_name, ZX_MAX_NAME_LEN);
}

zx_status_t VmObjectDispatcher::set_name(const char* name, size_t len) {
    canary_.Assert();
    return vmo_->set_name(name, len);
}

zx_status_t VmObjectDispatcher::Read(user_ptr<void> user_data,
                                     size_t length,
                                     uint64_t offset,
                                     size_t* bytes_read) {
    canary_.Assert();

    return vmo_->ReadUser(user_data, offset, length, bytes_read);
}

zx_status_t VmObjectDispatcher::Write(user_ptr<const void> user_data,
                                      size_t length,
                                      uint64_t offset,
                                      size_t* bytes_written) {
    canary_.Assert();

    return vmo_->WriteUser(user_data, offset, length, bytes_written);
}

zx_status_t VmObjectDispatcher::SetSize(uint64_t size) {
    canary_.Assert();

    return vmo_->Resize(size);
}

zx_status_t VmObjectDispatcher::GetSize(uint64_t* size) {
    canary_.Assert();

    *size = vmo_->size();

    return ZX_OK;
}

zx_status_t VmObjectDispatcher::RangeOp(uint32_t op, uint64_t offset, uint64_t size,
                                        user_ptr<void> buffer, size_t buffer_size) {
    canary_.Assert();

    LTRACEF("op %u offset %#" PRIx64 " size %#" PRIx64
            " buffer %p buffer_size %zu\n",
            op, offset, size, buffer.get(), buffer_size);

    switch (op) {
        case ZX_VMO_OP_COMMIT: {
            // TODO: handle partial commits
            auto status = vmo_->CommitRange(offset, size, nullptr);
            return status;
        }
        case ZX_VMO_OP_DECOMMIT: {
            // TODO: handle partial decommits
            auto status = vmo_->DecommitRange(offset, size, nullptr);
            return status;
        }
        case ZX_VMO_OP_LOCK:
        case ZX_VMO_OP_UNLOCK:
            // TODO: handle
            return ZX_ERR_NOT_SUPPORTED;
        case ZX_VMO_OP_LOOKUP:
            // we will be using the user pointer
            if (!buffer)
                return ZX_ERR_INVALID_ARGS;

            // make sure that zx_paddr_t doesn't drift from paddr_t, which the VM uses internally
            static_assert(sizeof(zx_paddr_t) == sizeof(paddr_t), "");

            return vmo_->LookupUser(offset, size, buffer.reinterpret<paddr_t>(), buffer_size);
        case ZX_VMO_OP_CACHE_SYNC:
            return vmo_->SyncCache(offset, size);
        case ZX_VMO_OP_CACHE_INVALIDATE:
            return vmo_->InvalidateCache(offset, size);
        case ZX_VMO_OP_CACHE_CLEAN:
            return vmo_->CleanCache(offset, size);
        case ZX_VMO_OP_CACHE_CLEAN_INVALIDATE:
            return vmo_->CleanInvalidateCache(offset, size);
        default:
            return ZX_ERR_INVALID_ARGS;
    }
}

zx_status_t VmObjectDispatcher::SetMappingCachePolicy(uint32_t cache_policy) {
    return vmo_->SetMappingCachePolicy(cache_policy);
}

zx_status_t VmObjectDispatcher::Clone(uint32_t options, uint64_t offset, uint64_t size,
        bool copy_name, fbl::RefPtr<VmObject>* clone_vmo) {
    canary_.Assert();

    LTRACEF("options 0x%x offset %#" PRIx64 " size %#" PRIx64 "\n",
            options, offset, size);

    if (options & ZX_VMO_CLONE_COPY_ON_WRITE) {
        return vmo_->CloneCOW(offset, size, copy_name, clone_vmo);
    } else {
        return ZX_ERR_INVALID_ARGS;
    }
}
