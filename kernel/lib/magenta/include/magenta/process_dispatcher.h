// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/vm/vm_aspace.h>

#include <magenta/dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/user_process.h>

#include <utils/unique_ptr.h>

#include <sys/types.h>

class UserProcess;

class ProcessDispatcher : public Dispatcher {
public:
    static status_t Create(utils::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights, utils::StringPiece name);

    // $$$ move to private?
    explicit ProcessDispatcher(utils::RefPtr<UserProcess> process);

    virtual ~ProcessDispatcher() final;
    mx_obj_type_t GetType() const final { return MX_OBJ_TYPE_PROCESS; }
    ProcessDispatcher* get_process_dispatcher() final { return this; }

    StateTracker* get_state_tracker() final;
    status_t Start(mx_handle_t handle, mx_vaddr_t pc_vaddr);
    mx_handle_t AddHandle(HandleUniquePtr handle);
    utils::RefPtr<VmAspace> GetVmAspace() { return process_->aspace(); }
    status_t GetInfo(mx_process_info_t* info);
    status_t SetExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour);

private:
    utils::RefPtr<UserProcess> process_;
};
