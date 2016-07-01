// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <magenta/syscalls.h>
#include <mojo/mojo.h>
#include <mojo/mojo_events.h>
#include <mojo/mojo_futex.h>
#include <mojo/mojo_interrupt.h>
#include <mojo/mojo_message_pipe.h>
#include <mojo/mojo_process.h>
#include <mojo/mojo_threads.h>
#include <stdlib.h>
#include <string.h>

static mx_time_t mojo_to_mx_time(mojo_deadline_t t) {
    if (t == MOJO_DEADLINE_INDEFINITE)
        return MX_TIME_INFINITE;
    // Convert microseconds to nanoseconds
    return (mx_time_t)(t * 1000);
}

static mojo_result_t lk_to_mojo_error(mx_status_t err) {
    switch (err) {
    case NO_ERROR:
        return MOJO_RESULT_OK;
    case ERR_INTERNAL:
        return MOJO_RESULT_INTERNAL;
    case ERR_NOT_FOUND:
        return MOJO_RESULT_NOT_FOUND;
    case ERR_NOT_READY:
        return MOJO_RESULT_FAILED_PRECONDITION;
    case ERR_NO_MEMORY:
        return MOJO_RESULT_RESOURCE_EXHAUSTED;
    case ERR_ALREADY_STARTED:
        return MOJO_RESULT_FAILED_PRECONDITION;
    case ERR_NOT_VALID:
        return MOJO_RESULT_INVALID_ARGUMENT;
    case ERR_INVALID_ARGS:
        return MOJO_RESULT_INVALID_ARGUMENT;
    case ERR_NOT_ENOUGH_BUFFER:
        return MOJO_RESULT_RESOURCE_EXHAUSTED;
    case ERR_NOT_BLOCKED:
        return MOJO_RESULT_FAILED_PRECONDITION;
    case ERR_TIMED_OUT:
        return MOJO_RESULT_DEADLINE_EXCEEDED;
    case ERR_ALREADY_EXISTS:
        return MOJO_RESULT_ALREADY_EXISTS;
    case ERR_CHANNEL_CLOSED:
        return MOJO_RESULT_FAILED_PRECONDITION;
    case ERR_NOT_ALLOWED:
        return MOJO_RESULT_PERMISSION_DENIED;
    case ERR_BAD_PATH:
        return MOJO_RESULT_INVALID_ARGUMENT;
    case ERR_IO:
        return MOJO_RESULT_INTERNAL;
    case ERR_NOT_DIR:
        return MOJO_RESULT_FAILED_PRECONDITION;
    case ERR_NOT_FILE:
        return MOJO_RESULT_FAILED_PRECONDITION;
    case ERR_RECURSE_TOO_DEEP:
        return MOJO_RESULT_INTERNAL;
    case ERR_NOT_SUPPORTED:
        return MOJO_RESULT_UNIMPLEMENTED;
    case ERR_TOO_BIG:
        return MOJO_RESULT_OUT_OF_RANGE;
    case ERR_CANCELLED:
        return MOJO_RESULT_ABORTED;
    case ERR_NOT_IMPLEMENTED:
        return MOJO_RESULT_UNIMPLEMENTED;
    case ERR_CHECKSUM_FAIL:
        return MOJO_RESULT_DATA_LOSS;
    case ERR_BAD_STATE:
        return MOJO_RESULT_FAILED_PRECONDITION;
    case ERR_BUSY:
        return MOJO_RESULT_BUSY;
    case ERR_THREAD_DETACHED:
        return MOJO_RESULT_FAILED_PRECONDITION;
    case ERR_I2C_NACK:
        return MOJO_RESULT_DATA_LOSS;
    case ERR_OUT_OF_RANGE:
        return MOJO_RESULT_OUT_OF_RANGE;
    case ERR_NOT_MOUNTED:
        return MOJO_RESULT_FAILED_PRECONDITION;
    case ERR_FAULT:
        return MOJO_RESULT_INTERNAL;
    case ERR_NO_RESOURCES:
        return MOJO_RESULT_RESOURCE_EXHAUSTED;
    case ERR_BAD_HANDLE:
        return MOJO_RESULT_INTERNAL;
    case ERR_ACCESS_DENIED:
        return MOJO_RESULT_PERMISSION_DENIED;
    default:
        return MOJO_RESULT_UNKNOWN;
    }
}

mojo_result_t mojo_close(mojo_handle_t handle) {
    return lk_to_mojo_error(_magenta_handle_close(handle));
}

mojo_result_t mojo_duplicate(mojo_handle_t handle, mojo_handle_t* out_handle) {
    mx_handle_t result = _magenta_handle_duplicate(handle, MX_RIGHT_SAME_RIGHTS);
    if (result < 0)
        return lk_to_mojo_error(result);
    *out_handle = result;
    return MOJO_RESULT_OK;
}

mojo_result_t mojo_wait(const mojo_handle_t* handles, const mojo_handle_signals_t* signals,
                        uint32_t num_handles, uint32_t* result_index, mojo_deadline_t deadline,
                        mojo_handle_signals_t* satisfied_signals,
                        mojo_handle_signals_t* satisfiable_signals) {
    mx_status_t result;

    if (num_handles == 1u) {
        result =
            _magenta_handle_wait_one(*handles, *signals, mojo_to_mx_time(deadline),
                                     satisfied_signals, satisfiable_signals);
    } else {
        result = _magenta_handle_wait_many(num_handles, (mx_handle_t*)handles, signals,
                                           mojo_to_mx_time(deadline),
                                           satisfied_signals, satisfiable_signals);
    }

    // TODO(cpu): implement |result_index|, see MG-33 bug.
    return lk_to_mojo_error(result);
}

mojo_result_t mojo_create_message_pipe(mojo_handle_t* handle0, mojo_handle_t* handle1) {
    mx_handle_t result = _magenta_message_pipe_create((mx_handle_t*)handle1);
    if (result < 0)
        return lk_to_mojo_error(result);
    *handle0 = result;
    return MOJO_RESULT_OK;
}

mojo_result_t mojo_read_message(mojo_handle_t handle, void* bytes, uint32_t* num_bytes,
                                mojo_handle_t* handles, uint32_t* num_handles, uint32_t flags) {
    return lk_to_mojo_error(
        _magenta_message_read(handle, bytes, num_bytes, (mx_handle_t*)handles, num_handles, flags));
}

mojo_result_t mojo_write_message(mojo_handle_t handle, const void* bytes, uint32_t num_bytes,
                                 const mojo_handle_t* handles, uint32_t num_handles,
                                 uint32_t flags) {
    return lk_to_mojo_error(
        _magenta_message_write(handle, bytes, num_bytes, (mx_handle_t*)handles, num_handles, flags));
}

void mojo_exit(int ec) {
    // call the exit syscall in a loop to satisfy compiler NO_RETURN semantics
    for (;;)
        _magenta_exit(ec);
}

// returns elapsed microseconds since boot
uint64_t mojo_current_time(void) {
    return _magenta_current_time();
}

struct thread_args {
    mojo_thread_start_routine entry;
    void* arg;
};

/* Wrapper for our thread to make sure thread_exit gets called */
static int thread_entry(void* args) {
    struct thread_args* ta = args;
    mojo_thread_start_routine entry = ta->entry;
    void* arg = ta->arg;
    free(args);

    int rc = entry(arg);
    _magenta_thread_exit();
    return rc;
}

mojo_result_t mojo_thread_create(mojo_thread_start_routine entry, void* arg,
                                 mojo_handle_t* out_handle, const char* name) {
    struct thread_args* ta = malloc(sizeof(struct thread_args));
    if (!ta) {
        return MOJO_RESULT_RESOURCE_EXHAUSTED;
    }
    ta->entry = entry;
    ta->arg = arg;
    if (!name)
        name = "";
    mx_handle_t result = _magenta_thread_create(thread_entry, ta, name, strlen(name) + 1);
    if (result < 0)
        return lk_to_mojo_error(result);
    *out_handle = result;
    return MOJO_RESULT_OK;
}

void mojo_thread_exit(void) {
    _magenta_thread_exit();
}

mojo_result_t mojo_thread_join(mojo_handle_t handle, mojo_deadline_t timeout) {
    // TODO: add timeout
    mx_status_t result = _magenta_handle_wait_one(handle, MX_SIGNAL_SIGNALED,
                                                  mojo_to_mx_time(timeout), NULL, NULL);
    return lk_to_mojo_error(result);
}

mojo_result_t mojo_interrupt_event_create(uint32_t vector, uint32_t flags,
                                          mojo_handle_t* out_handle) {
    mx_handle_t result = _magenta_interrupt_event_create(vector, flags);
    if (result < 0)
        return lk_to_mojo_error(result);
    *out_handle = result;
    return MOJO_RESULT_OK;
}

mojo_result_t mojo_interrupt_event_complete(mojo_handle_t handle) {
    return lk_to_mojo_error(_magenta_interrupt_event_complete(handle));
}

mojo_result_t mojo_interrupt_event_wait(mojo_handle_t handle) {
    return lk_to_mojo_error(_magenta_interrupt_event_wait(handle));
}

mojo_result_t mojo_process_create(mojo_handle_t* out_handle) {
    // TODO(cpu): Get Mojo processes a name.
    char pname[] = "mojo<?>";
    mx_handle_t result = _magenta_process_create(pname, sizeof(pname));
    if (result < 0)
        return lk_to_mojo_error(result);
    *out_handle = result;
    return MOJO_RESULT_OK;
}

mojo_result_t mojo_process_load(mojo_handle_t handle, const char* name) {
    return MOJO_RESULT_UNIMPLEMENTED;
}

mojo_result_t mojo_process_start(mojo_handle_t handle, mojo_handle_t handle_arg) {
    return lk_to_mojo_error(_magenta_process_start(handle, handle_arg, 0));
}

mojo_result_t mojo_process_join(mojo_handle_t handle, int* out_retcode) {
    *out_retcode = 0;

    // wait for the process to exit
    mx_status_t r = _magenta_handle_wait_one(handle, MX_SIGNAL_SIGNALED,
                                             MX_TIME_INFINITE, NULL, NULL);
    if (r != NO_ERROR)
        return lk_to_mojo_error(r);

    // read the return code
    if (out_retcode) {
        mx_process_info_t proc_info;
        r = _magenta_process_get_info(handle, &proc_info, sizeof(proc_info));
        if (r != NO_ERROR)
            return lk_to_mojo_error(r);

        *out_retcode = proc_info.return_code;
    }

    return MOJO_RESULT_OK;
}

mojo_result_t mojo_event_create(uint32_t options, mojo_handle_t* out_handle) {
    mx_handle_t result = _magenta_event_create(options);
    if (result < 0)
        return lk_to_mojo_error(result);
    *out_handle = result;
    return MOJO_RESULT_OK;
}

mojo_result_t mojo_event_signal(mojo_handle_t handle) {
    return lk_to_mojo_error(_magenta_event_signal(handle));
}

mojo_result_t mojo_event_reset(mojo_handle_t handle) {
    return lk_to_mojo_error(_magenta_event_reset(handle));
}

mojo_result_t mojo_futex_wait(int* value_ptr, int current_value, mojo_deadline_t timeout) {
    return lk_to_mojo_error(
        _magenta_futex_wait(value_ptr, current_value, mojo_to_mx_time(timeout)));
}

mojo_result_t mojo_futex_wake(int* value_ptr, uint32_t count) {
    return lk_to_mojo_error(_magenta_futex_wake(value_ptr, count));
}

mojo_result_t mojo_futex_requeue(int* wake_ptr, uint32_t wake_count, int current_value,
                                 int* requeue_ptr, uint32_t requeue_count) {
    return lk_to_mojo_error(
        _magenta_futex_requeue(wake_ptr, wake_count, current_value, requeue_ptr, requeue_count));
}
