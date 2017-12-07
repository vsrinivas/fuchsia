// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/handle_reaper.h>

#include <assert.h>
#include <inttypes.h>
#include <lib/dpc.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <object/dispatcher.h>
#include <object/handle.h>
#include <trace.h>

#define LOCAL_TRACE 0

fbl::Mutex HandleReaper::mutex;
fbl::DoublyLinkedList<Handle*> HandleReaper::pending;
dpc_t HandleReaper::dpc = {
    .node = LIST_INITIAL_CLEARED_VALUE,
    .func = &HandleReaper::ReaperRoutine,
    .arg = nullptr,
};

void HandleReaper::Reap(fbl::DoublyLinkedList<Handle*>* handles) {
    LTRACE_ENTRY;
    fbl::AutoLock lock(&mutex);
    pending.splice(pending.end(), *handles);
    dpc_queue(&dpc, false);
    LTRACE_EXIT;
}

void HandleReaper::Reap(Handle** handles, uint32_t num_handles) {
    LTRACE_ENTRY;
    fbl::DoublyLinkedList<Handle*> list;
    for (uint32_t i = 0; i < num_handles; i++)
        list.push_back(handles[i]);
    Reap(&list);
    LTRACE_EXIT;
}

void HandleReaper::ReaperRoutine(dpc_t*) {
    LTRACE_ENTRY;
    fbl::DoublyLinkedList<Handle*> list;
    {
        fbl::AutoLock lock(&mutex);
        list.swap(pending);
    }
    Handle* handle;
    while ((handle = list.pop_front()) != nullptr) {
        LTRACEF("Reaping handle of koid %" PRIu64 " of pid %" PRIu64 "\n",
                handle->dispatcher()->get_koid(), handle->process_id());
        DEBUG_ASSERT(handle->process_id() == 0u);
        handle->Delete();
    }
    LTRACE_EXIT;
}
