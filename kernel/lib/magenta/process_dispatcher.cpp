// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/process_dispatcher.h>

#include <err.h>
#include <trace.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultProcessRights = MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_TRANSFER;

status_t ProcessDispatcher::Create(utils::RefPtr<Dispatcher>* dispatcher,
                                        mx_rights_t* rights,
                                        utils::StringPiece name) {
    auto new_process = utils::AdoptRef(new UserProcess(name));
    if (!new_process)
        return ERR_NO_MEMORY;

    status_t result = new_process->Initialize();
    if (result != NO_ERROR)
        return result;

    *rights = kDefaultProcessRights;
    *dispatcher = utils::AdoptRef<Dispatcher>(new ProcessDispatcher(utils::move(new_process)));
    return (*dispatcher) ? NO_ERROR : ERR_NO_MEMORY;
}

ProcessDispatcher::ProcessDispatcher(utils::RefPtr<UserProcess> process)
    : process_(utils::move(process)) {
    LTRACE_ENTRY_OBJ;
}

ProcessDispatcher::~ProcessDispatcher() {
    LTRACE_ENTRY_OBJ;

    process_->DispatcherClosed();
}

StateTracker* ProcessDispatcher::get_state_tracker() {
    return process_->state_tracker();
}

status_t ProcessDispatcher::Start(mx_handle_t handle, mx_vaddr_t entry) {
    return process_->Start(reinterpret_cast<void*>(handle), entry);
}

mx_handle_t ProcessDispatcher::AddHandle(HandleUniquePtr handle) {
    auto hv = process_->MapHandleToValue(handle.get());
    process_->AddHandle(utils::move(handle));
    return hv;
}

status_t ProcessDispatcher::GetInfo(mx_process_info_t* info) {
    return process_->GetInfo(info);
}

status_t ProcessDispatcher::SetExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour) {
    return process_->SetExceptionHandler(handler, behaviour);
}
