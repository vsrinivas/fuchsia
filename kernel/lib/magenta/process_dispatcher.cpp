// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/process_dispatcher.h>

#include <list.h>
#include <new.h>
#include <rand.h>
#include <string.h>
#include <trace.h>

#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>

#include <magenta/futex_context.h>
#include <magenta/magenta.h>
#include <magenta/user_copy.h>

#define LOCAL_TRACE 0

static constexpr mx_rights_t kDefaultProcessRights = MX_RIGHT_READ  |
                                                     MX_RIGHT_WRITE |
                                                     MX_RIGHT_TRANSFER;

uint32_t ProcessDispatcher::next_process_id_;     // .bss init'ed to 0
mutex_t ProcessDispatcher::global_process_list_mutex_ =
    MUTEX_INITIAL_VALUE(global_process_list_mutex_);
utils::DoublyLinkedList<ProcessDispatcher*> ProcessDispatcher::global_process_list_;

mx_status_t ProcessDispatcher::Create(utils::StringPiece name,
                                      utils::RefPtr<Dispatcher>* dispatcher,
                                      mx_rights_t* rights) {
    AllocChecker ac;
    auto process = new (&ac) ProcessDispatcher(name);
    if (!ac.check())
        return ERR_NO_MEMORY;

    status_t result = process->Initialize();
    if (result != NO_ERROR)
        return result;

    *rights = kDefaultProcessRights;
    *dispatcher = utils::AdoptRef<Dispatcher>(process);
    return NO_ERROR;
}

ProcessDispatcher::ProcessDispatcher(utils::StringPiece name)
    : state_tracker_(true, mx_signals_state_t{0u, MX_SIGNAL_SIGNALED}) {
    LTRACE_ENTRY_OBJ;

    // Add ourself to the global process list, generating an ID at the same time.
    AddProcess(this);

    // Generate handle XOR mask with top bit and bottom two bits cleared
    handle_rand_ = (rand() << 2) & INT_MAX;

    if (name.length() > 0 && (name.length() < sizeof(name_)))
        strlcpy(name_, name.data(), sizeof(name_));
}

ProcessDispatcher::~ProcessDispatcher() {
    LTRACE_ENTRY_OBJ;
    Kill();

    DEBUG_ASSERT(state_ == State::INITIAL || state_ == State::DEAD);

    // assert that we have no handles, should have been cleaned up in the -> DEAD transition
    DEBUG_ASSERT(handles_.is_empty());

    // remove ourself from the global process list
    RemoveProcess(this);

    mutex_destroy(&state_lock_);
    mutex_destroy(&handle_table_lock_);
    mutex_destroy(&thread_list_lock_);

    LTRACE_EXIT_OBJ;
}

status_t ProcessDispatcher::Initialize() {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(state_lock_);

    DEBUG_ASSERT(state_ == State::INITIAL);

    // create an address space for this process.
    aspace_ = VmAspace::Create(0, nullptr);
    if (!aspace_) {
        TRACEF("error creating address space\n");
        return ERR_NO_MEMORY;
    }

    return NO_ERROR;
}

status_t ProcessDispatcher::Start(void* arg, mx_vaddr_t entry) {
    LTRACE_ENTRY_OBJ;

    // grab and hold the state lock across this entire routine, since we're
    // effectively transitioning from INITIAL to RUNNING
    AutoLock lock(state_lock_);

    DEBUG_ASSERT(state_ == State::INITIAL);

    // make sure we're in the right state
    if (state_ != State::INITIAL) {
        TRACEF("ProcessDispatcher has not been loaded\n");
        return ERR_BAD_STATE;
    }

    if (entry) {
        entry_ = (thread_start_routine)entry;
    }

    // TODO: move the creation of the initial thread to user space
    status_t result;
    // create the first thread
    utils::RefPtr<UserThread> t;
    result = CreateUserThread("main", entry_, arg, &t);
    if (result != NO_ERROR) {
        SetState(State::DEAD);
        return result;
    }
    // we're ready to run now
    main_thread_ = t;
    SetState(State::RUNNING);

    LTRACEF("starting main thread\n");
    t->Start();

    return NO_ERROR;
}

void ProcessDispatcher::Exit(int retcode) {
    LTRACE_ENTRY_OBJ;

    {
        AutoLock lock(state_lock_);

        DEBUG_ASSERT(state_ == State::RUNNING);

        retcode_ = retcode;

        // enter the dying state, which should kill all threads
        SetState(State::DYING);
    }

    UserThread::GetCurrent()->Exit();
}

void ProcessDispatcher::Kill() {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(state_lock_);

    // we're already dead
    if (state_ == State::DEAD)
        return;

    // if we have no threads, enter the dead state directly
    if (thread_list_.is_empty()) {
        SetState(State::DEAD);
    } else {
        // enter the dying state, which should trigger a thread kill.
        // the last thread exiting will transition us to DEAD
        SetState(State::DYING);
    }
}

void ProcessDispatcher::KillAllThreads() {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(&thread_list_lock_);

    for (auto& thread : thread_list_) {
        LTRACEF("killing thread %p\n", &thread);
        thread.Kill();
    };
}

status_t ProcessDispatcher::AddThread(UserThread* t) {
    LTRACE_ENTRY_OBJ;

    // cannot add thread to dying/dead state
    if (state_ == State::DYING || state_ == State::DEAD) {
        return ERR_BAD_STATE;
    }

    // add the thread to our list
    AutoLock lock(&thread_list_lock_);
    thread_list_.push_back(t);

    DEBUG_ASSERT(t->process() == this);

    return NO_ERROR;
}

void ProcessDispatcher::RemoveThread(UserThread* t) {
    LTRACE_ENTRY_OBJ;

    // we're going to check for state and possibly transition below
    AutoLock state_lock(&state_lock_);

    // remove the thread from our list
    AutoLock lock(&thread_list_lock_);
    thread_list_.erase(t);

    // drop the ref from the main_thread_ pointer if its being removed
    if (t == main_thread_.get()) {
        main_thread_.reset();
    }

    // if this was the last thread, transition directly to DEAD state
    if (thread_list_.is_empty()) {
        LTRACEF("last thread left the process %p, entering DEAD state\n", this);
        SetState(State::DEAD);
    }
}

void ProcessDispatcher::SetState(State s) {
    LTRACEF("process %p: state %u (%s)\n", this, static_cast<unsigned int>(s), StateToString(s));

    DEBUG_ASSERT(is_mutex_held(&state_lock_));

    // look for some invalid state transitions
    if (state_ == State::DEAD && s != State::DEAD) {
        panic("ProcessDispatcher::SetState invalid state transition from DEAD to !DEAD\n");
        return;
    }

    // transitions to your own state are okay
    if (s == state_)
        return;

    state_ = s;

    if (s == State::DYING) {
        // send kill to all of our threads
        KillAllThreads();
    } else if (s == State::DEAD) {
        // clean up the handle table
        LTRACEF_LEVEL(2, "cleaning up handle table on proc %p\n", this);
        {
            AutoLock lock(&handle_table_lock_);
            Handle* handle;
            while ((handle = handles_.pop_front()) != nullptr) {
                DeleteHandle(handle);
            };
        }
        LTRACEF_LEVEL(2, "done cleaning up handle table on proc %p\n", this);

        // tear down the address space
        aspace_->Destroy();

        // signal waiter
        LTRACEF_LEVEL(2, "signalling waiters\n");
        state_tracker_.UpdateSatisfied(MX_SIGNAL_SIGNALED, 0u);
    }
}

// process handle manipulation routines
mx_handle_t ProcessDispatcher::MapHandleToValue(Handle* handle) {
    auto handle_index = MapHandleToU32(handle) + 1;
    return handle_index ^ handle_rand_;
}

Handle* ProcessDispatcher::GetHandle_NoLock(mx_handle_t handle_value) {
    auto handle_index = (handle_value ^ handle_rand_) - 1;
    Handle* handle = MapU32ToHandle(handle_index);
    if (!handle)
        return nullptr;
    return (handle->process_id() == id_) ? handle : nullptr;
}

void ProcessDispatcher::AddHandle(HandleUniquePtr handle) {
    AutoLock lock(&handle_table_lock_);
    AddHandle_NoLock(utils::move(handle));
}

void ProcessDispatcher::AddHandle_NoLock(HandleUniquePtr handle) {
    handle->set_process_id(id_);
    handles_.push_front(handle.release());
}

HandleUniquePtr ProcessDispatcher::RemoveHandle(mx_handle_t handle_value) {
    AutoLock lock(&handle_table_lock_);
    return RemoveHandle_NoLock(handle_value);
}

HandleUniquePtr ProcessDispatcher::RemoveHandle_NoLock(mx_handle_t handle_value) {
    auto handle = GetHandle_NoLock(handle_value);
    if (!handle)
        return nullptr;
    handles_.erase(handle);
    handle->set_process_id(0u);

    return HandleUniquePtr(handle);
}

void ProcessDispatcher::UndoRemoveHandle_NoLock(mx_handle_t handle_value) {
    auto handle_index = (handle_value ^ handle_rand_) - 1;
    Handle* handle = MapU32ToHandle(handle_index);
    AddHandle_NoLock(HandleUniquePtr(handle));
}

bool ProcessDispatcher::GetDispatcher(mx_handle_t handle_value,
                                      utils::RefPtr<Dispatcher>* dispatcher,
                                      uint32_t* rights) {
    AutoLock lock(&handle_table_lock_);
    Handle* handle = GetHandle_NoLock(handle_value);
    if (!handle)
        return false;

    *rights = handle->rights();
    *dispatcher = handle->dispatcher();
    return true;
}

status_t ProcessDispatcher::GetInfo(mx_process_info_t* info) {
    info->return_code = retcode_;

    return NO_ERROR;
}

status_t ProcessDispatcher::CreateUserThread(utils::StringPiece name,
                                             thread_start_routine entry, void* arg,
                                             utils::RefPtr<UserThread>* user_thread) {
    AllocChecker ac;
    auto ut = utils::AdoptRef(new (&ac) UserThread(GenerateKernelObjectId(),
                                                   utils::RefPtr<ProcessDispatcher>(this),
                                                   entry, arg));
    if (!ac.check())
        return ERR_NO_MEMORY;

    status_t result = ut->Initialize(name);
    if (result != NO_ERROR)
        return result;

    *user_thread = utils::move(ut);
    return NO_ERROR;
}

mx_tid_t ProcessDispatcher::GetNextThreadId() {
    return atomic_add(&next_thread_id_, 1);
}

status_t ProcessDispatcher::SetExceptionHandler(utils::RefPtr<Dispatcher> handler,
                                                mx_exception_behaviour_t behaviour) {
    AutoLock lock(&exception_lock_);

    exception_handler_ = handler;
    exception_behaviour_ = behaviour;

    return NO_ERROR;
}

utils::RefPtr<Dispatcher> ProcessDispatcher::exception_handler() {
    AutoLock lock(&exception_lock_);
    return exception_handler_;
}

uint32_t ProcessDispatcher::HandleStats(uint32_t* handle_type, size_t size) const {
    AutoLock lock(&handle_table_lock_);
    uint32_t total = 0;
    for (const auto& handle : handles_) {
        if (handle_type) {
            uint32_t type = static_cast<uint32_t>(handle.dispatcher()->GetType());
            if (size > type)
                ++handle_type[type];
        }
        ++total;
    }
    return total;
}

uint32_t ProcessDispatcher::ThreadCount() const {
    AutoLock lock(&thread_list_lock_);
    return static_cast<uint32_t>(thread_list_.size_slow());
}

void ProcessDispatcher::AddProcess(ProcessDispatcher* process) {
    // Don't call any method of |process|, it is not yet fully constructed.
    AutoLock lock(&global_process_list_mutex_);

    process->id_ = ++next_process_id_;
    global_process_list_.push_back(process);

    LTRACEF("Adding process %p : id = %u\n", process, process->id_);
}

void ProcessDispatcher::RemoveProcess(ProcessDispatcher* process) {
    AutoLock lock(&global_process_list_mutex_);

    global_process_list_.erase(process);
    LTRACEF("Removing process %p : id = %u\n", process, process->id());
}

void ProcessDispatcher::DebugDumpProcessList() {
    AutoLock lock(&global_process_list_mutex_);
    printf(" id-s  #t  #h:  #pr #th #vm #mp #ev #ip #dp #it #io[name]\n");

    for (const auto& process : global_process_list_) {
        printf("%3u-%c %3u %s [%s]\n",
               process.id(),
               process.StateChar(),
               process.ThreadCount(),
               process.DebugDumpHandleTypeCount_NoLock(),
               process.name().data());
    }
}

char* ProcessDispatcher::DebugDumpHandleTypeCount_NoLock() const {
    static char buf[(MX_OBJ_TYPE_LAST * 4) + 1];

    uint32_t types[MX_OBJ_TYPE_LAST] = {0};
    uint32_t handle_count = HandleStats(types, sizeof(types));

    snprintf(buf, sizeof(buf), "%3u: %3u %3u %3u %3u %3u %3u %3u %3u %3u",
             handle_count,
             types[1],              // process.
             types[2],              // thread.
             types[3],              // vmem
             types[4],              // msg pipe.
             types[5],              // event
             types[6],              // ioport.
             types[7] + types[8],   // data pipe (both),
             types[9],              // interrupt.
             types[10]              // io map
             );
    return buf;
}

void ProcessDispatcher::DumpProcessListKeyMap() {
    printf("id  : process id number\n");
    printf("-s  : state: R = running D = dead\n");
    printf("#t  : number of threads\n");
    printf("#h  : total number of handles\n");
    printf("#pr : number of process handles\n");
    printf("#th : number of thread handles\n");
    printf("#vm : number of vm map handles\n");
    printf("#mp : number of message pipe handles\n");
    printf("#ev : number of event handles\n");
    printf("#ip : number of io port handles\n");
    printf("#dp : number of data pipe handles (both)\n");
    printf("#it : number of interrupt handles\n");
    printf("#io : number of io map handles\n");
}

char ProcessDispatcher::StateChar() const {
    State s = state();

    switch (s) {
    case State::INITIAL:
        return 'I';
    case State::RUNNING:
        return 'R';
    case State::DYING:
        return 'Y';
    case State::DEAD:
        return 'D';
    }
    return '?';
}

const char* StateToString(ProcessDispatcher::State state) {
    switch (state) {
    case ProcessDispatcher::State::INITIAL:
        return "initial";
    case ProcessDispatcher::State::RUNNING:
        return "running";
    case ProcessDispatcher::State::DYING:
        return "dying";
    case ProcessDispatcher::State::DEAD:
        return "dead";
    }
    return "unknown";
}
