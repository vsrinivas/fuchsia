// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <lib/counters.h>
#include <lib/ktrace.h>

#include <object/channel_dispatcher.h>
#include <object/handle.h>
#include <object/message_packet.h>
#include <object/process_dispatcher.h>
#include <zircon/syscalls/policy.h>
#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>

#include "priv.h"

using fbl::AutoLock;

#define LOCAL_TRACE 0

KCOUNTER(channel_msg_0_bytes,   "kernel.channel.bytes.0");
KCOUNTER(channel_msg_64_bytes,  "kernel.channel.bytes.64");
KCOUNTER(channel_msg_256_bytes, "kernel.channel.bytes.256");
KCOUNTER(channel_msg_1k_bytes,  "kernel.channel.bytes.1k");
KCOUNTER(channel_msg_4k_bytes,  "kernel.channel.bytes.4k");
KCOUNTER(channel_msg_16k_bytes, "kernel.channel.bytes.16k");
KCOUNTER(channel_msg_64k_bytes, "kernel.channel.bytes.64k");
KCOUNTER(channel_msg_received,  "kernel.channel.messages");

static void record_recv_msg_sz(uint32_t size) {
    kcounter_add(channel_msg_received, 1u);

    switch(size) {
        case     0          : kcounter_add(channel_msg_0_bytes, 1u);   break;
        case     1 ...    64: kcounter_add(channel_msg_64_bytes, 1u);  break;
        case    65 ...   256: kcounter_add(channel_msg_256_bytes, 1u); break;
        case   257 ...  1024: kcounter_add(channel_msg_1k_bytes, 1u);  break;
        case  1025 ...  4096: kcounter_add(channel_msg_4k_bytes, 1u);  break;
        case  4097 ... 16384: kcounter_add(channel_msg_16k_bytes, 1u); break;
        case 16385 ... 65536: kcounter_add(channel_msg_64k_bytes, 1u); break;
    }
}

zx_status_t sys_channel_create(uint32_t options,
                               user_out_handle* out0, user_out_handle* out1) {
    if (options != 0u)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t res = up->QueryPolicy(ZX_POL_NEW_CHANNEL);
    if (res != ZX_OK)
        return res;

    fbl::RefPtr<Dispatcher> mpd0, mpd1;
    zx_rights_t rights;
    zx_status_t result = ChannelDispatcher::Create(&mpd0, &mpd1, &rights);
    if (result != ZX_OK)
        return result;

    uint64_t id0 = mpd0->get_koid();
    uint64_t id1 = mpd1->get_koid();

    result = out0->make(fbl::move(mpd0), rights);
    if (result == ZX_OK)
        result = out1->make(fbl::move(mpd1), rights);
    if (result == ZX_OK)
        ktrace(TAG_CHANNEL_CREATE, (uint32_t)id0, (uint32_t)id1, options, 0);
    return result;
}

static void MapHandleToValue(
    ProcessDispatcher* up, const Handle* handle, uint32_t* out) {
    *out = up->MapHandleToValue(handle);
}

static void MapHandleToValue(
    ProcessDispatcher* up, const Handle* handle, zx_handle_info_t* out) {
    out->handle = up->MapHandleToValue(handle);
    out->type = handle->dispatcher()->get_type();
    out->rights = handle->rights();
    out->unused = 0;
}

template <typename HandleT>
static void msg_get_handles(ProcessDispatcher* up, MessagePacket* msg,
                            user_out_ptr<HandleT> handles, uint32_t num_handles) {
    Handle* const* handle_list = msg->handles();
    msg->set_owns_handles(false);

    HandleT hvs[kMaxMessageHandles];
    for (size_t i = 0; i < num_handles; ++i) {
        MapHandleToValue(up, handle_list[i], &hvs[i]);
    }

    handles.copy_array_to_user(hvs, num_handles);

    for (size_t i = 0; i < num_handles; ++i) {
        if (handle_list[i]->dispatcher()->has_state_tracker())
            handle_list[i]->dispatcher()->Cancel(handle_list[i]);
        HandleOwner handle(handle_list[i]);
        // TODO(ZX-969): This takes a lock per call. Consider doing these in a batch.
        up->AddHandle(fbl::move(handle));
    }
}

template <typename HandleInfoT>
static zx_status_t channel_read(zx_handle_t handle_value, uint32_t options,
                             user_out_ptr<void> bytes, user_out_ptr<HandleInfoT> handles,
                             uint32_t num_bytes, uint32_t num_handles,
                             user_out_ptr<uint32_t> actual_bytes,
                             user_out_ptr<uint32_t> actual_handles) {
    LTRACEF("handle %x bytes %p num_bytes %p handles %p num_handles %p",
            handle_value, bytes.get(), actual_bytes.get(), handles.get(), actual_handles.get());

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ChannelDispatcher> channel;
    zx_status_t result = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_READ, &channel);
    if (result != ZX_OK)
        return result;

    // Currently MAY_DISCARD is the only allowable option.
    if (options & ~ZX_CHANNEL_READ_MAY_DISCARD)
        return ZX_ERR_NOT_SUPPORTED;

    fbl::unique_ptr<MessagePacket> msg;
    result = channel->Read(&num_bytes, &num_handles, &msg,
                           options & ZX_CHANNEL_READ_MAY_DISCARD);
    if (result != ZX_OK && result != ZX_ERR_BUFFER_TOO_SMALL)
        return result;

    // On ZX_ERR_BUFFER_TOO_SMALL, Read() gives us the size of the next message (which remains
    // unconsumed, unless |options| has ZX_CHANNEL_READ_MAY_DISCARD set).
    if (actual_bytes) {
        zx_status_t status = actual_bytes.copy_to_user(num_bytes);
        if (status != ZX_OK)
            return status;
    }

    if (actual_handles) {
        zx_status_t status = actual_handles.copy_to_user(num_handles);
        if (status != ZX_OK)
            return status;
    }
    if (result == ZX_ERR_BUFFER_TOO_SMALL)
        return result;

    if (num_bytes > 0u) {
        if (msg->CopyDataTo(bytes) != ZX_OK)
            return ZX_ERR_INVALID_ARGS;
    }

    // The documented public API states that that writing to the handles buffer
    // must happen after writing to the data buffer.
    if (num_handles > 0u) {
        msg_get_handles(up, msg.get(), handles, num_handles);
    }

    record_recv_msg_sz(num_bytes);
    ktrace(TAG_CHANNEL_READ, (uint32_t)channel->get_koid(), num_bytes, num_handles, 0);
    return result;
}

zx_status_t sys_channel_read(zx_handle_t handle_value, uint32_t options,
                             user_out_ptr<void> bytes,
                             user_out_ptr<zx_handle_t> handle_info,
                             uint32_t num_bytes, uint32_t num_handles,
                             user_out_ptr<uint32_t> actual_bytes,
                             user_out_ptr<uint32_t> actual_handles) {
    return channel_read(handle_value, options,
        bytes, handle_info, num_bytes, num_handles, actual_bytes, actual_handles);
}

zx_status_t sys_channel_read_etc(zx_handle_t handle_value, uint32_t options,
                             user_out_ptr<void> bytes,
                             user_out_ptr<zx_handle_info_t> handle_info,
                             uint32_t num_bytes, uint32_t num_handles,
                             user_out_ptr<uint32_t> actual_bytes,
                             user_out_ptr<uint32_t> actual_handles) {
    return channel_read(handle_value, options,
        bytes, handle_info, num_bytes, num_handles, actual_bytes, actual_handles);
}

static zx_status_t channel_read_out(ProcessDispatcher* up,
                                    fbl::unique_ptr<MessagePacket> reply,
                                    zx_channel_call_args_t* args,
                                    user_out_ptr<uint32_t> actual_bytes,
                                    user_out_ptr<uint32_t> actual_handles) {
    uint32_t num_bytes = reply->data_size();
    uint32_t num_handles = reply->num_handles();

    if ((args->rd_num_bytes < num_bytes) || (args->rd_num_handles < num_handles)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    zx_status_t status = actual_bytes.copy_to_user(num_bytes);
    if (status != ZX_OK)
        return status;
    status = actual_handles.copy_to_user(num_handles);
    if (status != ZX_OK)
        return status;

    if (num_bytes > 0u) {
        if (reply->CopyDataTo(make_user_out_ptr(args->rd_bytes)) != ZX_OK) {
            return ZX_ERR_INVALID_ARGS;
        }
    }

    if (num_handles > 0u) {
        msg_get_handles(up, reply.get(), make_user_out_ptr(args->rd_handles), num_handles);
    }
    return ZX_OK;
}

// Handles generating the final results for call successes and read-half failures.
static zx_status_t channel_call_epilogue(ProcessDispatcher* up,
                                         fbl::unique_ptr<MessagePacket> reply,
                                         zx_channel_call_args_t* args,
                                         zx_status_t call_status,
                                         user_out_ptr<uint32_t> actual_bytes,
                                         user_out_ptr<uint32_t> actual_handles,
                                         user_out_ptr<zx_status_t> read_status) {
    // Timeout is always returned directly.
    if (call_status == ZX_ERR_TIMED_OUT) {
        return call_status;
    }

    auto bytes = reply? reply->data_size() : 0u;

    if (call_status == ZX_OK) {
        call_status = channel_read_out(up, fbl::move(reply), args, actual_bytes, actual_handles);
    }

    if (call_status != ZX_OK) {
        if (read_status) {
            read_status.copy_to_user(call_status);
        }
        return ZX_ERR_CALL_FAILED;
    }

    record_recv_msg_sz(bytes);
    return ZX_OK;
}

static zx_status_t msg_put_handles(ProcessDispatcher* up, MessagePacket* msg, zx_handle_t* handles,
                                   user_in_ptr<const zx_handle_t> user_handles, uint32_t num_user_handles,
                                   Dispatcher* channel) {

    if (user_handles.copy_array_from_user(handles, num_user_handles) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    {
        // Loop twice, first we collect and validate handles, the second pass
        // we remove them from this process.
        AutoLock lock(up->handle_table_lock());

        for (size_t ix = 0; ix != num_user_handles; ++ix) {
            auto handle = up->GetHandleLocked(handles[ix]);
            if (!handle)
                return ZX_ERR_BAD_HANDLE;

            if (handle->dispatcher().get() == channel) {
                // You may not write a channel endpoint handle
                // into that channel endpoint
                return ZX_ERR_NOT_SUPPORTED;
            }

            if (!handle->HasRights(ZX_RIGHT_TRANSFER))
                return ZX_ERR_ACCESS_DENIED;

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
                // TODO(ZX-968): more specific error?
                return ZX_ERR_INVALID_ARGS;
            }
        }
    }

    // On success, the MessagePacket owns the handles.
    msg->set_owns_handles(true);
    return ZX_OK;
}

zx_status_t sys_channel_write(zx_handle_t handle_value, uint32_t options,
                              user_in_ptr<const void> user_bytes, uint32_t num_bytes,
                              user_in_ptr<const zx_handle_t> user_handles, uint32_t num_handles) {
    LTRACEF("handle %x bytes %p num_bytes %u handles %p num_handles %u options 0x%x\n",
            handle_value, user_bytes.get(), num_bytes, user_handles.get(), num_handles, options);

    if (options)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ChannelDispatcher> channel;
    zx_status_t result = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_WRITE, &channel);
    if (result != ZX_OK)
        return result;


    fbl::unique_ptr<MessagePacket> msg;
    result = MessagePacket::Create(user_bytes, num_bytes, num_handles, &msg);
    if (result != ZX_OK)
        return result;

    zx_handle_t handles[kMaxMessageHandles];
    if (num_handles > 0u) {
        result = msg_put_handles(up, msg.get(), handles, user_handles, num_handles,
                                 static_cast<Dispatcher*>(channel.get()));
        if (result)
            return result;
    }

    result = channel->Write(fbl::move(msg));
    if (result != ZX_OK) {
        // Write failed, put back the handles into this process.
        AutoLock lock(up->handle_table_lock());
        for (size_t ix = 0; ix != num_handles; ++ix) {
            up->UndoRemoveHandleLocked(handles[ix]);
        }
        return result;
    }

    ktrace(TAG_CHANNEL_WRITE, (uint32_t)channel->get_koid(), num_bytes, num_handles, 0);
    return ZX_OK;
}

zx_status_t sys_channel_call_noretry(zx_handle_t handle_value, uint32_t options,
                                     zx_time_t deadline,
                                     user_in_ptr<const zx_channel_call_args_t> user_args,
                                     user_out_ptr<uint32_t> actual_bytes,
                                     user_out_ptr<uint32_t> actual_handles,
                                     user_out_ptr<zx_status_t> read_status) {
    zx_channel_call_args_t args;

    zx_status_t status = user_args.copy_from_user(&args);
    if (status != ZX_OK)
        return status;

    if (options)
        return ZX_ERR_INVALID_ARGS;

    uint32_t num_bytes = args.wr_num_bytes;
    uint32_t num_handles = args.wr_num_handles;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ChannelDispatcher> channel;
    zx_status_t result =
        up->GetDispatcherWithRights(handle_value, ZX_RIGHT_WRITE | ZX_RIGHT_READ, &channel);
    if (result != ZX_OK)
        return result;

    // Prepare a MessagePacket for writing
    fbl::unique_ptr<MessagePacket> msg;
    result = MessagePacket::Create(make_user_in_ptr(args.wr_bytes),
                                   num_bytes, num_handles, &msg);
    if (result != ZX_OK)
        return result;

    zx_handle_t handles[kMaxMessageHandles];
    if (num_handles > 0u) {
        result = msg_put_handles(up, msg.get(), handles,
                                 make_user_in_ptr(args.wr_handles), num_handles,
                                 static_cast<Dispatcher*>(channel.get()));
        if (result)
            return result;
    }

    // TODO(ZX-970): ktrace channel calls; maybe two traces, maybe with txid.

    // Write message and wait for reply, deadline, or cancelation
    bool return_handles = false;
    fbl::unique_ptr<MessagePacket> reply;
    if ((result = channel->Call(fbl::move(msg), deadline, &return_handles, &reply)) != ZX_OK) {
        if (return_handles) {
            // Write phase failed:
            // 1. Put back the handles into this process.
            AutoLock lock(up->handle_table_lock());
            for (size_t ix = 0; ix != num_handles; ++ix) {
                up->UndoRemoveHandleLocked(handles[ix]);
            }
            // 2. Return error directly.  Note that the write phase cannot fail
            // with ZX_ERR_INTERNAL_INTR_RETRY.
            DEBUG_ASSERT(result != ZX_ERR_INTERNAL_INTR_RETRY);
            return result;
        }
    }
    return channel_call_epilogue(up, fbl::move(reply), &args, result,
                                 actual_bytes, actual_handles, read_status);
}

zx_status_t sys_channel_call_finish(zx_time_t deadline,
                                    user_in_ptr<const zx_channel_call_args_t> user_args,
                                    user_out_ptr<uint32_t> actual_bytes,
                                    user_out_ptr<uint32_t> actual_handles,
                                    user_out_ptr<zx_status_t> read_status) {

    zx_channel_call_args_t args;
    zx_status_t status = user_args.copy_from_user(&args);
    if (status != ZX_OK)
        return status;

    auto up = ProcessDispatcher::GetCurrent();

    auto waiter = ThreadDispatcher::GetCurrent()->GetMessageWaiter();
    fbl::RefPtr<ChannelDispatcher> channel = waiter->get_channel();
    if (!channel)
        return ZX_ERR_BAD_STATE;

    fbl::unique_ptr<MessagePacket> reply;
    zx_status_t result = channel->ResumeInterruptedCall(
        waiter, deadline, &reply);
    return channel_call_epilogue(up, fbl::move(reply), &args, result,
                                 actual_bytes, actual_handles, read_status);

}
