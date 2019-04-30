// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/rights.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <object/dispatcher.h>
#include <object/handle.h>

#include <sys/types.h>

class SuspendTokenDispatcher final :
    public SoloDispatcher<SuspendTokenDispatcher, ZX_DEFAULT_SUSPEND_TOKEN_RIGHTS> {
public:
    // Creates a new token which suspends |task|.
    //
    // Returns:
    //   ZX_OK on success
    //   ZX_ERR_NO_MEMORY if the token could not be allocated
    //   ZX_ERR_WRONG_TYPE if |task| is not a supported type
    //   ZX_ERR_NOT_SUPPORTED if |task| is trying to suspend itself
    static zx_status_t Create(fbl::RefPtr<Dispatcher> task,
                              KernelHandle<SuspendTokenDispatcher>* handle,
                              zx_rights_t* rights);

    ~SuspendTokenDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_SUSPEND_TOKEN; }
    void on_zero_handles() final;

private:
    SuspendTokenDispatcher();

    // A lock annotation is unnecessary because the only time |task_| is used is on_zero_handles()
    // and Create(), and the object can only get zero handles once.
    fbl::RefPtr<Dispatcher> task_;
};
