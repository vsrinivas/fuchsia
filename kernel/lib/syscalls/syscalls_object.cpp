// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/auto_lock.h>

#include <magenta/data_pipe_consumer_dispatcher.h>
#include <magenta/data_pipe_producer_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/thread_dispatcher.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0


mx_status_t sys_object_get_info(mx_handle_t handle, uint32_t topic, uint16_t topic_size,
                                user_ptr<void> _buffer, mx_size_t buffer_size,
                                user_ptr<mx_size_t> actual) {
    auto up = ProcessDispatcher::GetCurrent();

    LTRACEF("handle %d topic %u topic_size %u buffer %p buffer_size %"
            PRIuPTR "\n",
            handle, topic, topic_size, _buffer.get(), buffer_size);

    switch (topic) {
        case MX_INFO_HANDLE_VALID: {
            mxtl::RefPtr<Dispatcher> dispatcher;
            uint32_t rights;

            // test that the handle is valid at all, return error if it's not
            if (!up->GetDispatcher(handle, &dispatcher, &rights))
                return ERR_BAD_HANDLE;
            return NO_ERROR;
        }
        case MX_INFO_HANDLE_BASIC: {
            mxtl::RefPtr<Dispatcher> dispatcher;
            uint32_t rights;

            if (!up->GetDispatcher(handle, &dispatcher, &rights))
                return up->BadHandle(handle, ERR_BAD_HANDLE);

            // test that they've asking for an appropriate version
            if (topic_size != 0 && topic_size != sizeof(mx_record_handle_basic_t))
                return ERR_INVALID_ARGS;

            // make sure they passed us a buffer
            if (!_buffer)
                return ERR_INVALID_ARGS;

            // test that we have at least enough target buffer to support the header and one record
            if (buffer_size < sizeof(mx_info_header_t) + topic_size)
                return ERR_BUFFER_TOO_SMALL;

            // build the info structure
            mx_info_handle_basic_t info = {};

            // fill in the header
            info.hdr.topic = topic;
            info.hdr.avail_topic_size = sizeof(info.rec);
            info.hdr.topic_size = topic_size;
            info.hdr.avail_count = 1;
            info.hdr.count = 1;

            mx_size_t tocopy;
            if (topic_size == 0) {
                // just copy the header
                tocopy = sizeof(info.hdr);
            } else {
                bool waitable = dispatcher->get_state_tracker() != nullptr;

                // copy the header and the record
                info.rec.koid = dispatcher->get_koid();
                info.rec.rights = rights;
                info.rec.type = dispatcher->get_type();
                info.rec.props = waitable ? MX_OBJ_PROP_WAITABLE : MX_OBJ_PROP_NONE;

                tocopy = sizeof(info);
            }

            if (_buffer.copy_array_to_user(&info, tocopy) != NO_ERROR)
                return ERR_INVALID_ARGS;
            if (actual.copy_to_user(tocopy) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_INFO_PROCESS: {
            // grab a reference to the dispatcher
            mxtl::RefPtr<ProcessDispatcher> process;
            auto error = up->GetDispatcher<ProcessDispatcher>(handle, &process, MX_RIGHT_READ);
            if (error < 0)
                return error;

            // test that they've asking for an appropriate version
            if (topic_size != 0 && topic_size != sizeof(mx_record_process_t))
                return ERR_INVALID_ARGS;

            // make sure they passed us a buffer
            if (!_buffer)
                return ERR_INVALID_ARGS;

            // test that we have at least enough target buffer to support the header and one record
            if (buffer_size < sizeof(mx_info_header_t) + topic_size)
                return ERR_BUFFER_TOO_SMALL;

            // build the info structure
            mx_info_process_t info = {};

            // fill in the header
            info.hdr.topic = topic;
            info.hdr.avail_topic_size = sizeof(info.rec);
            info.hdr.topic_size = topic_size;
            info.hdr.avail_count = 1;
            info.hdr.count = 1;

            mx_size_t tocopy;
            if (topic_size == 0) {
                // just copy the header
                tocopy = sizeof(info.hdr);
            } else {
                auto err = process->GetInfo(&info.rec);
                if (err != NO_ERROR)
                    return err;

                tocopy = sizeof(info);
            }

            if (_buffer.copy_array_to_user(&info, tocopy) != NO_ERROR)
                return ERR_INVALID_ARGS;
            if (actual.copy_to_user(tocopy) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_INFO_PROCESS_THREADS: {
            // grab a reference to the dispatcher
            mxtl::RefPtr<ProcessDispatcher> process;
            auto error = up->GetDispatcher<ProcessDispatcher>(handle, &process, MX_RIGHT_READ);
            if (error < 0)
                return error;

            // test that they've asking for an appropriate version
            if (topic_size != 0 && topic_size != sizeof(mx_record_process_thread_t))
                return ERR_INVALID_ARGS;

            // make sure they passed us a buffer
            if (!_buffer)
                return ERR_INVALID_ARGS;

            // test that we have at least enough target buffer to at least support the header
            if (buffer_size < sizeof(mx_info_header_t))
                return ERR_BUFFER_TOO_SMALL;

            // Getting the list of threads is inherently racy (unless the
            // caller has already stopped all threads, but that's not our
            // concern). Still, we promise to either return all threads we know
            // about at a particular point in time, or notify the caller that
            // more threads exist than what we computed at that same point in
            // time.

            mxtl::Array<mx_record_process_thread_t> threads;
            mx_status_t status = process->GetThreads(&threads);
            if (status != NO_ERROR)
                return status;
            size_t actual_num_threads = threads.size();
            if (actual_num_threads > UINT32_MAX)
                return ERR_BAD_STATE;
            size_t thread_offset = offsetof(mx_info_process_threads_t, rec);
            size_t num_space_for =
                (buffer_size - thread_offset) / sizeof(mx_record_process_thread_t);
            size_t num_to_copy = 0;
            if (topic_size > 0)
                num_to_copy = MIN(actual_num_threads, num_space_for);
            if (num_to_copy > UINT32_MAX)
                return ERR_INVALID_ARGS;

            mx_info_header_t hdr;
            hdr.topic = topic;
            hdr.avail_topic_size = sizeof(mx_record_process_thread_t);
            hdr.topic_size = topic_size;
            hdr.avail_count = static_cast<uint32_t>(actual_num_threads);
            hdr.count = static_cast<uint32_t>(num_to_copy);

            if (_buffer.copy_array_to_user(&hdr, sizeof(hdr)) != NO_ERROR)
                return ERR_INVALID_ARGS;
            auto thread_result_buffer = _buffer.byte_offset(thread_offset);
            if (thread_result_buffer.reinterpret<mx_record_process_thread_t>().copy_array_to_user(threads.get(), num_to_copy) != NO_ERROR)
                return ERR_INVALID_ARGS;
            size_t result_bytes = thread_offset + (num_to_copy * topic_size);
            if (actual.copy_to_user(result_bytes) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        default:
            return ERR_NOT_FOUND;
    }
}

mx_status_t sys_object_get_property(mx_handle_t handle_value, uint32_t property,
                                    user_ptr<void> _value, mx_size_t size) {
    if (!_value)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return up->BadHandle(handle_value, ERR_BAD_HANDLE);

    if (!magenta_rights_check(rights, MX_RIGHT_GET_PROPERTY))
        return ERR_ACCESS_DENIED;

    switch (property) {
        case MX_PROP_BAD_HANDLE_POLICY: {
            if (size < sizeof(uint32_t))
                return ERR_BUFFER_TOO_SMALL;
            auto process = dispatcher->get_specific<ProcessDispatcher>();
            if (!process)
                return ERR_WRONG_TYPE;
            uint32_t value = process->get_bad_handle_policy();
            if (_value.reinterpret<uint32_t>().copy_to_user(value) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_PROP_NUM_STATE_KINDS: {
            if (size != sizeof(uint32_t))
                return ERR_BUFFER_TOO_SMALL;
            auto thread = dispatcher->get_specific<ThreadDispatcher>();
            if (!thread)
                return ERR_WRONG_TYPE;
            uint32_t value = thread->thread()->get_num_state_kinds();
            if (_value.reinterpret<uint32_t>().copy_to_user(value) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_PROP_DATAPIPE_READ_THRESHOLD: {
            auto consumer_dispatcher = dispatcher->get_specific<DataPipeConsumerDispatcher>();
            if (!consumer_dispatcher)
                return ERR_WRONG_TYPE;
            if (size < sizeof(mx_size_t))
                return ERR_BUFFER_TOO_SMALL;
            mx_size_t threshold = consumer_dispatcher->GetReadThreshold();
            if (_value.reinterpret<mx_size_t>().copy_to_user(threshold) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        case MX_PROP_DATAPIPE_WRITE_THRESHOLD: {
            auto producer_dispatcher = dispatcher->get_specific<DataPipeProducerDispatcher>();
            if (!producer_dispatcher)
                return ERR_WRONG_TYPE;
            if (size < sizeof(mx_size_t))
                return ERR_BUFFER_TOO_SMALL;
            mx_size_t threshold = producer_dispatcher->GetWriteThreshold();
            if (_value.reinterpret<mx_size_t>().copy_to_user(threshold) != NO_ERROR)
                return ERR_INVALID_ARGS;
            return NO_ERROR;
        }
        default:
            return ERR_INVALID_ARGS;
    }

    __UNREACHABLE;
}

mx_status_t sys_object_set_property(mx_handle_t handle_value, uint32_t property,
                                    user_ptr<const void> _value, mx_size_t size) {
    if (!_value)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return up->BadHandle(handle_value, ERR_BAD_HANDLE);

    if (!magenta_rights_check(rights, MX_RIGHT_SET_PROPERTY))
        return up->BadHandle(handle_value, ERR_ACCESS_DENIED);

    mx_status_t status = ERR_INVALID_ARGS;

    switch (property) {
        case MX_PROP_BAD_HANDLE_POLICY: {
            if (size < sizeof(uint32_t))
                return ERR_BUFFER_TOO_SMALL;
            auto process = dispatcher->get_specific<ProcessDispatcher>();
            if (!process)
                return up->BadHandle(handle_value, ERR_WRONG_TYPE);
            uint32_t value = 0;
            if (_value.reinterpret<const uint32_t>().copy_from_user(&value) != NO_ERROR)
                return ERR_INVALID_ARGS;
            status = process->set_bad_handle_policy(value);
            break;
        }
        case MX_PROP_DATAPIPE_READ_THRESHOLD: {
            if (size < sizeof(mx_size_t))
                return ERR_BUFFER_TOO_SMALL;
            auto consumer_dispatcher = dispatcher->get_specific<DataPipeConsumerDispatcher>();
            if (!consumer_dispatcher)
                return up->BadHandle(handle_value, ERR_WRONG_TYPE);
            mx_size_t threshold = 0;
            if (_value.reinterpret<const mx_size_t>().copy_from_user(&threshold) != NO_ERROR)
                return ERR_INVALID_ARGS;
            status = consumer_dispatcher->SetReadThreshold(threshold);
            break;
        }
        case MX_PROP_DATAPIPE_WRITE_THRESHOLD: {
            if (size < sizeof(mx_size_t))
                return ERR_BUFFER_TOO_SMALL;
            auto producer_dispatcher = dispatcher->get_specific<DataPipeProducerDispatcher>();
            if (!producer_dispatcher)
                return up->BadHandle(handle_value, ERR_WRONG_TYPE);
            mx_size_t threshold = 0;
            if (_value.reinterpret<const mx_size_t>().copy_from_user(&threshold) != NO_ERROR)
                return ERR_INVALID_ARGS;
            status = producer_dispatcher->SetWriteThreshold(threshold);
            break;
        }
    }

    return status;
}

mx_status_t sys_object_signal(mx_handle_t handle_value, uint32_t clear_mask, uint32_t set_mask) {
    LTRACEF("handle %d\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<Dispatcher> dispatcher;
    uint32_t rights;

    if (!up->GetDispatcher(handle_value, &dispatcher, &rights))
        return up->BadHandle(handle_value, ERR_BAD_HANDLE);
    if (!magenta_rights_check(rights, MX_RIGHT_WRITE))
        return up->BadHandle(handle_value, ERR_ACCESS_DENIED);

    mx_status_t status = dispatcher->UserSignal(clear_mask, set_mask);
    return (status == ERR_BAD_HANDLE) ? up->BadHandle(handle_value, ERR_BAD_HANDLE) : status;
}

// Given a kernel object with children objects, obtain a handle to the
// child specified by the provided kernel object id.
//
// MX_HANDLE_INVALID is currently treated as a "magic" handle used to
// object a process from "the system".
mx_status_t sys_object_get_child(mx_handle_t handle, uint64_t koid, mx_rights_t rights, user_ptr<mx_handle_t> out) {
    auto up = ProcessDispatcher::GetCurrent();

    if (handle == MX_HANDLE_INVALID) {
        //TODO: lookup process from job instead of treating INVALID as magic
        const auto kDebugRights =
            MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER |
            MX_RIGHT_GET_PROPERTY | MX_RIGHT_SET_PROPERTY;

        if (rights == MX_RIGHT_SAME_RIGHTS) {
            rights = kDebugRights;
        } else if ((kDebugRights & rights) != rights) {
            return ERR_ACCESS_DENIED;
        }

        auto process = ProcessDispatcher::LookupProcessById(koid);
        if (!process)
            return ERR_NOT_FOUND;

        HandleUniquePtr process_h(
            MakeHandle(mxtl::RefPtr<Dispatcher>(process.get()), rights));
        if (!process_h)
            return ERR_NO_MEMORY;

        if (out.copy_to_user(up->MapHandleToValue(process_h.get())))
            return ERR_INVALID_ARGS;
        up->AddHandle(mxtl::move(process_h));
        return NO_ERROR;
    }

    mxtl::RefPtr<Dispatcher> dispatcher;
    uint32_t parent_rights;
    if (!up->GetDispatcher(handle, &dispatcher, &parent_rights))
        return ERR_BAD_HANDLE;

    if (rights == MX_RIGHT_SAME_RIGHTS) {
        rights = parent_rights;
    } else if ((parent_rights & rights) != rights) {
        return ERR_ACCESS_DENIED;
    }

    auto process = dispatcher->get_specific<ProcessDispatcher>();
    if (process) {
        auto thread = process->LookupThreadById(koid);
        if (!thread)
            return ERR_NOT_FOUND;
        auto td = mxtl::RefPtr<Dispatcher>(thread->dispatcher());
        if (!td)
            return ERR_NOT_FOUND;
        HandleUniquePtr thread_h(MakeHandle(td, rights));
        if (!thread_h)
            return ERR_NO_MEMORY;

        if (out.copy_to_user(up->MapHandleToValue(thread_h.get())) != NO_ERROR)
            return ERR_INVALID_ARGS;
        up->AddHandle(mxtl::move(thread_h));
        return NO_ERROR;
    }

    return ERR_WRONG_TYPE;
}

