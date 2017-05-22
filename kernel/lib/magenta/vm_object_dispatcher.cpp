// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/vm_object_dispatcher.h>

#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>

#include <mxalloc/new.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <trace.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultVmoRights =
    MX_RIGHT_DUPLICATE |
    MX_RIGHT_TRANSFER |
    MX_RIGHT_READ |
    MX_RIGHT_WRITE |
    MX_RIGHT_EXECUTE |
    MX_RIGHT_MAP |
    MX_RIGHT_GET_PROPERTY |
    MX_RIGHT_SET_PROPERTY;

status_t VmObjectDispatcher::Create(mxtl::RefPtr<VmObject> vmo,
                                    mxtl::RefPtr<Dispatcher>* dispatcher,
                                    mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = new (&ac) VmObjectDispatcher(mxtl::move(vmo));
    if (!ac.check())
        return ERR_NO_MEMORY;

    disp->vmo()->set_user_id(disp->get_koid());
    *rights = kDefaultVmoRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

VmObjectDispatcher::VmObjectDispatcher(mxtl::RefPtr<VmObject> vmo)
    : vmo_(vmo), state_tracker_(0u) {}

VmObjectDispatcher::~VmObjectDispatcher() {
    // Intentionally leave vmo_->user_id() set to our koid even though we're
    // dying and the koid will no longer map to a Dispatcher. koids are never
    // recycled, and it could be a useful breadcrumb.
}

void VmObjectDispatcher::get_name(char out_name[MX_MAX_NAME_LEN]) const {
    canary_.Assert();
    vmo_->get_name(out_name);
}

status_t VmObjectDispatcher::set_name(const char* name, size_t len) {
    canary_.Assert();
    return vmo_->set_name(name, len);
}

mx_status_t VmObjectDispatcher::Read(user_ptr<void> user_data,
                                     size_t length,
                                     uint64_t offset,
                                     size_t* bytes_read) {
    canary_.Assert();

    return vmo_->ReadUser(user_data, offset, length, bytes_read);
}

mx_status_t VmObjectDispatcher::Write(user_ptr<const void> user_data,
                                      size_t length,
                                      uint64_t offset,
                                      size_t* bytes_written) {
    canary_.Assert();

    return vmo_->WriteUser(user_data, offset, length, bytes_written);
}

mx_status_t VmObjectDispatcher::SetSize(uint64_t size) {
    canary_.Assert();

    return vmo_->Resize(size);
}

mx_status_t VmObjectDispatcher::GetSize(uint64_t* size) {
    canary_.Assert();

    *size = vmo_->size();

    return NO_ERROR;
}

mx_status_t VmObjectDispatcher::RangeOp(uint32_t op, uint64_t offset, uint64_t size,
                                        user_ptr<void> buffer, size_t buffer_size) {
    canary_.Assert();

    LTRACEF("op %u offset %#" PRIx64 " size %#" PRIx64
            " buffer %p buffer_size %zu\n",
            op, offset, size, buffer.get(), buffer_size);

    switch (op) {
        case MX_VMO_OP_COMMIT: {
            // TODO: handle partial commits
            auto status = vmo_->CommitRange(offset, size, nullptr);
            return status;
        }
        case MX_VMO_OP_DECOMMIT: {
            // TODO: handle partial decommits
            auto status = vmo_->DecommitRange(offset, size, nullptr);
            return status;
        }
        case MX_VMO_OP_LOCK:
        case MX_VMO_OP_UNLOCK:
            // TODO: handle
            return ERR_NOT_SUPPORTED;
        case MX_VMO_OP_LOOKUP:
            // we will be using the user pointer
            if (!buffer)
                return ERR_INVALID_ARGS;

            // make sure that mx_paddr_t doesn't drift from paddr_t, which the VM uses internally
            static_assert(sizeof(mx_paddr_t) == sizeof(paddr_t), "");

            return vmo_->LookupUser(offset, size, buffer.reinterpret<paddr_t>(), buffer_size);
        case MX_VMO_OP_CACHE_SYNC:
            return vmo_->SyncCache(offset, size);
        case MX_VMO_OP_CACHE_INVALIDATE:
            return vmo_->InvalidateCache(offset, size);
        case MX_VMO_OP_CACHE_CLEAN:
            return vmo_->CleanCache(offset, size);
        case MX_VMO_OP_CACHE_CLEAN_INVALIDATE:
            return vmo_->CleanInvalidateCache(offset, size);
        default:
            return ERR_INVALID_ARGS;
    }
}

mx_status_t VmObjectDispatcher::SetMappingCachePolicy(uint32_t cache_policy) {
    return vmo_->SetMappingCachePolicy(cache_policy);
}

mx_status_t VmObjectDispatcher::Clone(uint32_t options, uint64_t offset, uint64_t size,
        mxtl::RefPtr<VmObject>* clone_vmo) {
    canary_.Assert();

    LTRACEF("options 0x%x offset %#" PRIx64 " size %#" PRIx64 "\n",
            options, offset, size);

    if (options & MX_VMO_CLONE_COPY_ON_WRITE) {
        return vmo_->CloneCOW(offset, size, clone_vmo);
    } else {
        return ERR_INVALID_ARGS;
    }
}
