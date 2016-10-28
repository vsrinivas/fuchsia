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
#include <magenta/magenta.h>
#include <magenta/message_packet.h>
#include <magenta/process_dispatcher.h>
#include <magenta/user_copy.h>

#include <magenta/syscalls/channel.h>

#include <mxtl/algorithm.h>
#include <mxtl/inline_array.h>
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr uint32_t kMaxMessageSize = 65536u;
constexpr uint32_t kMaxMessageHandles = 1024u;

constexpr size_t kChannelReadHandlesChunkCount = 16u;
constexpr size_t kChannelWriteHandlesInlineCount = 8u;

mx_status_t sys_channel_create(uint32_t flags, user_ptr<mx_handle_t> out0, user_ptr<mx_handle_t> out1) {
    LTRACEF("entry out_handles %p,%p\n", out0.get(), out1.get());

    if ((flags != 0u) && (flags != MX_FLAG_REPLY_CHANNEL))
        return ERR_INVALID_ARGS;

    mxtl::RefPtr<Dispatcher> mpd0, mpd1;
    mx_rights_t rights;
    status_t result = ChannelDispatcher::Create(flags, &mpd0, &mpd1, &rights);
    if (result != NO_ERROR)
        return result;

    uint64_t id0 = mpd0->get_koid();
    uint64_t id1 = mpd1->get_koid();

    HandleUniquePtr h0(MakeHandle(mxtl::move(mpd0), rights));
    if (!h0)
        return ERR_NO_MEMORY;

    HandleUniquePtr h1(MakeHandle(mxtl::move(mpd1), rights));
    if (!h1)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    if (out0.copy_to_user(up->MapHandleToValue(h0.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;
    if (out1.copy_to_user(up->MapHandleToValue(h1.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(h0));
    up->AddHandle(mxtl::move(h1));

    ktrace(TAG_MSGPIPE_CREATE, (uint32_t)id0, (uint32_t)id1, flags, 0);
    return NO_ERROR;
}

mx_status_t sys_channel_read(mx_handle_t handle_value, uint32_t flags,
                             user_ptr<void> _bytes,
                             uint32_t num_bytes, user_ptr<uint32_t> _num_bytes,
                             user_ptr<mx_handle_t> _handles,
                             uint32_t num_handles, user_ptr<uint32_t> _num_handles) {
    LTRACEF("handle %d bytes %p num_bytes %p handles %p num_handles %p",
            handle_value, _bytes.get(), _num_bytes.get(), _handles.get(), _num_handles.get());

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ChannelDispatcher> channel;
    mx_status_t result = up->GetDispatcher(handle_value, &channel, MX_RIGHT_READ);
    if (result != NO_ERROR)
        return result;

    if (flags & ~MX_CHANNEL_READ_MASK)
        return ERR_NOT_SUPPORTED;

    mxtl::unique_ptr<MessagePacket> msg;
    result = channel->Read(&num_bytes, &num_handles, &msg,
                           flags & MX_CHANNEL_READ_MAY_DISCARD);
    if (result != NO_ERROR && result != ERR_BUFFER_TOO_SMALL)
        return result;

    // On ERR_BUFFER_TOO_SMALL, Read() gives us the size of the next message (which remains
    // unconsumed, unless |flags| has MX_CHANNEL_READ_MAY_DISCARD set).
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

    if (num_handles > 0u) {
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
            HandleUniquePtr handle(handle_list[idx]);
            up->AddHandle(mxtl::move(handle));
        }
    }

    ktrace(TAG_MSGPIPE_READ, (uint32_t)channel->get_koid(), num_bytes, num_handles, 0);
    return result;
}

mx_status_t sys_channel_write(mx_handle_t handle_value, uint32_t flags,
                              user_ptr<const void> _bytes, uint32_t num_bytes,
                              user_ptr<const mx_handle_t> _handles, uint32_t num_handles) {
    LTRACEF("handle %d bytes %p num_bytes %u handles %p num_handles %u flags 0x%x\n",
            handle_value, _bytes.get(), num_bytes, _handles.get(), num_handles, flags);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ChannelDispatcher> channel;
    mx_status_t result = up->GetDispatcher(handle_value, &channel, MX_RIGHT_WRITE);
    if (result != NO_ERROR)
        return result;

    bool is_reply_channel = channel->is_reply_channel();

    if (num_bytes > 0u && !_bytes)
        return ERR_INVALID_ARGS;
    if (num_handles > 0u && !_handles)
        return ERR_INVALID_ARGS;

    if (num_bytes > kMaxMessageSize)
        return ERR_OUT_OF_RANGE;
    if (num_handles > kMaxMessageHandles)
        return ERR_OUT_OF_RANGE;

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
        if (_handles.copy_array_from_user(handles.get(), num_handles) != NO_ERROR)
            return ERR_INVALID_ARGS;

        {
            // Loop twice, first we collect and validate handles, the second pass
            // we remove them from this process.
            AutoLock lock(up->handle_table_lock());

            size_t reply_channel_found = -1;

            for (size_t ix = 0; ix != num_handles; ++ix) {
                auto handle = up->GetHandle_NoLock(handles[ix]);
                if (!handle)
                    return up->BadHandle(handles[ix], ERR_BAD_HANDLE);

                if (handle->dispatcher().get() == static_cast<Dispatcher*>(channel.get())) {
                    // Found itself, which is only allowed for
                    // MX_FLAG_REPLY_CHANNEL (aka Reply) channels.
                    if (!is_reply_channel) {
                        return ERR_NOT_SUPPORTED;
                    } else {
                        reply_channel_found = ix;
                    }
                }

                if (!magenta_rights_check(handle->rights(), MX_RIGHT_TRANSFER))
                    return up->BadHandle(handles[ix], ERR_ACCESS_DENIED);

                msg->mutable_handles()[ix] = handle;
            }

            if (is_reply_channel) {
                // For reply channels, itself must be in the handle
                // array and be the last handle.
                if ((reply_channel_found != (num_handles - 1)))
                    return ERR_BAD_STATE;
            }

            for (size_t ix = 0; ix != num_handles; ++ix) {
                auto handle = up->RemoveHandle_NoLock(handles[ix]).release();
                // Passing duplicate handles is not allowed.
                // If we've already seen this handle flag an error.
                if (!handle) {
                    // Put back the handles we've already removed.
                    for (size_t idx = 0; idx < ix; ++idx) {
                        up->UndoRemoveHandle_NoLock(handles[idx]);
                    }
                    // TODO: more specific error?
                    return ERR_INVALID_ARGS;
                }
            }
        }

        // On success, the MessagePacket owns the handles.
        msg->set_owns_handles(true);
    } else {
        // For reply channels, itself must be in the handle array and
        // be the last handle.
        if (is_reply_channel)
            return ERR_BAD_STATE;
    }

    result = channel->Write(mxtl::move(msg));
    if (result != NO_ERROR) {
        // Write failed, put back the handles into this process.
        AutoLock lock(up->handle_table_lock());
        for (size_t ix = 0; ix != num_handles; ++ix) {
            up->UndoRemoveHandle_NoLock(handles[ix]);
        }
    }

    ktrace(TAG_MSGPIPE_WRITE, (uint32_t)channel->get_koid(), num_bytes, num_handles, 0);
    return result;
}
