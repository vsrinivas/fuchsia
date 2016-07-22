// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>

#include <sys/types.h>

class VmObject;
class VmAspace;

class VmObjectDispatcher : public Dispatcher {
public:
    static status_t Create(utils::RefPtr<VmObject> vmo, utils::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~VmObjectDispatcher() final;
    mx_obj_type_t GetType() const final { return MX_OBJ_TYPE_VMEM; }
    VmObjectDispatcher* get_vm_object_dispatcher() final { return this; }

    mx_ssize_t Read(void* user_data, mx_size_t length, uint64_t offset);
    mx_ssize_t Write(const void* user_data, mx_size_t length, uint64_t offset);
    mx_status_t SetSize(uint64_t);
    mx_status_t GetSize(uint64_t* size);

    // XXX really belongs in process
    mx_status_t Map(utils::RefPtr<VmAspace> aspace, uint32_t vmo_rights, uint64_t offset, mx_size_t len,
                    uintptr_t* ptr, uint32_t flags);

private:
    explicit VmObjectDispatcher(utils::RefPtr<VmObject> vmo);

    utils::RefPtr<VmObject> vmo_;
};
