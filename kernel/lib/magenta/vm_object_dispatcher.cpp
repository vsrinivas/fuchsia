// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/vm_object_dispatcher.h>

#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>

#include <assert.h>
#include <new.h>
#include <err.h>
#include <inttypes.h>
#include <trace.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultVmoRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE | MX_RIGHT_MAP;

status_t VmObjectDispatcher::Create(mxtl::RefPtr<VmObject> vmo,
                                    mxtl::RefPtr<Dispatcher>* dispatcher,
                                    mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = new (&ac) VmObjectDispatcher(mxtl::move(vmo));
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultVmoRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

VmObjectDispatcher::VmObjectDispatcher(mxtl::RefPtr<VmObject> vmo)
    : vmo_(vmo), state_tracker_(0u) {}

VmObjectDispatcher::~VmObjectDispatcher() {}

mx_status_t VmObjectDispatcher::Read(user_ptr<void> user_data,
                                     size_t length,
                                     uint64_t offset,
                                     size_t* bytes_read) {
    return vmo_->ReadUser(user_data, offset, length, bytes_read);
}

mx_status_t VmObjectDispatcher::Write(user_ptr<const void> user_data,
                                      size_t length,
                                      uint64_t offset,
                                      size_t* bytes_written) {

    return vmo_->WriteUser(user_data, offset, length, bytes_written);
}

mx_status_t VmObjectDispatcher::SetSize(uint64_t size) {
    return vmo_->Resize(size);
}

mx_status_t VmObjectDispatcher::GetSize(uint64_t* size) {
    *size = vmo_->size();

    return NO_ERROR;
}

mx_status_t VmObjectDispatcher::RangeOp(uint32_t op, uint64_t offset, uint64_t size,
                                        user_ptr<void> buffer, size_t buffer_size, mx_rights_t rights) {
    LTRACEF("op %u offset %#" PRIx64 " size %#" PRIx64
            " buffer %p buffer_size %zu rights %#x\n",
            op, offset, size, buffer.get(), buffer_size, rights);

    // TODO: test rights

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

            return vmo_->Lookup(offset, size, buffer.reinterpret<paddr_t>(), buffer_size);
        case MX_VMO_OP_CACHE_SYNC:
            // TODO: handle
            return ERR_NOT_SUPPORTED;
        default:
            return ERR_INVALID_ARGS;
    }
}
