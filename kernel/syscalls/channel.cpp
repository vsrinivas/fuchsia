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
    kcounter_add(channel_msg_received, 1);

    switch(size) {
        case     0          : kcounter_add(channel_msg_0_bytes, 1);   break;
        case     1 ...    64: kcounter_add(channel_msg_64_bytes, 1);  break;
        case    65 ...   256: kcounter_add(channel_msg_256_bytes, 1); break;
        case   257 ...  1024: kcounter_add(channel_msg_1k_bytes, 1);  break;
        case  1025 ...  4096: kcounter_add(channel_msg_4k_bytes, 1);  break;
        case  4097 ... 16384: kcounter_add(channel_msg_16k_bytes, 1); break;
        case 16385 ... 65536: kcounter_add(channel_msg_64k_bytes, 1); break;
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

static zx_status_t channel_call_epilogue(ProcessDispatcher* up,
                                         fbl::unique_ptr<MessagePacket> reply,
                                         zx_channel_call_args_t* args,
                                         user_out_ptr<uint32_t> actual_bytes,
                                         user_out_ptr<uint32_t> actual_handles) {
    auto bytes = reply? reply->data_size() : 0u;
    zx_status_t status = channel_read_out(up, fbl::move(reply), args, actual_bytes, actual_handles);
    if (status != ZX_OK)
        return status;
    record_recv_msg_sz(bytes);
    return ZX_OK;
}

static zx_status_t msg_put_handles(ProcessDispatcher* up, MessagePacket* msg,
                                   user_in_ptr<const zx_handle_t> user_handles,
                                   uint32_t num_handles,
                                   Dispatcher* channel) {

    zx_handle_t handles[kMaxMessageHandles];
    if (user_handles.copy_array_from_user(handles, num_handles) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    zx_status_t status = ZX_OK;

    {
        AutoLock lock(up->handle_table_lock());

        for (size_t ix = 0; ix != num_handles; ++ix) {
            auto handle = up->RemoveHandleLocked(handles[ix]).release();

            if (status == ZX_OK) {
                if (!handle) {
                    status = ZX_ERR_BAD_HANDLE;
                } else {
                    // You may not write a channel endpoint handle into that channel
                    // endpoint.
                    if (handle->dispatcher().get() == channel)
                        status = ZX_ERR_NOT_SUPPORTED;

                    if (!handle->HasRights(ZX_RIGHT_TRANSFER))
                        status = ZX_ERR_ACCESS_DENIED;
                }
            }

            msg->mutable_handles()[ix] = handle;
        }
    }

    msg->set_owns_handles(true);
    return status;
}

zx_status_t sys_channel_write(zx_handle_t handle_value, uint32_t options,
                              user_in_ptr<const void> user_bytes, uint32_t num_bytes,
                              user_in_ptr<const zx_handle_t> user_handles, uint32_t num_handles) {
    LTRACEF("handle %x bytes %p num_bytes %u handles %p num_handles %u options 0x%x\n",
            handle_value, user_bytes.get(), num_bytes, user_handles.get(), num_handles, options);

    auto up = ProcessDispatcher::GetCurrent();

    if (options != 0u) {
        up->RemoveHandles(user_handles, num_handles);
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<ChannelDispatcher> channel;
    zx_status_t status = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_WRITE, &channel);
    if (status != ZX_OK) {
        up->RemoveHandles(user_handles, num_handles);
        return status;
    }

    fbl::unique_ptr<MessagePacket> msg;
    status = MessagePacket::Create(user_bytes, num_bytes, num_handles, &msg);
    if (status != ZX_OK) {
        up->RemoveHandles(user_handles, num_handles);
        return status;
    }

    if (num_handles > 0u) {
        status = msg_put_handles(up, msg.get(), user_handles, num_handles,
                                 static_cast<Dispatcher*>(channel.get()));
        if (status != ZX_OK)
            return status;
    }

    status = channel->Write(fbl::move(msg));
    if (status != ZX_OK)
        return status;

    ktrace(TAG_CHANNEL_WRITE, (uint32_t)channel->get_koid(), num_bytes, num_handles, 0);
    return ZX_OK;
}

zx_status_t sys_channel_call_noretry(zx_handle_t handle_value, uint32_t options,
                                     zx_time_t deadline,
                                     user_in_ptr<const zx_channel_call_args_t> user_args,
                                     user_out_ptr<uint32_t> actual_bytes,
                                     user_out_ptr<uint32_t> actual_handles) {
    zx_channel_call_args_t args;

    zx_status_t status = user_args.copy_from_user(&args);
    if (status != ZX_OK)
        return status;

    user_in_ptr<const void> user_bytes = make_user_in_ptr(args.wr_bytes);
    user_in_ptr<const zx_handle_t> user_handles = make_user_in_ptr(args.wr_handles);

    uint32_t num_bytes = args.wr_num_bytes;
    uint32_t num_handles = args.wr_num_handles;

    auto up = ProcessDispatcher::GetCurrent();

    if (options || num_bytes < sizeof(zx_txid_t)) {
        up->RemoveHandles(user_handles, num_handles);
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<ChannelDispatcher> channel;
    status = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_WRITE | ZX_RIGHT_READ, &channel);
    if (status != ZX_OK) {
        up->RemoveHandles(user_handles, num_handles);
        return status;
    }

    // Prepare a MessagePacket for writing
    fbl::unique_ptr<MessagePacket> msg;
    status = MessagePacket::Create(user_bytes, num_bytes, num_handles, &msg);
    if (status != ZX_OK) {
        up->RemoveHandles(user_handles, num_handles);
        return status;
    }

    if (num_handles > 0u) {
        status = msg_put_handles(up, msg.get(), user_handles, num_handles,
                                 static_cast<Dispatcher*>(channel.get()));
        if (status)
            return status;
    }

    // TODO(ZX-970): ktrace channel calls; maybe two traces, maybe with txid.

    // Write message and wait for reply, deadline, or cancelation
    fbl::unique_ptr<MessagePacket> reply;
    status = channel->Call(fbl::move(msg), deadline, &reply);
    if (status != ZX_OK)
        return status;
    return channel_call_epilogue(up, fbl::move(reply), &args, actual_bytes, actual_handles);
}

zx_status_t sys_channel_call_finish(zx_time_t deadline,
                                    user_in_ptr<const zx_channel_call_args_t> user_args,
                                    user_out_ptr<uint32_t> actual_bytes,
                                    user_out_ptr<uint32_t> actual_handles) {

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
    status = channel->ResumeInterruptedCall(waiter, deadline, &reply);
    if (status != ZX_OK)
        return status;
    return channel_call_epilogue(up, fbl::move(reply), &args, actual_bytes, actual_handles);

}
