// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/syscalls/port.h>
#include <magenta/types.h>

__BEGIN_CDECLS

// Dispatcher interface for performing asynchronous operations.
// There may be multiple implementations of this interface.
typedef struct async_dispatcher async_t;

// Private state owned by the asynchronous dispatcher.
// Clients should initialize the contents of this structure to zero using
// |ASYNC_STATE_INIT| or with calloc, memset, or a similar means.
typedef struct {
    uintptr_t reserved[2];
} async_state_t;

#define ASYNC_STATE_INIT \
    { 0u, 0u }

// Flags for asynchronous operations.
enum {
    // Asks the dispatcher to notify the handler when the dispatcher itself
    // is being shut down so that the handler can release its resources.
    //
    // The dispatcher will invoke the handler with a status of
    // |MX_ERR_CANCELED| to indicate that it is being shut down.
    //
    // This flag only applies to pending waits and tasks; receivers will
    // not be notified of shutdown.
    ASYNC_HANDLE_SHUTDOWN = 1 << 0,
};

// Return codes for |async_wait_handler_t|.
typedef enum {
    // The handler has finished waiting; it may immediately destroy or
    // reuse the associated wait context for another purpose.
    ASYNC_WAIT_FINISHED = 0,
    // The handler is requesting for the wait to be reiussed upon return;
    // it may modify the wait's properties before returning.
    ASYNC_WAIT_AGAIN = 1,
} async_wait_result_t;

// Handles completion of asynchronous wait operations.
//
// Reports the |status| of the wait.  If the status is |MX_OK| then |signal|
// describes the signal which was received, otherwise |signal| is null.
//
// The result indicates whether the wait should be repeated; it may
// modify the wait's properties (such as the trigger) before returning.
//
// The result must be |ASYNC_WAIT_FINISHED| if |status| was not |MX_OK|.
typedef struct async_wait async_wait_t;
typedef async_wait_result_t(async_wait_handler_t)(async_t* async,
                                                  async_wait_t* wait,
                                                  mx_status_t status,
                                                  const mx_packet_signal_t* signal);

// Context for an asynchronous wait operation.
// A separate instance must be used for each wait.
//
// It is customary to aggregate (in C) or subclass (in C++) this structure
// to allow the handler to retain additional information about the wait.
struct async_wait {
    // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
    async_state_t state;
    // The handler to invoke on completion of the wait.
    async_wait_handler_t* handler;
    // The object to wait for signals on.
    mx_handle_t object;
    // The set of signals to wait for.
    mx_signals_t trigger;
    // Valid flags: |ASYNC_HANDLE_SHUTDOWN|.
    uint32_t flags;
    // Reserved for future use, set to zero.
    uint32_t reserved;
};

// Return codes for |async_task_handler_t|.
typedef enum {
    // The handler has finished the task; it may immediately destroy or
    // reuse the associated task context for another purpose.
    ASYNC_TASK_FINISHED = 0,
    // The handler is requesting for the task to be reiussed upon return;
    // it may modify the task's properties before returning.  In particular,
    // it should modify the task's deadline to prevent the task from
    // immediately retriggering.
    ASYNC_TASK_REPEAT = 1,
} async_task_result_t;

// Handles execution of a posted task.
//
// Reports the |status| of the task.  If the status is |MX_OK| then the
// task ran, otherwise the task did not run.
//
// The result indicates whether the task should be repeated; it may
// modify the task's properties (such as the deadline) before returning.
//
// The result must be |ASYNC_TASK_FINISHED| if |status| was not |MX_OK|.
typedef struct async_task async_task_t;
typedef async_task_result_t(async_task_handler_t)(async_t* async,
                                                  async_task_t* task,
                                                  mx_status_t status);

// Context for a posted task.
// A separate instance must be used for each task.
//
// It is customary to aggregate (in C) or subclass (in C++) this structure
// to allow the handler to retain additional information about the task.
struct async_task {
    // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
    async_state_t state;
    // The handler to invoke to perform the task.
    async_task_handler_t* handler;
    // The time when the task should run.
    mx_time_t deadline;
    // Valid flags: |ASYNC_HANDLE_SHUTDOWN|.
    uint32_t flags;
    // Reserved for future use, set to zero.
    uint32_t reserved;
};

// Receives packets containing user supplied data.
//
// Reports the |status| of the receiver.  If the status is |MX_OK| then |data|
// describes the contents of the packet which was received, otherwise |data|
// is null.
//
// The handler may destroy or reuse the |receiver| object for another purpose
// as long as there are no more packets pending delivery to it.
typedef struct async_receiver async_receiver_t;
typedef void(async_receiver_handler_t)(async_t* async,
                                       async_receiver_t* receiver,
                                       mx_status_t status,
                                       const mx_packet_user_t* data);

// Context for packet receiver.
// The same instance may be used to receive arbitrarily many queued packets.
//
// It is customary to aggregate (in C) or subclass (in C++) this structure
// to allow the handler to retain additional information about the receiver.
struct async_receiver {
    // Private state owned by the dispatcher, initialize to zero with |ASYNC_STATE_INIT|.
    async_state_t state;
    // The handler to invoke when a packet is received.
    async_receiver_handler_t* handler;
    // Valid flags: None, set to zero.
    uint32_t flags;
    // Reserved for future use, set to zero.
    uint32_t reserved;
};

// Asynchronous dispatcher interface.
//
// Clients should prefer using the |async_*| inline functions declared below.
// See the documentation of those inline functions for information about each method.
//
// This interface consists of three groups of methods:
//
// - Waiting for signals: |begin_wait|, |cancel_wait|
// - Posting tasks: |post_task|, |cancel_task|
// - Queuing packets: |queue_packet|
//
// Implementations of this interface are not required to support all of these methods.
// Unsupported methods must have valid (non-null) function pointers, must have
// no side-effects, and must return |MX_ERR_NOT_SUPPORTED| when called.
// Furthermore, if an implementation supports one method of a group, such as |begin_wait|,
// it must also support the other methods of the group, such as |cancel_wait|.
//
// Many clients assume that the dispatcher interface is fully implemented and may
// fail to work with dispatchers that do not support the methods they need.
// Therefore general-purpose dispatcher implementations are strongly encouraged to
// support the whole interface to ensure broad compatibility.
typedef struct async_ops {
    mx_status_t (*begin_wait)(async_t* async, async_wait_t* wait);
    mx_status_t (*cancel_wait)(async_t* async, async_wait_t* wait);
    mx_status_t (*post_task)(async_t* async, async_task_t* task);
    mx_status_t (*cancel_task)(async_t* async, async_task_t* task);
    mx_status_t (*queue_packet)(async_t* async, async_receiver_t* receiver,
                                const mx_packet_user_t* data);
} async_ops_t;
struct async_dispatcher {
    const async_ops_t* ops;
};

// Begins asynchronously waiting for an object to receive one or more signals
// specified in |wait|.  Invokes the handler when the wait completes.
//
// The client is responsible for allocating and retaining the wait context
// until the handler runs or the wait is successfully canceled using
// `async_cancel_wait()`.
//
// When the dispatcher is shutting down (being destroyed), attempting to
// begin new waits will fail but previously begun waits can still be canceled
// successfully.
//
// Returns |MX_OK| if the wait has been successfully started.
// Returns |MX_ERR_BAD_STATE| if the dispatcher shut down.
// Returns |MX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// See |mx_object_wait_async()|.
inline mx_status_t async_begin_wait(async_t* async, async_wait_t* wait) {
    return async->ops->begin_wait(async, wait);
}

// Cancels the wait associated with |wait|.
//
// When the dispatcher is shutting down (being destroyed), attempting to
// begin new waits will fail but previously begun waits can still be canceled
// successfully.
//
// Returns |MX_OK| if there was a pending wait and it has been successfully
// canceled; its handler will not run again and can be released immediately.
// Returns |MX_ERR_NOT_FOUND| if there was no pending wait either because it
// already completed, had not been started, or its completion packet has been
// dequeued and is pending delivery to its handler (perhaps on another thread).
// Returns |MX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// See |mx_port_cancel()|.
inline mx_status_t async_cancel_wait(async_t* async, async_wait_t* wait) {
    return async->ops->cancel_wait(async, wait);
}

// Posts a task to run on or after its deadline following all posted
// tasks with lesser or equal deadlines.
//
// The client is responsible for allocating and retaining the task context
// until the handler is invoked or until the task is successfully canceled
// using `async_cancel_task()`.
//
// When the dispatcher is shutting down (being destroyed), attempting to
// post new tasks will fail but previously posted tasks can still be canceled
// successfully.
//
// Returns |MX_OK| if the task was successfully posted.
// Returns |MX_ERR_BAD_STATE| if the dispatcher shut down.
// Returns |MX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// See also |mx_deadline_after()|.
//
// TODO(MG-976): Strict serial ordering of task dispatch isn't always needed.
// We should consider adding support for multiple independent task queues or
// similar mechanisms.
inline mx_status_t async_post_task(async_t* async, async_task_t* task) {
    return async->ops->post_task(async, task);
}

// Cancels the task associated with |task|.
//
// When the dispatcher is shutting down (being destroyed), attempting to
// post new tasks will fail but previously posted tasks can still be canceled
// successfully.
//
// Returns |MX_OK| if there was a pending task and it has been successfully
// canceled; its handler will not run again and can be released immediately.
// Returns |MX_ERR_NOT_FOUND| if there was no pending task either because it
// already ran, had not been posted, or has been dequeued and is pending
// execution (perhaps on another thread).
// Returns |MX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
inline mx_status_t async_cancel_task(async_t* async, async_task_t* task) {
    return async->ops->cancel_task(async, task);
}

// Enqueues a packet of data for delivery to a receiver.
//
// The client is responsible for allocating and retaining the packet context
// until all packets have been delivered.
//
// The |data| will be copied into the packet.  May be NULL to create a
// zero-initialized packet payload.
//
// When the dispatcher is shutting down (being destroyed), attempting to
// queue new packets will fail.
//
// Returns |MX_OK| if the packet was successfully enqueued.
// Returns |MX_ERR_BAD_STATE| if the dispatcher shut down.
// Returns |MX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
//
// See |mx_port_queue()|.
inline mx_status_t async_queue_packet(async_t* async, async_receiver_t* receiver,
                                      const mx_packet_user_t* data) {
    return async->ops->queue_packet(async, receiver, data);
}

__END_CDECLS

#ifdef __cplusplus

#define ASYNC_DISALLOW_COPY_ASSIGN_AND_MOVE(_class_name) \
    _class_name(const _class_name&) = delete;            \
    _class_name(_class_name&&) = delete;                 \
    _class_name& operator=(const _class_name&) = delete; \
    _class_name& operator=(_class_name&&) = delete

namespace async {

// C++ wrapper for a pending wait operation.
// This object must not be destroyed until the wait has completed or been
// successfully canceled or the dispatcher itself has been destroyed.
// A separate instance must be used for each wait.
class Wait : private async_wait_t {
public:
    Wait();
    explicit Wait(mx_handle_t object, mx_signals_t trigger, uint32_t flags = 0u);
    virtual ~Wait();

    // The object to wait for signals on.
    mx_handle_t object() const { return async_wait_t::object; }
    void set_object(mx_handle_t object) { async_wait_t::object = object; }

    // The set of signals to wait for.
    mx_signals_t trigger() const { return async_wait_t::trigger; }
    void set_trigger(mx_signals_t trigger) { async_wait_t::trigger = trigger; }

    // Valid flags: |ASYNC_HANDLE_SHUTDOWN|.
    uint32_t flags() const { return async_wait_t::flags; }
    void set_flags(uint32_t flags) { async_wait_t::flags = flags; }

    // Begins asynchronously waiting for the object to receive one or more of
    // the trigger signals.
    //
    // See |async_begin_wait()| for details.
    mx_status_t Begin(async_t* async);

    // Cancels the wait.
    //
    // See |async_cancel_wait()| for details.
    mx_status_t Cancel(async_t* async);

protected:
    // Override this method to handle completion of the asynchronous wait operation.
    //
    // Reports the |status| of the wait.  If the status is |MX_OK| then |signal|
    // describes the signal which was received, otherwise |signal| is null.
    //
    // The result indicates whether the wait should be repeated; it may
    // modify the wait's properties (such as the trigger) before returning.
    //
    // The result must be |ASYNC_WAIT_FINISHED| if |status| was not |MX_OK|.
    virtual async_wait_result_t Handle(async_t* async,
                                       mx_status_t status, const mx_packet_signal_t* signal) = 0;

private:
    static async_wait_result_t CallHandler(async_t* async, async_wait_t* wait,
                                           mx_status_t status, const mx_packet_signal_t* signal);

    ASYNC_DISALLOW_COPY_ASSIGN_AND_MOVE(Wait);
};

// C++ wrapper for a pending task.
// This object must not be destroyed until the task has completed or been
// successfully canceled or the dispatcher itself has been destroyed.
// A separate instance must be used for each task.
class Task : private async_task_t {
public:
    Task();
    explicit Task(mx_time_t deadline, uint32_t flags = 0u);
    virtual ~Task();

    // The time when the task should run.
    mx_time_t deadline() const { return async_task_t::deadline; }
    void set_deadline(mx_time_t deadline) { async_task_t::deadline = deadline; }

    // Valid flags: |ASYNC_HANDLE_SHUTDOWN|.
    uint32_t flags() const { return async_task_t::flags; }
    void set_flags(uint32_t flags) { async_task_t::flags = flags; }

    // Posts a task to run on or after its deadline following all posted
    // tasks with lesser or equal deadlines.
    //
    // See |async_post_task()| for details.
    mx_status_t Post(async_t* async);

    // Cancels the task.
    //
    // See |async_cancel_task()| for details.
    mx_status_t Cancel(async_t* async);

protected:
    // Override this method to handle execution of the posted task.
    //
    // Reports the |status| of the task.  If the status is |MX_OK| then the
    // task ran, otherwise the task did not run.
    //
    // The result indicates whether the task should be repeated; it may
    // modify the task's properties (such as the deadline) before returning.
    //
    // The result must be |ASYNC_TASK_FINISHED| if |status| was not |MX_OK|.
    virtual async_task_result_t Handle(async_t* async, mx_status_t status) = 0;

private:
    static async_task_result_t CallHandler(async_t* async, async_task_t* task,
                                           mx_status_t status);

    ASYNC_DISALLOW_COPY_ASSIGN_AND_MOVE(Task);
};

// C++ wrapper for a packet receiver.
// This object must not be destroyed until all packets destined for it
// have been delivered or the dispatcher itself has been destroyed.
// The same instance may be used to receive arbitrarily many queued packets.
class Receiver : private async_receiver_t {
public:
    explicit Receiver(uint32_t flags = 0u);
    virtual ~Receiver();

    // Valid flags: None, set to zero.
    uint32_t flags() const { return async_receiver_t::flags; }
    void set_flags(uint32_t flags) { async_receiver_t::flags = flags; }

    // Enqueues a packet of data for delivery to the receiver.
    //
    // See |async_queue_packet()| for details.
    mx_status_t Queue(async_t* async, const mx_packet_user_t* data = nullptr);

protected:
    // Override this method to handle received packets.
    //
    // Reports the |status| of the receiver.  If the status is |MX_OK| then |data|
    // describes the contents of the packet which was received, otherwise |data|
    // is null.
    //
    // The handler may destroy or reuse this object for another purpose
    // as long as there are no more packets pending delivery to it.
    virtual void Handle(async_t* async, mx_status_t status,
                        const mx_packet_user_t* data) = 0;

private:
    static void CallHandler(async_t* async, async_receiver_t* receiver,
                            mx_status_t status, const mx_packet_user_t* data);

    ASYNC_DISALLOW_COPY_ASSIGN_AND_MOVE(Receiver);
};

} // namespace async

#endif // __cplusplus
