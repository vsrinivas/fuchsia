// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/async/cpp/wait.h>
#include <threads.h>
#include <lib/zx/event.h>

#include "id-map.h"

namespace display {

class FenceReference;
class Fence;

class FenceCallback {
public:
    virtual void OnFenceFired(FenceReference* ref) = 0;
    virtual void OnRefForFenceDead(Fence* fence) = 0;
};

// Class which wraps an event into a fence. A single Fence can have multiple FenceReference
// objects, which allows an event to be treated as a semaphore independently of it being
// imported/released (i.e. can be released while still in use).
class Fence : public fbl::RefCounted<Fence>, public IdMappable<fbl::RefPtr<Fence>> {
public:
    Fence(FenceCallback* cb, async_t* async, uint64_t id, zx::event&& event);
    ~Fence();

    // Creates a new FenceReference when an event is imported.
    bool CreateRef();
    // Clears a FenceReference when an event is released. Note that references to the cleared
    // FenceReference might still exist within the driver.
    void ClearRef();
    // Decrements the reference count and returns true if the last ref died.
    bool OnRefDead();

    // Gets the fence reference for the current import. An individual fence reference cannot
    // be used for multiple things simultaniously.
    fbl::RefPtr<FenceReference> GetReference();
private:
    void Signal();
    void OnRefDied();
    zx_status_t OnRefArmed(fbl::RefPtr<FenceReference>&& ref);
    void OnRefDisarmed(FenceReference* ref);

    // The fence reference corresponding to the current event import.
    fbl::RefPtr<FenceReference> cur_ref_;

    // A queue of fence references which are being waited upon. When the event is
    // signaled, the signal will be cleared and the first fence ref will be marked ready.
    fbl::DoublyLinkedList<fbl::RefPtr<FenceReference>> armed_refs_;

    void OnReady(async_t* async, async::WaitBase* self,
                 zx_status_t status, const zx_packet_signal_t* signal);
    async::WaitMethod<Fence, &Fence::OnReady> ready_wait_{this};

    FenceCallback* cb_;
    async_t* async_;
    zx::event event_;
    int ref_count_ = 0;

    friend FenceReference;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Fence);
};

class FenceReference : public fbl::RefCounted<FenceReference>
                     , public fbl::DoublyLinkedListable<fbl::RefPtr<FenceReference>> {
public:
    explicit FenceReference(fbl::RefPtr<Fence> fence);
    ~FenceReference();

    void Signal();

    zx_status_t StartReadyWait();
    void ResetReadyWait();
    // Sets the fence which will be signaled immedately when this fence is ready.
    void SetImmediateRelease(fbl::RefPtr<FenceReference>&& fence);

    void OnReady();
private:
    fbl::RefPtr<Fence> fence_;

    fbl::RefPtr<FenceReference> release_fence_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(FenceReference);
};

} // namespace display
