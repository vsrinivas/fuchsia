// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>

#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>

#include <magenta/magenta.h>
#include <magenta/msg_pipe_dispatcher.h>
#include <magenta/user_process.h>
#include <magenta/user_thread.h>

#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 0

UserThread::UserThread(UserProcess* process, thread_start_routine entry, void* arg)
    : process_(process), entry_(entry), arg_(arg), joined_(false), detached_(false), user_stack_(nullptr),
      exception_behaviour_(MX_EXCEPTION_BEHAVIOUR_DEFAULT),
      exception_status_(MX_EXCEPTION_STATUS_NOT_HANDLED) {
    LTRACE_ENTRY_OBJ;

    id_ = process->GetNextThreadId();

    list_clear_node(&node_);
    mutex_init(&exception_lock_);
    cond_init(&exception_wait_cond_);
    mutex_init(&exception_wait_lock_);
}

UserThread::~UserThread() {
    LTRACE_ENTRY_OBJ;

    process_->aspace()->FreeRegion(reinterpret_cast<vaddr_t>(user_stack_));
    cond_destroy(&exception_wait_cond_);
    mutex_destroy(&exception_wait_lock_);
}

status_t UserThread::Initialize(utils::StringPiece name) {
    LTRACE_ENTRY_OBJ;

    // Make sure we can hold process name and thread name combined.
    static_assert((MX_MAX_NAME_LEN * 2) == THREAD_NAME_LENGTH, "name length issue");

    char full_name[THREAD_NAME_LENGTH + 1];
    auto pname = process_->name();
    if ((pname.length() > 0) && (pname.length() < THREAD_NAME_LENGTH)) {
        snprintf(full_name, sizeof(full_name), "%s:%s", pname.data(), name.data());
    } else {
        snprintf(full_name, sizeof(full_name), "<unnamed>:%s", name.data());
    }

    // create an underlying LK thread
    thread_t* lkthread = thread_create_etc(&thread_, full_name, StartRoutine, this, LOW_PRIORITY,
                                           NULL, DEFAULT_STACK_SIZE);

    if (!lkthread) {
        TRACEF("error creating thread\n");
        return ERR_NO_MEMORY;
    }
    DEBUG_ASSERT(lkthread == &thread_);

    // set the per-thread pointer
    thread_.tls[TLS_ENTRY_LKUSER] = reinterpret_cast<uintptr_t>(this);

    // associate the proc's address space with this thread
    process_->aspace()->AttachToThread(lkthread);

    return NO_ERROR;
}

void UserThread::Start() {
    LTRACE_ENTRY_OBJ;

    process_->AddThread(this);
    thread_resume(&thread_);
}

void UserThread::Exit() {
    LTRACE_ENTRY_OBJ;

    waiter_.Signal(MX_SIGNAL_SIGNALED);

    thread_exit(0);
}

void UserThread::Detach() {
    LTRACE_ENTRY_OBJ;

    process_->ThreadDetached(this);
}

int UserThread::StartRoutine(void* arg) {
    LTRACE_ENTRY;

    UserThread* t = (UserThread*)arg;

    // create a user stack for the new thread
    auto err = t->process_->aspace()->Alloc("user stack", kDefaultStackSize, &t->user_stack_, PAGE_SIZE_SHIFT, 0,
                                            ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_NO_EXECUTE);
    LTRACEF("alloc returns %d, stack at %p\n", err, t->user_stack_);

    LTRACEF("arch_enter_uspace SP: %p PC: %p\n", t->user_stack_, t->entry_);
    // switch to user mode and start the process
    arch_enter_uspace(reinterpret_cast<vaddr_t>(t->entry_),
                      reinterpret_cast<uintptr_t>(t->user_stack_) + kDefaultStackSize, t->arg_);

    __UNREACHABLE;
}

status_t UserThread::SetExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour) {
    AutoLock lock(&exception_lock_);

    exception_handler_ = handler;
    exception_behaviour_ = behaviour;

    return NO_ERROR;
}

utils::RefPtr<Dispatcher> UserThread::exception_handler() {
    AutoLock lock(&exception_lock_);
    return exception_handler_;
}

static status_t send_exception_report(utils::RefPtr<Dispatcher> dispatcher, const mx_exception_report_t* report) {
    LTRACE_ENTRY;

    utils::Array<uint8_t> data;
    utils::Array<Handle*> handles;

    uint8_t* report_bytes = new uint8_t[sizeof(*report)];
    memcpy(report_bytes, report, sizeof(*report));
    data.reset(report_bytes, sizeof(*report));

    // TODO(cpu): We should rather deal RefPtr<MessagePipeDispatcher> in the exception code.
    auto message_pipe = dispatcher->get_message_pipe_dispatcher();
    DEBUG_ASSERT(message_pipe);

    status_t status = message_pipe->Write(utils::move(data), utils::move(handles));
    if (status != NO_ERROR)
        LTRACEF("dispatcher->Write returned %d\n", status);
    return status;
}

status_t UserThread::WaitForExceptionHandler(utils::RefPtr<Dispatcher> dispatcher, const mx_exception_report_t* report) {
    LTRACE_ENTRY_OBJ;
    AutoLock lock(&exception_wait_lock_);
    // Status if handler disappears.
    exception_status_ = MX_EXCEPTION_STATUS_NOT_HANDLED;
    // Send message, wait for reply.
    status_t status = send_exception_report(dispatcher, report);
    if (status != NO_ERROR) {
        LTRACEF("send_exception_report returned %d\n", status);
        return status;
    }
    status = cond_wait_timeout(&exception_wait_cond_, &exception_wait_lock_, INFINITE_TIME);
    DEBUG_ASSERT(status == NO_ERROR);
    if (exception_status_ != MX_EXCEPTION_STATUS_RESUME)
        return ERR_GENERIC;
    return NO_ERROR;
}

void UserThread::WakeFromExceptionHandler(mx_exception_status_t status) {
    LTRACE_ENTRY_OBJ;
    AutoLock lock(&exception_wait_lock_);
    exception_status_ = status;
    cond_signal(&exception_wait_cond_);
}
