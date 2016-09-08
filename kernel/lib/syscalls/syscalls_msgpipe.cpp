#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/auto_lock.h>

#include <lib/ktrace.h>
#include <lib/user_copy.h>

#include <magenta/magenta.h>
#include <magenta/message_pipe_dispatcher.h>
#include <magenta/process_dispatcher.h>
#include <magenta/user_copy.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr uint32_t kMaxMessageSize = 65536u;
constexpr uint32_t kMaxMessageHandles = 1024u;

mx_status_t sys_msgpipe_create(mxtl::user_ptr<mx_handle_t> out_handle /* array of size 2 */,
                               uint32_t flags) {
    LTRACEF("entry out_handle[] %p\n", out_handle.get());

    if (!out_handle)
        return ERR_INVALID_ARGS;

    if ((flags != 0u) && (flags != MX_FLAG_REPLY_PIPE))
        return ERR_INVALID_ARGS;

    mxtl::RefPtr<Dispatcher> mpd0, mpd1;
    mx_rights_t rights;
    status_t result = MessagePipeDispatcher::Create(flags, &mpd0, &mpd1, &rights);
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
    mx_handle_t hv[2] = {up->MapHandleToValue(h0.get()), up->MapHandleToValue(h1.get())};

    if (copy_to_user(out_handle, hv, sizeof(mx_handle_t) * 2) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(h0));
    up->AddHandle(mxtl::move(h1));

    ktrace(TAG_MSGPIPE_CREATE, (uint32_t)id0, (uint32_t)id1, flags, 0);
    return NO_ERROR;
}

mx_status_t sys_msgpipe_read(mx_handle_t handle_value,
                             mxtl::user_ptr<void> _bytes,
                             mxtl::user_ptr<uint32_t> _num_bytes,
                             mxtl::user_ptr<mx_handle_t> _handles,
                             mxtl::user_ptr<uint32_t> _num_handles,
                             uint32_t flags) {
    LTRACEF("handle %d bytes %p num_bytes %p handles %p num_handles %p",
            handle_value, _bytes.get(), _num_bytes.get(), _handles.get(), _num_handles.get());

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<MessagePipeDispatcher> msg_pipe;
    mx_status_t status = up->GetDispatcher(handle_value, &msg_pipe,
                                           MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    uint32_t num_bytes = 0;
    uint32_t num_handles = 0;

    if (_num_bytes) {
        if (copy_from_user_u32(&num_bytes, _num_bytes) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    if (_num_handles) {
        if (copy_from_user_u32(&num_handles, _num_handles) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    if (_bytes != 0u && !_num_bytes)
        return ERR_INVALID_ARGS;
    if (_handles != 0u && !_num_handles)
        return ERR_INVALID_ARGS;

    mxtl::unique_ptr<uint32_t[]> handles;

    AllocChecker ac;
    if (num_handles) {
        handles.reset(new (&ac) uint32_t[num_handles]());
        if (!ac.check())
            return ERR_NO_MEMORY;
    }

    uint32_t next_message_size = 0u;
    uint32_t next_message_num_handles = 0u;
    status_t result = msg_pipe->BeginRead(&next_message_size, &next_message_num_handles);
    if (result != NO_ERROR)
        return result;

    // Always set the actual size and handle count so the caller can provide larger buffers.
    if (_num_bytes) {
        if (copy_to_user_u32(_num_bytes, next_message_size) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }
    if (_num_handles) {
        if (copy_to_user_u32(_num_handles, next_message_num_handles) != NO_ERROR)
            return ERR_INVALID_ARGS;
    }

    // If the caller provided buffers are too small, abort the read so the caller can try again.
    if (num_bytes < next_message_size || num_handles < next_message_num_handles)
        return ERR_NOT_ENOUGH_BUFFER;

    // OK, now we can accept the message.
    mxtl::Array<uint8_t> bytes;
    mxtl::Array<Handle*> handle_list;

    result = msg_pipe->AcceptRead(&bytes, &handle_list);

    if (_bytes) {
        if (copy_to_user(_bytes.reinterpret<uint8_t>(), bytes.get(), num_bytes) != NO_ERROR) {
            return ERR_INVALID_ARGS;
        }
    }

    if (next_message_num_handles != 0u) {
        for (size_t ix = 0u; ix < next_message_num_handles; ++ix) {
            auto hv = up->MapHandleToValue(handle_list[ix]);
            if (copy_to_user_32_unsafe(&_handles.get()[ix], hv) != NO_ERROR)
                return ERR_INVALID_ARGS;
        }
    }

    for (size_t idx = 0u; idx < next_message_num_handles; ++idx) {
        if (handle_list[idx]->dispatcher()->get_state_tracker())
            handle_list[idx]->dispatcher()->get_state_tracker()->Cancel(handle_list[idx]);
        HandleUniquePtr handle(handle_list[idx]);
        up->AddHandle(mxtl::move(handle));
    }

    ktrace(TAG_MSGPIPE_READ, (uint32_t)msg_pipe->get_koid(),
           next_message_size, next_message_num_handles, 0);
    return result;
}

mx_status_t sys_msgpipe_write(mx_handle_t handle_value,
                              mxtl::user_ptr<const void> _bytes, uint32_t num_bytes,
                              mxtl::user_ptr<const mx_handle_t> _handles, uint32_t num_handles,
                              uint32_t flags) {
    LTRACEF("handle %d bytes %p num_bytes %u handles %p num_handles %u flags 0x%x\n",
            handle_value, _bytes.get(), num_bytes, _handles.get(), num_handles, flags);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<MessagePipeDispatcher> msg_pipe;
    mx_status_t status = up->GetDispatcher(handle_value, &msg_pipe,
                                           MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    bool is_reply_pipe = msg_pipe->is_reply_pipe();

    if (num_bytes != 0u && !_bytes)
        return ERR_INVALID_ARGS;
    if (num_handles != 0u && !_handles)
        return ERR_INVALID_ARGS;

    if (num_bytes > kMaxMessageSize)
        return ERR_OUT_OF_RANGE;
    if (num_handles > kMaxMessageHandles)
        return ERR_OUT_OF_RANGE;

    status_t result;
    mxtl::Array<uint8_t> bytes;

    if (num_bytes) {
        void* copy;
        result = magenta_copy_user_dynamic(_bytes.get(), &copy, num_bytes, kMaxMessageSize);
        if (result != NO_ERROR)
            return result;
        bytes.reset(reinterpret_cast<uint8_t*>(copy), num_bytes);
    }

    mxtl::unique_ptr<mx_handle_t[], mxtl::free_delete> handles;
    if (num_handles) {
        void* c_handles;
        status_t status = magenta_copy_user_dynamic(
            _handles.reinterpret<const void>().get(),
            &c_handles,
            num_handles * sizeof(_handles.get()[0]),
            kMaxMessageHandles);
        // |status| can be ERR_NO_MEMORY or ERR_INVALID_ARGS.
        if (status != NO_ERROR)
            return status;

        handles.reset(static_cast<mx_handle_t*>(c_handles));
    }

    AllocChecker ac;
    mxtl::Array<Handle*> handle_list(new (&ac) Handle*[num_handles], num_handles);
    if (!ac.check())
        return ERR_NO_MEMORY;

    {
        // Loop twice, first we collect and validate handles, the second pass
        // we remove them from this process.
        AutoLock lock(up->handle_table_lock());

        size_t reply_pipe_found = -1;

        for (size_t ix = 0; ix != num_handles; ++ix) {
            auto handle = up->GetHandle_NoLock(handles[ix]);
            if (!handle)
                return up->BadHandle(handles[ix], ERR_BAD_HANDLE);

            if (handle->dispatcher().get() == static_cast<Dispatcher*>(msg_pipe.get())) {
                // Found itself, which is only allowed for MX_FLAG_REPLY_PIPE (aka Reply) pipes.
                if (!is_reply_pipe) {
                    return ERR_NOT_SUPPORTED;
                } else {
                    reply_pipe_found = ix;
                }
            }

            if (!magenta_rights_check(handle->rights(), MX_RIGHT_TRANSFER))
                return up->BadHandle(handles[ix], ERR_ACCESS_DENIED);

            handle_list[ix] = handle;
        }

        if (is_reply_pipe) {
            // For reply pipes, itself must be in the handle array and be the last handle.
            if ((num_handles == 0) || (reply_pipe_found != (num_handles - 1)))
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

    result = msg_pipe->Write(mxtl::move(bytes), mxtl::move(handle_list));

    if (result != NO_ERROR) {
        // Write failed, put back the handles into this process.
        AutoLock lock(up->handle_table_lock());
        for (size_t ix = 0; ix != num_handles; ++ix) {
            up->UndoRemoveHandle_NoLock(handles[ix]);
        }
    }

    ktrace(TAG_MSGPIPE_WRITE, (uint32_t)msg_pipe->get_koid(), num_bytes, num_handles, 0);
    return result;
}

