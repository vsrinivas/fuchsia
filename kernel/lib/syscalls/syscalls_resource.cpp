// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <kernel/auto_lock.h>
#include <magenta/magenta.h>
#include <magenta/channel_dispatcher.h>
#include <magenta/process_dispatcher.h>
#include <magenta/resource_dispatcher.h>
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

// Create a new resource, child of the provided resource.
// records must be an array of valid resource info records
// records[0] must be a mx_rrec_type_self describing the resource.
// On success, a new resource is created and handle is returned
// If records[0].options MX_ROPT_SELF_CHANNEL, a new ipc channel is returned
// via new_channel_handle and ipc connections may be accepted via this pipe.
// parent_handle must have RIGHT_WRITE
mx_status_t sys_resource_create(mx_handle_t handle,
                                user_ptr<const mx_rrec_t> records, uint32_t count,
                                user_ptr<mx_handle_t> rsrc_out) {
    auto up = ProcessDispatcher::GetCurrent();

    // Obtain the parent Resource
    mx_status_t result;
    mxtl::RefPtr<ResourceDispatcher> parent;
    result = up->GetDispatcher<ResourceDispatcher>(handle, &parent, MX_RIGHT_WRITE);
    if (result)
        return result;

    // Disallow no records (self is required) or excessive number
    if ((count < 1) || (count > ResourceDispatcher::kMaxRecords))
        return ERR_OUT_OF_RANGE;

    mx_rrec_t rec;
    if (records.copy_array_from_user(&rec, 1, 0) != NO_ERROR)
        return ERR_INVALID_ARGS;

    if ((rec.self.type != MX_RREC_SELF) ||
        (rec.self.subtype != MX_RREC_SELF_GENERIC))
        return ERR_INVALID_ARGS;

    // Ensure name is terminated
    rec.self.name[MX_MAX_NAME_LEN - 1] = 0;

    // Create a new child Resource
    mx_rights_t rights;
    mxtl::RefPtr<ResourceDispatcher> child;
    result = ResourceDispatcher::Create(&child, &rights, rec.self.name, rec.self.subtype);
    if (result != NO_ERROR)
        return result;

    // Add Records to the child
    result = child->AddRecords(records, count);
    if (result != NO_ERROR)
        return result;

    // Add child to the parent (validating it)
    result = parent->AddChild(child);
    if (result != NO_ERROR)
        return result;

    // Create a handle for the child
    HandleUniquePtr child_h(MakeHandle(mxtl::RefPtr<Dispatcher>(child.get()), rights));
    if (!child_h)
        return ERR_NO_MEMORY;

    if (rsrc_out.copy_to_user(up->MapHandleToValue(child_h.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(child_h));

    return NO_ERROR;
}

// Given a resource handle and an index into its array of rsrc info records
// Return a handle appropriate to that index (eg VMO for RSRC_INFO_MMIO, etc)
// resource handle must have RIGHT_READ
mx_status_t sys_resource_get_handle(mx_handle_t handle, uint32_t index,
                                    uint32_t options, user_ptr<mx_handle_t> out) {
    auto up = ProcessDispatcher::GetCurrent();

    // Obtain the parent Resource
    mx_status_t result;
    mxtl::RefPtr<ResourceDispatcher> resource;
    result = up->GetDispatcher<ResourceDispatcher>(handle, &resource, MX_RIGHT_EXECUTE);
    if (result)
        return result;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    result = resource->RecordCreateDispatcher(index, options, &dispatcher, &rights);
    if (result)
        return result;

    HandleUniquePtr out_h(MakeHandle(mxtl::move(dispatcher), rights));
    if (!out_h)
        return ERR_NO_MEMORY;

    if (out.copy_to_user(up->MapHandleToValue(out_h.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(out_h));

    return NO_ERROR;
}

// Given a resource handle, perform an action that is specific to that
// resource type (eg, enable/disable PCI bus mastering)
mx_status_t sys_resource_do_action(mx_handle_t handle, uint32_t index,
                                   uint32_t action, uint32_t arg0, uint32_t arg1) {
    auto up = ProcessDispatcher::GetCurrent();

    // Obtain the parent Resource
    mx_status_t result;
    mxtl::RefPtr<ResourceDispatcher> resource;
    result = up->GetDispatcher<ResourceDispatcher>(handle, &resource, MX_RIGHT_EXECUTE);
    if (result)
        return result;

    return resource->RecordDoAction(index, action, arg0, arg1);
}

// Given a resource handle and a channel handle attempt to connect
// that channel to the service behind the resource
// resource handle must have RIGHT_EXECUTE
// channel handle must have RIGHT_TRANSFER
mx_status_t sys_resource_connect(mx_handle_t handle, mx_handle_t channel_hv) {
    auto up = ProcessDispatcher::GetCurrent();

    mx_status_t result;
    mxtl::RefPtr<ResourceDispatcher> resource;
    result = up->GetDispatcher<ResourceDispatcher>(handle, &resource, MX_RIGHT_EXECUTE);
    if (result)
        return result;

    HandleUniquePtr channel = up->RemoveHandle(channel_hv);

    if (!channel) {
        return up->BadHandle(channel_hv, ERR_BAD_HANDLE);
    } else if (channel->dispatcher()->get_type() != MX_OBJ_TYPE_CHANNEL) {
        result = ERR_WRONG_TYPE;
    } else if (!magenta_rights_check(channel->rights(), MX_RIGHT_TRANSFER)) {
        result = ERR_ACCESS_DENIED;
    } else {
        result = resource->Connect(&channel);
    }

    // If we did not succeed, we must return the channel handle
    // to the caller's handle table
    if (result != NO_ERROR) {
        up->AddHandle(mxtl::move(channel));
    }

    return result;
}

// Given a resource handle, attempt to accept an inbound connection
// resource handle must have RIGHT_WRITE
mx_status_t sys_resource_accept(mx_handle_t handle, user_ptr<mx_handle_t> out) {
    auto up = ProcessDispatcher::GetCurrent();

    mx_status_t result;
    mxtl::RefPtr<ResourceDispatcher> resource;
    result = up->GetDispatcher<ResourceDispatcher>(handle, &resource, MX_RIGHT_WRITE);
    if (result)
        return result;

    HandleUniquePtr channel;
    result = resource->Accept(&channel);
    if (result)
        return result;

    if (out.copy_to_user(up->MapHandleToValue(channel.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(channel));

    return NO_ERROR;
}
