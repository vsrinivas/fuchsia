// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-internal.h"

#include "devhost.h"
#include <fbl/auto_call.h>

zx_status_t zx_device::Create(fbl::RefPtr<zx_device>* out_dev) {
    *out_dev = fbl::AdoptRef(new zx_device());
    return ZX_OK;
}

// We must disable thread-safety analysis due to not being able to statically
// guarantee the lock holding invariant.  Instead, we acquire the lock if
// it's not already being held by the current thread.
void zx_device::fbl_recycle() TA_NO_THREAD_SAFETY_ANALYSIS {
    bool acq_lock = !devmgr::DM_LOCK_HELD();
    if (acq_lock) {
        devmgr::DM_LOCK();
    }
    auto unlock = fbl::MakeAutoCall([acq_lock]() TA_NO_THREAD_SAFETY_ANALYSIS {
        if (acq_lock) {
            devmgr::DM_UNLOCK();
        }
    });

    if (this->flags & DEV_FLAG_INSTANCE) {
        // these don't get removed, so mark dead state here
        this->flags |= DEV_FLAG_DEAD | DEV_FLAG_VERY_DEAD;
    }
    if (this->flags & DEV_FLAG_BUSY) {
        // this can happen if creation fails
        // the caller to device_add() will free it
        printf("device: %p(%s): ref=0, busy, not releasing\n", this, this->name);
        return;
    }
#if TRACE_ADD_REMOVE
    printf("device: %p(%s): ref=0. releasing.\n", this, this->name);
#endif

    if (!(this->flags & DEV_FLAG_VERY_DEAD)) {
        printf("device: %p(%s): only mostly dead (this is bad)\n", this, this->name);
    }
    if (!this->children.is_empty()) {
        printf("device: %p(%s): still has children! not good.\n", this, this->name);
    }

    this->event.reset();
    this->local_event.reset();

    // Put on the defered work list for finalization
    devmgr::defer_device_list.push_back(this);

    // Immediately finalize if there's not an active enumerator
    if (devmgr::devhost_enumerators == 0) {
        devmgr::devhost_finalize();
    }
}
