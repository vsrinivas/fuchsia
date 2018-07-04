// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client.h"
#include "fence.h"

namespace display {

bool Fence::CreateRef() {
    fbl::AllocChecker ac;
    cur_ref_ = fbl::AdoptRef(new (&ac) FenceReference(fbl::RefPtr<Fence>(this)));
    if (ac.check()) {
        ref_count_++;
    }

    return ac.check();
}

void Fence::ClearRef() {
    cur_ref_ = nullptr;
}

fbl::RefPtr<FenceReference> Fence::GetReference() {
    return cur_ref_;
}

void Fence::Signal() {
    event_.signal(0, ZX_EVENT_SIGNALED);
}

bool Fence::OnRefDead() {
    return --ref_count_ == 0;
}

zx_status_t Fence::OnRefArmed(fbl::RefPtr<FenceReference>&& ref) {
    if (armed_refs_.is_empty()) {
        ready_wait_.set_object(event_.get());
        ready_wait_.set_trigger(ZX_EVENT_SIGNALED);

        zx_status_t status = ready_wait_.Begin(dispatcher_);
        if (status != ZX_OK) {
            return status;
        }
    }

    armed_refs_.push_back(fbl::move(ref));
    return ZX_OK;
}

void Fence::OnRefDisarmed(FenceReference* ref) {
    armed_refs_.erase(*ref);
    if (armed_refs_.is_empty()) {
        ready_wait_.Cancel();
    }
}

void Fence::OnReady(async_dispatcher_t* dispatcher, async::WaitBase* self,
                    zx_status_t status, const zx_packet_signal_t* signal) {
    ZX_DEBUG_ASSERT(status == ZX_OK && (signal->observed & ZX_EVENT_SIGNALED));

    event_.signal(ZX_EVENT_SIGNALED, 0);

    fbl::RefPtr<FenceReference> ref = armed_refs_.pop_front();
    ref->OnReady();
    cb_->OnFenceFired(ref.get());

    if (!armed_refs_.is_empty()) {
        ready_wait_.Begin(dispatcher_);
    }
}

Fence::Fence(FenceCallback* cb, async_dispatcher_t* dispatcher, uint64_t fence_id, zx::event&& event)
        : cb_(cb), dispatcher_(dispatcher), event_(fbl::move(event)) {
    id = fence_id;
}

Fence::~Fence() {
    ZX_DEBUG_ASSERT(armed_refs_.is_empty());
    ZX_DEBUG_ASSERT(ref_count_ == 0);
}

zx_status_t FenceReference::StartReadyWait() {
    return fence_->OnRefArmed(fbl::RefPtr<FenceReference>(this));
}

void FenceReference::ResetReadyWait() {
    fence_->OnRefDisarmed(this);
}

void FenceReference::SetImmediateRelease(fbl::RefPtr<FenceReference>&& fence) {
    release_fence_ = fbl::move(fence);
}

void FenceReference::OnReady() {
    if (release_fence_) {
        release_fence_->Signal();
        release_fence_ = nullptr;
    }
}

void FenceReference::Signal() {
    fence_->Signal();
}

FenceReference::FenceReference(fbl::RefPtr<Fence> fence) : fence_(fbl::move(fence)) { }

FenceReference::~FenceReference() {
    fence_->cb_->OnRefForFenceDead(fence_.get());
}

} // namespace display
