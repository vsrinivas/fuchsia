// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/handle_reaper.h>

#include <assert.h>
#include <inttypes.h>
#include <lib/dpc.h>
#include <magenta/dispatcher.h>
#include <magenta/magenta.h>
#include <mxtl/auto_lock.h>
#include <mxtl/mutex.h>
#include <trace.h>

#define LOCAL_TRACE 0

static void ReaperRoutine(dpc_t* dpc);

static mxtl::Mutex reaper_mutex;
static mxtl::DoublyLinkedList<Handle*> reaper_handles TA_GUARDED(reaper_mutex);
static dpc_t reaper_dpc = {
    .node = LIST_INITIAL_CLEARED_VALUE,
    .func = ReaperRoutine,
    .arg = nullptr,
};

void ReapHandles(mxtl::DoublyLinkedList<Handle*>* handles) {
    LTRACE_ENTRY;
    mxtl::AutoLock lock(&reaper_mutex);
    reaper_handles.splice(reaper_handles.end(), *handles);
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
        mxtl::AutoLock lock(&reaper_mutex);
        list.swap(reaper_handles);
    }
    Handle* handle;
    while ((handle = list.pop_front()) != nullptr) {
        LTRACEF("Reaping handle of koid %" PRIu64 " of pid %" PRIu64 "\n",
                handle->dispatcher()->get_koid(), handle->process_id());
        DEBUG_ASSERT(handle->process_id() == 0u);
        DeleteHandle(handle);
    }
    LTRACE_EXIT;
;
}
