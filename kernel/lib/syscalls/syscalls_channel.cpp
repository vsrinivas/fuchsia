// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <lib/ktrace.h>

#include <magenta/syscalls/policy.h>
#include <object/channel_dispatcher.h>
#include <object/handle_owner.h>
#include <object/handles.h>
#include <object/message_packet.h>
#include <object/process_dispatcher.h>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>

#include "syscalls_priv.h"

using fbl::AutoLock;

#define LOCAL_TRACE 0

mx_status_t sys_channel_create(
    uint32_t options, user_ptr<mx_handle_t> out0, user_ptr<mx_handle_t> out1) {
    LTRACEF("out_handles %p,%p\n", out0.get(), out1.get());

    if (options != 0u)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mx_status_t res = up->QueryPolicy(MX_POL_NEW_CHANNEL);
    if (res != MX_OK)
        return res;

    fbl::RefPtr<Dispatcher> mpd0, mpd1;
    mx_rights_t rights;
    mx_status_t result = ChannelDispatcher::Create(&mpd0, &mpd1, &rights);
    if (result != MX_OK)
        return result;

    uint64_t id0 = mpd0->get_koid();
    uint64_t id1 = mpd1->get_koid();

    HandleOwner h0(MakeHandle(fbl::move(mpd0), rights));
    if (!h0)
        return MX_ERR_NO_MEMORY;

    HandleOwner h1(MakeHandle(fbl::move(mpd1), rights));
    if (!h1)
        return MX_ERR_NO_MEMORY;

    if (out0.copy_to_user(up->MapHandleToValue(h0)) != MX_OK)
        return MX_ERR_INVALID_ARGS;
    if (out1.copy_to_user(up->MapHandleToValue(h1)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(fbl::move(h0));
    up->AddHandle(fbl::move(h1));

    ktrace(TAG_CHANNEL_CREATE, (uint32_t)id0, (uint32_t)id1, options, 0);
    return MX_OK;
}

static void msg_get_handles(ProcessDispatcher* up, MessagePacket* msg,
                            user_ptr<mx_handle_t> handles, uint32_t num_handles) {
    Handle* const* handle_list = msg->handles();
    msg->set_owns_handles(false);

    mx_handle_t hvs[kMaxMessageHandles];
    for (size_t i = 0; i < num_handles; ++i) {
        hvs[i] = up->MapHandleToValue(handle_list[i]);
    }
    handles.copy_array_to_user(hvs, num_handles);

    for (size_t i = 0; i < num_handles; ++i) {
        if (handle_list[i]->dispatcher()->get_state_tracker())
            handle_list[i]->dispatcher()->get_state_tracker()->Cancel(handle_list[i]);
        HandleOwner handle(handle_list[i]);
        // TODO(MG-969): This takes a lock per call. Consider doing these in a batch.
        up->AddHandle(fbl::move(handle));
    }
}

mx_status_t sys_channel_read(mx_handle_t handle_value, uint32_t options,
                             user_ptr<void> bytes, user_ptr<mx_handle_t> handles,
                             uint32_t num_bytes, uint32_t num_handles,
                             user_ptr<uint32_t> actual_bytes, user_ptr<uint32_t> actual_handles) {
    LTRACEF("handle %x bytes %p num_bytes %p handles %p num_handles %p",
            handle_value, bytes.get(), actual_bytes.get(), handles.get(), actual_handles.get());

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ChannelDispatcher> channel;
    mx_status_t result = up->GetDispatcherWithRights(handle_value, MX_RIGHT_READ, &channel);
    if (result != MX_OK)
        return result;

    // Currently MAY_DISCARD is the only allowable option.
    if (options & ~MX_CHANNEL_READ_MAY_DISCARD)
        return MX_ERR_NOT_SUPPORTED;

    fbl::unique_ptr<MessagePacket> msg;
    result = channel->Read(&num_bytes, &num_handles, &msg,
                           options & MX_CHANNEL_READ_MAY_DISCARD);
    if (result != MX_OK && result != MX_ERR_BUFFER_TOO_SMALL)
        return result;

    // On MX_ERR_BUFFER_TOO_SMALL, Read() gives us the size of the next message (which remains
    // unconsumed, unless |options| has MX_CHANNEL_READ_MAY_DISCARD set).
    if (actual_bytes) {
        if (actual_bytes.copy_to_user(num_bytes) != MX_OK)
            return MX_ERR_INVALID_ARGS;
    }
    if (actual_handles) {
        if (actual_handles.copy_to_user(num_handles) != MX_OK)
            return MX_ERR_INVALID_ARGS;
    }
    if (result == MX_ERR_BUFFER_TOO_SMALL)
        return result;

    if (num_bytes > 0u) {
        if (msg->CopyDataTo(bytes) != MX_OK)
            return MX_ERR_INVALID_ARGS;
    }

    // The documented public API states that that writing to the handles buffer
    // must happen after writing to the data buffer.
    if (num_handles > 0u) {
        msg_get_handles(up, msg.get(), handles, num_handles);
    }

    ktrace(TAG_CHANNEL_READ, (uint32_t)channel->get_koid(), num_bytes, num_handles, 0);
    return result;
}

static mx_status_t channel_read_out(ProcessDispatcher* up,
                                    fbl::unique_ptr<MessagePacket> reply,
                                    mx_channel_call_args_t* args,
                                    user_ptr<uint32_t> actual_bytes,
                                    user_ptr<uint32_t> actual_handles) {
    uint32_t num_bytes = reply->data_size();
    uint32_t num_handles = reply->num_handles();

    if ((args->rd_num_bytes < num_bytes) || (args->rd_num_handles < num_handles)) {
        return MX_ERR_BUFFER_TOO_SMALL;
    }

    if (actual_bytes.copy_to_user(num_bytes) != MX_OK) {
        return MX_ERR_INVALID_ARGS;
    }
    if (actual_handles.copy_to_user(num_handles) != MX_OK) {
        return MX_ERR_INVALID_ARGS;
    }

    if (num_bytes > 0u) {
        if (reply->CopyDataTo(make_user_ptr(args->rd_bytes)) != MX_OK) {
            return MX_ERR_INVALID_ARGS;
        }
    }

    if (num_handles > 0u) {
        msg_get_handles(up, reply.get(), make_user_ptr(args->rd_handles), num_handles);
    }
    return MX_OK;
}

// Handles generating the final results for call successes and read-half failures.
static mx_status_t channel_call_epilogue(ProcessDispatcher* up,
                                         fbl::unique_ptr<MessagePacket> reply,
                                         mx_channel_call_args_t* args,
                                         mx_status_t call_status,
                                         user_ptr<uint32_t> actual_bytes,
                                         user_ptr<uint32_t> actual_handles,
                                         user_ptr<mx_status_t> read_status) {
    // Timeout is always returned directly.
    if (call_status == MX_ERR_TIMED_OUT) {
        return call_status;
    }

    if (call_status == MX_OK) {
        call_status = channel_read_out(up, fbl::move(reply), args, actual_bytes, actual_handles);
    }

    if (call_status != MX_OK) {
        if (read_status) {
            read_status.copy_to_user(call_status);
        }
        return MX_ERR_CALL_FAILED;
    }

    return MX_OK;
}

static mx_status_t msg_put_handles(ProcessDispatcher* up, MessagePacket* msg, mx_handle_t* handles,
                                   user_ptr<const mx_handle_t> user_handles, uint32_t num_user_handles,
                                   Dispatcher* channel) {

    if (user_handles.copy_array_from_user(handles, num_user_handles) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    {
        // Loop twice, first we collect and validate handles, the second pass
        // we remove them from this process.
        AutoLock lock(up->handle_table_lock());

        for (size_t ix = 0; ix != num_user_handles; ++ix) {
            auto handle = up->GetHandleLocked(handles[ix]);
            if (!handle)
                return MX_ERR_BAD_HANDLE;

            if (handle->dispatcher().get() == channel) {
                // You may not write a channel endpoint handle
                // into that channel endpoint
                return MX_ERR_NOT_SUPPORTED;
            }

            if (!handle->HasRights(MX_RIGHT_TRANSFER))
                return MX_ERR_ACCESS_DENIED;

            msg->mutable_handles()[ix] = handle;
        }

        for (size_t ix = 0; ix != num_user_handles; ++ix) {
            auto handle = up->RemoveHandleLocked(handles[ix]).release();
            // Passing duplicate handles is not allowed.
            // If we've already seen this handle flag an error.
            if (!handle) {
                // Put back the handles we've already removed.
                for (size_t idx = 0; idx < ix; ++idx) {
                    up->UndoRemoveHandleLocked(handles[idx]);
                }
                // TODO(MG-968): more specific error?
                return MX_ERR_INVALID_ARGS;
            }
        }
    }

    // On success, the MessagePacket owns the handles.
    msg->set_owns_handles(true);
    return MX_OK;
}

mx_status_t sys_channel_write(mx_handle_t handle_value, uint32_t options,
                              user_ptr<const void> user_bytes, uint32_t num_bytes,
                              user_ptr<const mx_handle_t> user_handles, uint32_t num_handles) {
    LTRACEF("handle %x bytes %p num_bytes %u handles %p num_handles %u options 0x%x\n",
            handle_value, user_bytes.get(), num_bytes, user_handles.get(), num_handles, options);

    if (options)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ChannelDispatcher> channel;
    mx_status_t result = up->GetDispatcherWithRights(handle_value, MX_RIGHT_WRITE, &channel);
    if (result != MX_OK)
        return result;


    fbl::unique_ptr<MessagePacket> msg;
    result = MessagePacket::Create(user_bytes, num_bytes, num_handles, &msg);
    if (result != MX_OK)
        return result;

    mx_handle_t handles[kMaxMessageHandles];
    if (num_handles > 0u) {
        result = msg_put_handles(up, msg.get(), handles, user_handles, num_handles,
                                 static_cast<Dispatcher*>(channel.get()));
        if (result)
            return result;
    }

    result = channel->Write(fbl::move(msg));
    if (result != MX_OK) {
        // Write failed, put back the handles into this process.
        AutoLock lock(up->handle_table_lock());
        for (size_t ix = 0; ix != num_handles; ++ix) {
            up->UndoRemoveHandleLocked(handles[ix]);
        }
        return result;
    }

    ktrace(TAG_CHANNEL_WRITE, (uint32_t)channel->get_koid(), num_bytes, num_handles, 0);
    return MX_OK;
}

mx_status_t sys_channel_call_noretry(mx_handle_t handle_value, uint32_t options,
                                     mx_time_t deadline,
                                     user_ptr<const mx_channel_call_args_t> user_args,
                                     user_ptr<uint32_t> actual_bytes,
                                     user_ptr<uint32_t> actual_handles,
                                     user_ptr<mx_status_t> read_status) {
    mx_channel_call_args_t args;

    if (user_args.copy_from_user(&args) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    if (options)
        return MX_ERR_INVALID_ARGS;

    uint32_t num_bytes = args.wr_num_bytes;
    uint32_t num_handles = args.wr_num_handles;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ChannelDispatcher> channel;
    mx_status_t result =
        up->GetDispatcherWithRights(handle_value, MX_RIGHT_WRITE | MX_RIGHT_READ, &channel);
    if (result != MX_OK)
        return result;

    // Prepare a MessagePacket for writing
    fbl::unique_ptr<MessagePacket> msg;
    result = MessagePacket::Create(make_user_ptr<const void>(args.wr_bytes),
                                   num_bytes, num_handles, &msg);
    if (result != MX_OK)
        return result;

    mx_handle_t handles[kMaxMessageHandles];
    if (num_handles > 0u) {
        result = msg_put_handles(up, msg.get(), handles,
                                 make_user_ptr<const mx_handle_t>(args.wr_handles), num_handles,
                                 static_cast<Dispatcher*>(channel.get()));
        if (result)
            return result;
    }

    // TODO(MG-970): ktrace channel calls; maybe two traces, maybe with txid.

    // Write message and wait for reply, deadline, or cancelation
    bool return_handles = false;
    fbl::unique_ptr<MessagePacket> reply;
    if ((result = channel->Call(fbl::move(msg), deadline, &return_handles, &reply)) != MX_OK) {
        if (return_handles) {
            // Write phase failed:
            // 1. Put back the handles into this process.
            AutoLock lock(up->handle_table_lock());
            for (size_t ix = 0; ix != num_handles; ++ix) {
                up->UndoRemoveHandleLocked(handles[ix]);
            }
            // 2. Return error directly.  Note that the write phase cannot fail
            // with MX_ERR_INTERNAL_INTR_RETRY.
            DEBUG_ASSERT(result != MX_ERR_INTERNAL_INTR_RETRY);
            return result;
        }
    }
    return channel_call_epilogue(up, fbl::move(reply), &args, result,
                                 actual_bytes, actual_handles, read_status);
}

mx_status_t sys_channel_call_finish(mx_time_t deadline,
                                    user_ptr<const mx_channel_call_args_t> user_args,
                                    user_ptr<uint32_t> actual_bytes,
                                    user_ptr<uint32_t> actual_handles,
                                    user_ptr<mx_status_t> read_status) {

    mx_channel_call_args_t args;
    if (user_args.copy_from_user(&args) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    auto waiter = ThreadDispatcher::GetCurrent()->GetMessageWaiter();
    fbl::RefPtr<ChannelDispatcher> channel = waiter->get_channel();
    if (!channel)
        return MX_ERR_BAD_STATE;

    fbl::unique_ptr<MessagePacket> reply;
    mx_status_t result = channel->ResumeInterruptedCall(
        waiter, deadline, &reply);
    return channel_call_epilogue(up, fbl::move(reply), &args, result,
                                 actual_bytes, actual_handles, read_status);

}
