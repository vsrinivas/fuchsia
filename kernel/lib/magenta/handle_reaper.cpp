// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/dpc.h>
#include <kernel/mutex.h>
#include <magenta/handle_reaper.h>

static void ReaperRoutine(dpc_t* dpc);

static mutex_t reaper_mutex = MUTEX_INITIAL_VALUE(reaper_mutex);
static mxtl::DoublyLinkedList<Handle*> reaper_handles TA_GUARDED(reaper_mutex);
static dpc_t reaper_dpc = {
    .node = LIST_INITIAL_CLEARED_VALUE,
    .func = ReaperRoutine,
    .arg = nullptr,
};

void ReapHandles(mxtl::DoublyLinkedList<Handle*>* handles) {
    AutoLock lock(reaper_mutex);
    reaper_handles.splice(reaper_handles.end(), *handles);
    if (!list_in_list(&reaper_dpc.node))
        dpc_queue(&reaper_dpc, false);
}

void ReapHandles(Handle** handles, uint32_t num_handles) {
    mxtl::DoublyLinkedList<Handle*> list;
    for (uint32_t i = 0; i < num_handles; i++)
        list.push_back(handles[i]);
    ReapHandles(&list);
}

static void ReaperRoutine(dpc_t* dpc) {
    mxtl::DoublyLinkedList<Handle*> list;
    {
        AutoLock lock(reaper_mutex);
        list.swap(reaper_handles);
    }
    Handle* handle;
    while ((handle = list.pop_front()) != nullptr)
        DeleteHandle(handle);
}
