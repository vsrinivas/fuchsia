// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/intrusive_double_list.h>
#include <lib/dpc.h>
#include <fbl/mutex.h>
#include <object/handle.h>

// Delete handles out-of-band, using a deferred procedure call.
class HandleReaper {
public:
    static void Reap(fbl::DoublyLinkedList<Handle*>* handles);
    static void Reap(Handle** handles, uint32_t num_handles);

private:
    static fbl::Mutex mutex;
    static fbl::DoublyLinkedList<Handle*> pending TA_GUARDED(mutex);
    static dpc_t dpc;

    static void ReaperRoutine(dpc_t*);
};
