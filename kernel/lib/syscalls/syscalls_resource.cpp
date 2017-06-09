// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>

#include <magenta/channel_dispatcher.h>
#include <magenta/handle_owner.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/resource_dispatcher.h>
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

// Create a new resource, child of the provided resource.
// records must be an array of valid resource info records
// records[0] must be a mx_rrec_type_self describing the resource.
// On success, a new resource is created and handle is returned
mx_status_t sys_resource_create(mx_handle_t handle,
                                user_ptr<const mx_rrec_t> _records, uint32_t count,
                                user_ptr<mx_handle_t> _rsrc_out) {
    auto up = ProcessDispatcher::GetCurrent();

    // Obtain the parent Resource
    mx_status_t result;
    mxtl::RefPtr<ResourceDispatcher> parent;
    result = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &parent);
    if (result)
        return result;

    // Disallow no records (self is required) or excessive number
    if ((count < 1) || (count > ResourceDispatcher::kMaxRecords))
        return MX_ERR_OUT_OF_RANGE;

    mx_rrec_t rec;
    if (_records.copy_array_from_user(&rec, 1, 0) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    if ((rec.self.type != MX_RREC_SELF) ||
        (rec.self.subtype != MX_RREC_SELF_GENERIC))
        return MX_ERR_INVALID_ARGS;

    // Ensure name is terminated
    rec.self.name[MX_MAX_NAME_LEN - 1] = 0;

    // Create a new child Resource
    mx_rights_t rights;
    mxtl::RefPtr<ResourceDispatcher> child;
    result = ResourceDispatcher::Create(&child, &rights, rec.self.name, rec.self.subtype);
    if (result != MX_OK)
        return result;

    // Add Records to the child, completing its creation
    result = child->AddRecords(_records, count);
    if (result != MX_OK)
        return result;

    // Add child to the parent
    result = parent->AddChild(child);
    if (result != MX_OK)
        return result;

    // Create a handle for the child
    HandleOwner child_h(MakeHandle(mxtl::RefPtr<Dispatcher>(child.get()), rights));
    if (!child_h)
        return MX_ERR_NO_MEMORY;

    if (_rsrc_out.copy_to_user(up->MapHandleToValue(child_h)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(child_h));

    return MX_OK;
}

mx_status_t sys_resource_destroy(mx_handle_t handle) {
    auto up = ProcessDispatcher::GetCurrent();

    mx_status_t result;
    mxtl::RefPtr<ResourceDispatcher> resource;
    result = up->GetDispatcherWithRights(handle, MX_RIGHT_DESTROY, &resource);
    if (result)
        return result;

    // Obtain the parent Resource
    mxtl::RefPtr<ResourceDispatcher> parent;
    if ((result = resource->GetParent(&parent)) != MX_OK)
        return result;

    return parent->DestroyChild(mxtl::move(resource));
}

// Given a resource handle and an index into its array of rsrc info records
// Return a handle appropriate to that index (eg VMO for RSRC_INFO_MMIO, etc)
// resource handle must have RIGHT_READ
mx_status_t sys_resource_get_handle(mx_handle_t handle, uint32_t index,
                                    uint32_t options, user_ptr<mx_handle_t> _out) {
    auto up = ProcessDispatcher::GetCurrent();

    // Obtain the parent Resource
    mx_status_t result;
    mxtl::RefPtr<ResourceDispatcher> resource;
    result = up->GetDispatcherWithRights(handle, MX_RIGHT_EXECUTE, &resource);
    if (result)
        return result;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    result = resource->RecordCreateDispatcher(index, options, &dispatcher, &rights);
    if (result)
        return result;

    HandleOwner out_h(MakeHandle(mxtl::move(dispatcher), rights));
    if (!out_h)
        return MX_ERR_NO_MEMORY;

    if (_out.copy_to_user(up->MapHandleToValue(out_h)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(out_h));

    return MX_OK;
}

// Given a resource handle, perform an action that is specific to that
// resource type (eg, enable/disable PCI bus mastering)
mx_status_t sys_resource_do_action(mx_handle_t handle, uint32_t index,
                                   uint32_t action, uint32_t arg0, uint32_t arg1) {
    auto up = ProcessDispatcher::GetCurrent();

    // Obtain the parent Resource
    mx_status_t result;
    mxtl::RefPtr<ResourceDispatcher> resource;
    result = up->GetDispatcherWithRights(handle, MX_RIGHT_EXECUTE, &resource);
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
    result = up->GetDispatcherWithRights(handle, MX_RIGHT_EXECUTE, &resource);
    if (result)
        return result;

    HandleOwner channel = up->RemoveHandle(channel_hv);

    if (!channel) {
        return MX_ERR_BAD_HANDLE;
    } else if (channel->dispatcher()->get_type() != MX_OBJ_TYPE_CHANNEL) {
        result = MX_ERR_WRONG_TYPE;
    } else if (!magenta_rights_check(channel.get(), MX_RIGHT_TRANSFER)) {
        result = MX_ERR_ACCESS_DENIED;
    } else {
        result = resource->Connect(&channel);
    }

    // If we did not succeed, we must return the channel handle
    // to the caller's handle table
    if (result != MX_OK) {
        up->AddHandle(mxtl::move(channel));
    }

    return result;
}

// Given a resource handle, attempt to accept an inbound connection
// resource handle must have RIGHT_WRITE
mx_status_t sys_resource_accept(mx_handle_t handle, user_ptr<mx_handle_t> _out) {
    auto up = ProcessDispatcher::GetCurrent();

    mx_status_t result;
    mxtl::RefPtr<ResourceDispatcher> resource;
    result = up->GetDispatcherWithRights(handle, MX_RIGHT_WRITE, &resource);
    if (result)
        return result;

    HandleOwner channel;
    result = resource->Accept(&channel);
    if (result)
        return result;

    if (_out.copy_to_user(up->MapHandleToValue(channel)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(channel));

    return MX_OK;
}
