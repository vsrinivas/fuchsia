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
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

static mx_status_t channel_call_epilogue(ProcessDispatcher* up,
                                         mxtl::unique_ptr<MessagePacket> reply,
                                         mx_channel_call_args_t* args,
                                         mx_status_t call_status,
                                         user_ptr<uint32_t> actual_bytes,
                                         user_ptr<uint32_t> actual_handles,
                                         user_ptr<mx_status_t> read_status);

mx_status_t sys_channel_create(
    uint32_t options, user_ptr<mx_handle_t> _out0, user_ptr<mx_handle_t> _out1) {
    LTRACEF("out_handles %p,%p\n", _out0.get(), _out1.get());

    if (options != 0u)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mx_status_t res = up->QueryPolicy(MX_POL_NEW_CHANNEL);
    if (res < 0)
        return res;

    mxtl::RefPtr<Dispatcher> mpd0, mpd1;
    mx_rights_t rights;
    status_t result = ChannelDispatcher::Create(options, &mpd0, &mpd1, &rights);
    if (result != MX_OK)
        return result;

    uint64_t id0 = mpd0->get_koid();
    uint64_t id1 = mpd1->get_koid();

    HandleOwner h0(MakeHandle(mxtl::move(mpd0), rights));
    if (!h0)
        return MX_ERR_NO_MEMORY;

    HandleOwner h1(MakeHandle(mxtl::move(mpd1), rights));
    if (!h1)
        return MX_ERR_NO_MEMORY;

    if (_out0.copy_to_user(up->MapHandleToValue(h0)) != MX_OK)
        return MX_ERR_INVALID_ARGS;
    if (_out1.copy_to_user(up->MapHandleToValue(h1)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(h0));
    up->AddHandle(mxtl::move(h1));

    ktrace(TAG_CHANNEL_CREATE, (uint32_t)id0, (uint32_t)id1, options, 0);
    return MX_OK;
}

static void msg_get_handles(ProcessDispatcher* up, MessagePacket* msg,
                            user_ptr<mx_handle_t> _handles, uint32_t num_handles) {
    Handle* const* handle_list = msg->handles();
    msg->set_owns_handles(false);

    mx_handle_t hvs[kMaxMessageHandles];
    for (size_t i = 0; i < num_handles; ++i) {
        hvs[i] = up->MapHandleToValue(handle_list[i]);
    }
    _handles.copy_array_to_user(hvs, num_handles);

    for (size_t i = 0; i < num_handles; ++i) {
        if (handle_list[i]->dispatcher()->get_state_tracker())
            handle_list[i]->dispatcher()->get_state_tracker()->Cancel(handle_list[i]);
        HandleOwner handle(handle_list[i]);
        //TODO: This takes a lock per call. Consider doing these in a batch.
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
    if (result != MX_OK)
        return result;

    // Currently MAY_DISCARD is the only allowable option.
    if (options & ~MX_CHANNEL_READ_MAY_DISCARD)
        return MX_ERR_NOT_SUPPORTED;

    mxtl::unique_ptr<MessagePacket> msg;
    result = channel->Read(&num_bytes, &num_handles, &msg,
                           options & MX_CHANNEL_READ_MAY_DISCARD);
    if (result != MX_OK && result != MX_ERR_BUFFER_TOO_SMALL)
        return result;

    // On MX_ERR_BUFFER_TOO_SMALL, Read() gives us the size of the next message (which remains
    // unconsumed, unless |options| has MX_CHANNEL_READ_MAY_DISCARD set).
    if (_num_bytes) {
        if (_num_bytes.copy_to_user(num_bytes) != MX_OK)
            return MX_ERR_INVALID_ARGS;
    }
    if (_num_handles) {
        if (_num_handles.copy_to_user(num_handles) != MX_OK)
            return MX_ERR_INVALID_ARGS;
    }
    if (result == MX_ERR_BUFFER_TOO_SMALL)
        return result;

    if (num_bytes > 0u) {
        if (_bytes.copy_array_to_user(msg->data(), num_bytes) != MX_OK)
            return MX_ERR_INVALID_ARGS;
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

    if (_handles.copy_array_from_user(handles, num_handles) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    {
        // Loop twice, first we collect and validate handles, the second pass
        // we remove them from this process.
        AutoLock lock(up->handle_table_lock());

        for (size_t ix = 0; ix != num_handles; ++ix) {
            auto handle = up->GetHandleLocked(handles[ix]);
            if (!handle)
                return MX_ERR_BAD_HANDLE;

            if (handle->dispatcher().get() == channel) {
                // You may not write a channel endpoint handle
                // into that channel endpoint
                return MX_ERR_NOT_SUPPORTED;
            }

            if (!magenta_rights_check(handle, MX_RIGHT_TRANSFER))
                return MX_ERR_ACCESS_DENIED;

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
                return MX_ERR_INVALID_ARGS;
            }
        }
    }

    // On success, the MessagePacket owns the handles.
    msg->set_owns_handles(true);
    return MX_OK;
}

mx_status_t sys_channel_write(mx_handle_t handle_value, uint32_t options,
                              user_ptr<const void> _bytes, uint32_t num_bytes,
                              user_ptr<const mx_handle_t> _handles, uint32_t num_handles) {
    LTRACEF("handle %d bytes %p num_bytes %u handles %p num_handles %u options 0x%x\n",
            handle_value, _bytes.get(), num_bytes, _handles.get(), num_handles, options);

    if (options)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ChannelDispatcher> channel;
    mx_status_t result = up->GetDispatcherWithRights(handle_value, MX_RIGHT_WRITE, &channel);
    if (result != MX_OK)
        return result;


    mxtl::unique_ptr<MessagePacket> msg;
    result = MessagePacket::Create(num_bytes, num_handles, &msg);
    if (result != MX_OK)
        return result;

    if (num_bytes > 0u) {
        if (_bytes.copy_array_from_user(msg->mutable_data(), num_bytes) != MX_OK)
            return MX_ERR_INVALID_ARGS;
    }

    mx_handle_t handles[kMaxMessageHandles];
    if (num_handles > 0u) {
        result = msg_put_handles(up, msg.get(), handles, _handles, num_handles,
                                 static_cast<Dispatcher*>(channel.get()));
        if (result)
            return result;
    }

    result = channel->Write(mxtl::move(msg));
    if (result != MX_OK) {
        // Write failed, put back the handles into this process.
        AutoLock lock(up->handle_table_lock());
        for (size_t ix = 0; ix != num_handles; ++ix) {
            up->UndoRemoveHandleLocked(handles[ix]);
        }
    }

    ktrace(TAG_CHANNEL_WRITE, (uint32_t)channel->get_koid(), num_bytes, num_handles, 0);
    return result;
}

mx_status_t sys_channel_call_noretry(mx_handle_t handle_value, uint32_t options,
                                     mx_time_t deadline,
                                     user_ptr<const mx_channel_call_args_t> _args,
                                     user_ptr<uint32_t> actual_bytes,
                                     user_ptr<uint32_t> actual_handles,
                                     user_ptr<mx_status_t> read_status) {
    mx_channel_call_args_t args;

    if (_args.copy_from_user(&args) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    if (options)
        return MX_ERR_INVALID_ARGS;

    uint32_t num_bytes = args.wr_num_bytes;
    uint32_t num_handles = args.wr_num_handles;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ChannelDispatcher> channel;
    mx_status_t result = up->GetDispatcherWithRights(handle_value, MX_RIGHT_WRITE, &channel);
    if (result != MX_OK)
        return result;

    // Prepare a MessagePacket for writing
    mxtl::unique_ptr<MessagePacket> msg;
    result = MessagePacket::Create(num_bytes, num_handles, &msg);
    if (result != MX_OK)
        return result;

    if (num_bytes > 0u) {
        if (make_user_ptr(args.wr_bytes).copy_array_from_user(msg->mutable_data(), num_bytes) != MX_OK)
            return MX_ERR_INVALID_ARGS;
    }

    mx_handle_t handles[kMaxMessageHandles];
    if (num_handles > 0u) {
        result = msg_put_handles(up, msg.get(), handles,
                                 make_user_ptr<const mx_handle_t>(args.wr_handles), num_handles,
                                 static_cast<Dispatcher*>(channel.get()));
        if (result)
            return result;
    }

    // Write message and wait for reply, deadline, or cancelation
    bool return_handles = false;
    mxtl::unique_ptr<MessagePacket> reply;
    if ((result = channel->Call(mxtl::move(msg), deadline, &return_handles, &reply)) != MX_OK) {
        if (return_handles) {
            // Write phase failed:
            // 1. Put back the handles into this process.
            AutoLock lock(up->handle_table_lock());
            for (size_t ix = 0; ix != num_handles; ++ix) {
                up->UndoRemoveHandleLocked(handles[ix]);
            }
            // 2. Return error directly.  Note that the write phase cannot fail
            // with MX_ERR_INTERRUPTED_RETRY.
            DEBUG_ASSERT(result != MX_ERR_INTERRUPTED_RETRY);
            return result;
        }
    }
    return channel_call_epilogue(up, mxtl::move(reply), &args, result,
                                 actual_bytes, actual_handles, read_status);
}

mx_status_t sys_channel_call_finish(mx_handle_t handle_value, mx_time_t deadline,
                                    user_ptr<const mx_channel_call_args_t> _args,
                                    user_ptr<uint32_t> actual_bytes,
                                    user_ptr<uint32_t> actual_handles,
                                    user_ptr<mx_status_t> read_status) {

    mx_channel_call_args_t args;
    if (_args.copy_from_user(&args) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ChannelDispatcher> channel;
    mx_status_t result = up->GetDispatcherWithRights(handle_value, MX_RIGHT_WRITE, &channel);
    if (result != MX_OK)
        return result;

    mxtl::unique_ptr<MessagePacket> reply;
    result = channel->ResumeInterruptedCall(deadline, &reply);
    return channel_call_epilogue(up, mxtl::move(reply), &args, result,
                                 actual_bytes, actual_handles, read_status);

}

// Handles generating the final results for call successes and read-half failures.
mx_status_t channel_call_epilogue(ProcessDispatcher* up,
                                  mxtl::unique_ptr<MessagePacket> reply,
                                  mx_channel_call_args_t* args,
                                  mx_status_t call_status,
                                  user_ptr<uint32_t> actual_bytes,
                                  user_ptr<uint32_t> actual_handles,
                                  user_ptr<mx_status_t> read_status) {

    uint32_t num_bytes;
    uint32_t num_handles;

    // Timeout is always returned directly.
    if (call_status == MX_ERR_TIMED_OUT) {
        return call_status;
    } else if (call_status != MX_OK) {
        goto read_failed;
    }

    // Return inbound message to userspace
    num_bytes = reply->data_size();
    num_handles = reply->num_handles();

    if ((args->rd_num_bytes < num_bytes) || (args->rd_num_handles < num_handles)) {
        call_status = MX_ERR_BUFFER_TOO_SMALL;
        goto read_failed;
    }

    if (actual_bytes.copy_to_user(num_bytes) != MX_OK) {
        call_status = MX_ERR_INVALID_ARGS;
        goto read_failed;
    }
    if (actual_handles.copy_to_user(num_handles) != MX_OK) {
        call_status = MX_ERR_INVALID_ARGS;
        goto read_failed;
    }

    if (num_bytes > 0u) {
        if (make_user_ptr(args->rd_bytes).copy_array_to_user(reply->data(), num_bytes) != MX_OK) {
            call_status = MX_ERR_INVALID_ARGS;
            goto read_failed;
        }
    }

    if (num_handles > 0u) {
        msg_get_handles(up, reply.get(), make_user_ptr(args->rd_handles), num_handles);
    }
    return MX_OK;

read_failed:
    if (read_status)
        read_status.copy_to_user(call_status);
    return MX_ERR_CALL_FAILED;
}
