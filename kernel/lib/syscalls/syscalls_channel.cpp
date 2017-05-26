// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/auto_lock.h>

#include <lib/ktrace.h>
#include <lib/user_copy.h>

#include <magenta/channel_dispatcher.h>
#include <magenta/handle_owner.h>
#include <magenta/magenta.h>
#include <magenta/message_packet.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/policy.h>
#include <magenta/user_copy.h>

#include <mxtl/algorithm.h>
#include <mxtl/inline_array.h>
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr size_t kChannelReadHandlesChunkCount = 16u;
constexpr size_t kChannelWriteHandlesInlineCount = 8u;

mx_status_t sys_channel_create(
    uint32_t options, user_ptr<mx_handle_t> _out0, user_ptr<mx_handle_t> _out1) {
    LTRACEF("out_handles %p,%p\n", _out0.get(), _out1.get());

    if (options != 0u)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mx_status_t res = up->QueryPolicy(MX_POL_NEW_CHANNEL);
    if (res < 0)
        return res;

    mxtl::RefPtr<Dispatcher> mpd0, mpd1;
    mx_rights_t rights;
    status_t result = ChannelDispatcher::Create(options, &mpd0, &mpd1, &rights);
    if (result != NO_ERROR)
        return result;

    uint64_t id0 = mpd0->get_koid();
    uint64_t id1 = mpd1->get_koid();

    HandleOwner h0(MakeHandle(mxtl::move(mpd0), rights));
    if (!h0)
        return ERR_NO_MEMORY;

    HandleOwner h1(MakeHandle(mxtl::move(mpd1), rights));
    if (!h1)
        return ERR_NO_MEMORY;

    if (_out0.copy_to_user(up->MapHandleToValue(h0)) != NO_ERROR)
        return ERR_INVALID_ARGS;
    if (_out1.copy_to_user(up->MapHandleToValue(h1)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(h0));
    up->AddHandle(mxtl::move(h1));

    ktrace(TAG_CHANNEL_CREATE, (uint32_t)id0, (uint32_t)id1, options, 0);
    return NO_ERROR;
}

void msg_get_handles(ProcessDispatcher* up, MessagePacket* msg,
                     user_ptr<mx_handle_t> _handles, uint32_t num_handles) {
    Handle* const* handle_list = msg->handles();
    msg->set_owns_handles(false);

    // Copy the handle values out in chunks.
    mx_handle_t hvs[kChannelReadHandlesChunkCount];
    size_t num_copied = 0;

    do {
        size_t this_chunk_size = mxtl::min(num_handles - num_copied,
                                           kChannelReadHandlesChunkCount);
        for (size_t i = 0; i < this_chunk_size; i++)
            hvs[i] = up->MapHandleToValue(handle_list[num_copied + i]);
        _handles.element_offset(num_copied).copy_array_to_user(hvs, this_chunk_size);
        num_copied += this_chunk_size;
    } while (num_copied < num_handles);

    for (size_t idx = 0u; idx < num_handles; ++idx) {
        if (handle_list[idx]->dispatcher()->get_state_tracker())
            handle_list[idx]->dispatcher()->get_state_tracker()->Cancel(handle_list[idx]);
        HandleOwner handle(handle_list[idx]);
        up->AddHandle(mxtl::move(handle));
    }
}

mx_status_t sys_channel_read(mx_handle_t handle_value, uint32_t options,
                             user_ptr<void> _bytes, user_ptr<mx_handle_t> _handles,
                             uint32_t num_bytes, uint32_t num_handles,
                             user_ptr<uint32_t> _num_bytes, user_ptr<uint32_t> _num_handles) {
    LTRACEF("handle %d bytes %p num_bytes %p handles %p num_handles %p",
            handle_value, _bytes.get(), _num_bytes.get(), _handles.get(), _num_handles.get());

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ChannelDispatcher> channel;
    mx_status_t result = up->GetDispatcherWithRights(handle_value, MX_RIGHT_READ, &channel);
    if (result != NO_ERROR)
        return result;

    // Currently MAY_DISCARD is the only allowable option.
    if (options & ~MX_CHANNEL_READ_MAY_DISCARD)
        return ERR_NOT_SUPPORTED;

    mxtl::unique_ptr<MessagePacket> msg;
    result = channel->Read(&num_bytes, &num_handles, &msg,
                           options & MX_CHANNEL_READ_MAY_DISCARD);
    if (result != NO_ERROR && result != ERR_BUFFER_TOO_SMALL)
        return result;

    // On ERR_BUFFER_TOO_SMALL, Read() gives us the size of the next message (which remains
    // unconsumed, unless |options| has MX_CHANNEL_READ_MAY_DISCARD set).
    if (_num_bytes) {
        if (_num_bytes.copy_to_user(num_bytes) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }
    if (_num_handles) {
        if (_num_handles.copy_to_user(num_handles) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }
    if (result == ERR_BUFFER_TOO_SMALL)
        return result;

    if (num_bytes > 0u) {
        if (_bytes.copy_array_to_user(msg->data(), num_bytes) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    // The documented public API states that that writing to the handles buffer
    // must happen after writing to the data buffer.
    if (num_handles > 0u) {
        msg_get_handles(up, msg.get(), _handles, num_handles);
    }

    ktrace(TAG_CHANNEL_READ, (uint32_t)channel->get_koid(), num_bytes, num_handles, 0);
    return result;
}

static mx_status_t msg_put_handles(ProcessDispatcher* up, MessagePacket* msg, mx_handle_t* handles,
                                   user_ptr<const mx_handle_t> _handles, uint32_t num_handles,
                                   Dispatcher* channel) {

    if (_handles.copy_array_from_user(handles, num_handles) != NO_ERROR)
        return ERR_INVALID_ARGS;

    {
        // Loop twice, first we collect and validate handles, the second pass
        // we remove them from this process.
        AutoLock lock(up->handle_table_lock());

        for (size_t ix = 0; ix != num_handles; ++ix) {
            auto handle = up->GetHandleLocked(handles[ix]);
            if (!handle)
                return ERR_BAD_HANDLE;

            if (handle->dispatcher().get() == channel) {
                // You may not write a channel endpoint handle
                // into that channel endpoint
                return ERR_NOT_SUPPORTED;
            }

            if (!magenta_rights_check(handle, MX_RIGHT_TRANSFER))
                return ERR_ACCESS_DENIED;

            msg->mutable_handles()[ix] = handle;
        }

        for (size_t ix = 0; ix != num_handles; ++ix) {
            auto handle = up->RemoveHandleLocked(handles[ix]).release();
            // Passing duplicate handles is not allowed.
            // If we've already seen this handle flag an error.
            if (!handle) {
                // Put back the handles we've already removed.
                for (size_t idx = 0; idx < ix; ++idx) {
                    up->UndoRemoveHandleLocked(handles[idx]);
                }
                // TODO: more specific error?
                return ERR_INVALID_ARGS;
            }
        }
    }

    // On success, the MessagePacket owns the handles.
    msg->set_owns_handles(true);
    return NO_ERROR;
}

mx_status_t sys_channel_write(mx_handle_t handle_value, uint32_t options,
                              user_ptr<const void> _bytes, uint32_t num_bytes,
                              user_ptr<const mx_handle_t> _handles, uint32_t num_handles) {
    LTRACEF("handle %d bytes %p num_bytes %u handles %p num_handles %u options 0x%x\n",
            handle_value, _bytes.get(), num_bytes, _handles.get(), num_handles, options);

    if (options)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ChannelDispatcher> channel;
    mx_status_t result = up->GetDispatcherWithRights(handle_value, MX_RIGHT_WRITE, &channel);
    if (result != NO_ERROR)
        return result;


    mxtl::unique_ptr<MessagePacket> msg;
    result = MessagePacket::Create(num_bytes, num_handles, &msg);
    if (result != NO_ERROR)
        return result;

    if (num_bytes > 0u) {
        if (_bytes.copy_array_from_user(msg->mutable_data(), num_bytes) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    AllocChecker ac;
    mxtl::InlineArray<mx_handle_t, kChannelWriteHandlesInlineCount> handles(&ac, num_handles);
    if (!ac.check())
        return ERR_NO_MEMORY;
    if (num_handles > 0u) {
        result = msg_put_handles(up, msg.get(), handles.get(), _handles, num_handles,
                                 static_cast<Dispatcher*>(channel.get()));
        if (result)
            return result;
    }

    result = channel->Write(mxtl::move(msg));
    if (result != NO_ERROR) {
        // Write failed, put back the handles into this process.
        AutoLock lock(up->handle_table_lock());
        for (size_t ix = 0; ix != num_handles; ++ix) {
            up->UndoRemoveHandleLocked(handles[ix]);
        }
    }

    ktrace(TAG_CHANNEL_WRITE, (uint32_t)channel->get_koid(), num_bytes, num_handles, 0);
    return result;
}

mx_status_t sys_channel_call(mx_handle_t handle_value, uint32_t options,
                             mx_time_t deadline, user_ptr<const mx_channel_call_args_t> _args,
                             user_ptr<uint32_t> actual_bytes, user_ptr<uint32_t> actual_handles,
                             user_ptr<mx_status_t> read_status) {
    mx_channel_call_args_t args;


    if (_args.copy_from_user(&args) != NO_ERROR)
        return ERR_INVALID_ARGS;

    if (options)
        return ERR_INVALID_ARGS;

    uint32_t num_bytes = args.wr_num_bytes;
    uint32_t num_handles = args.wr_num_handles;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ChannelDispatcher> channel;
    mx_status_t result = up->GetDispatcherWithRights(handle_value, MX_RIGHT_WRITE, &channel);
    if (result != NO_ERROR)
        return result;

    // Prepare a MessagePacket for writing
    mxtl::unique_ptr<MessagePacket> msg;
    result = MessagePacket::Create(num_bytes, num_handles, &msg);
    if (result != NO_ERROR)
        return result;

    if (num_bytes > 0u) {
        if (make_user_ptr(args.wr_bytes).copy_array_from_user(msg->mutable_data(), num_bytes) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    AllocChecker ac;
    mxtl::InlineArray<mx_handle_t, kChannelWriteHandlesInlineCount> handles(&ac, num_handles);
    if (!ac.check())
        return ERR_NO_MEMORY;
    if (num_handles > 0u) {
        result = msg_put_handles(up, msg.get(), handles.get(),
                                 make_user_ptr<const mx_handle_t>(args.wr_handles), num_handles,
                                 static_cast<Dispatcher*>(channel.get()));
        if (result)
            return result;
    }

    // Write message and wait for reply, deadline, or cancelation
    bool return_handles = false;
    mxtl::unique_ptr<MessagePacket> reply;
    if ((result = channel->Call(mxtl::move(msg), deadline, &return_handles, &reply)) != NO_ERROR) {
        if (return_handles) {
            // Write phase failed:
            // 1. Put back the handles into this process.
            AutoLock lock(up->handle_table_lock());
            for (size_t ix = 0; ix != num_handles; ++ix) {
                up->UndoRemoveHandleLocked(handles[ix]);
            }
            // 2. Return error directly.  Note that if err is
            // ERR_SUSPEND_PENDING, the syscall glue will fully retry the Call
            // on resume.
            return result;
        }

        // Check if we have a pending suspend after the write phase and handle
        // it.  Then retry the wait-and-read after resuming.
        // TODO(teisenbe): Get rid of this check when we move retries to the
        // VDSO.
        while (result == ERR_INTERRUPTED_RETRY) {
            thread_process_pending_signals();
            result = channel->ResumeInterruptedCall(deadline, &reply);
        }

        // Timeout is always returned directly.
        if (result == ERR_TIMED_OUT) {
            return result;
        } else if (result != NO_ERROR) {
            // Read phase failed:
            // Return error via read_status
            goto read_failed;
        }
    }

    // Return inbound message to userspace
    num_bytes = reply->data_size();
    num_handles = reply->num_handles();

    if ((args.rd_num_bytes < num_bytes) || (args.rd_num_handles < num_handles)) {
        result = ERR_BUFFER_TOO_SMALL;
        goto read_failed;
    }

    if (actual_bytes.copy_to_user(num_bytes) != NO_ERROR) {
        result = ERR_INVALID_ARGS;
        goto read_failed;
    }
    if (actual_handles.copy_to_user(num_handles) != NO_ERROR) {
        result = ERR_INVALID_ARGS;
        goto read_failed;
    }

    if (num_bytes > 0u) {
        if (make_user_ptr(args.rd_bytes).copy_array_to_user(reply->data(), num_bytes) != NO_ERROR) {
            result = ERR_INVALID_ARGS;
            goto read_failed;
        }
    }

    if (num_handles > 0u) {
        msg_get_handles(up, reply.get(), make_user_ptr(args.rd_handles), num_handles);
    }
    return NO_ERROR;

read_failed:
    DEBUG_ASSERT(result != ERR_INTERRUPTED_RETRY);
    if (read_status)
        read_status.copy_to_user(result);
    return ERR_CALL_FAILED;
}
