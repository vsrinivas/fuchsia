// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/dpc.h>
#include <kernel/mutex.h>
#include <magenta/dispatcher.h>
#include <magenta/handle_reaper.h>
#include <trace.h>

#define LOCAL_TRACE 0

static void ReaperRoutine(dpc_t* dpc);

static Mutex reaper_mutex;
static mxtl::DoublyLinkedList<Handle*> reaper_handles TA_GUARDED(reaper_mutex);
static dpc_t reaper_dpc = {
    .node = LIST_INITIAL_CLEARED_VALUE,
    .func = ReaperRoutine,
    .arg = nullptr,
};

void ReapHandles(mxtl::DoublyLinkedList<Handle*>* handles) {
    LTRACE_ENTRY;
    AutoLock lock(&reaper_mutex);
    reaper_handles.splice(reaper_handles.end(), *handles);
    if (!list_in_list(&reaper_dpc.node))
        dpc_queue(&reaper_dpc, false);
}

void ReapHandles(Handle** handles, uint32_t num_handles) {
    LTRACE_ENTRY;
    mxtl::DoublyLinkedList<Handle*> list;
    for (uint32_t i = 0; i < num_handles; i++)
        list.push_back(handles[i]);
    ReapHandles(&list);
}

static void ReaperRoutine(dpc_t* dpc) {
    LTRACE_ENTRY;
    mxtl::DoublyLinkedList<Handle*> list;
    {
        AutoLock lock(&reaper_mutex);
        list.swap(reaper_handles);
    }
    Handle* handle;
    while ((handle = list.pop_front()) != nullptr) {
        LTRACEF("Reaping handle of koid %" PRIu64 " of pid %" PRIu64 "\n",
                handle->dispatcher()->get_koid(), handle->process_id());
        DeleteHandle(handle);
    }
    LTRACE_EXIT;
;
}
