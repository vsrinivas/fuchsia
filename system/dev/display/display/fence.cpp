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

        zx_status_t status = ready_wait_.Begin(async_);
        if (status != ZX_OK) {
            return status;
        }
    }

    armed_refs_.push_back(fbl::move(ref));
    return ZX_OK;
}

void Fence::OnReady(async_t* async, async::WaitBase* self,
                    zx_status_t status, const zx_packet_signal_t* signal) {
    ZX_DEBUG_ASSERT(status == ZX_OK && (signal->observed & ZX_EVENT_SIGNALED));

    event_.signal(ZX_EVENT_SIGNALED, 0);

    fbl::RefPtr<FenceReference> ref = armed_refs_.pop_front();
    ref->OnReady();
    cb_->OnFenceFired(ref.get());

    if (!armed_refs_.is_empty()) {
        ready_wait_.Begin(async_);
    }
}

// Fences need to be manually reset because of circular references in fences that are
// being waited upon. Signal fences could still exist after a fence is reset.
void Fence::Reset() {
    // The only time a Fence is destroyed with outstanding armed refs is
    // when the client is torn down, in which case dropping events is fine.
    if (!armed_refs_.is_empty()) {
        ready_wait_.Cancel();
        armed_refs_.clear();
    }
    cur_ref_ = nullptr;
}

Fence::Fence(FenceCallback* cb, async_t* async, int32_t fence_id, zx::event&& event)
        : cb_(cb), async_(async), event_(fbl::move(event)) {
    id = fence_id;
}

Fence::~Fence() {
    ZX_DEBUG_ASSERT(ref_count_ == 0);
}

zx_status_t FenceReference::StartReadyWait() {
    return fence_->OnRefArmed(fbl::RefPtr<FenceReference>(this));
}

void FenceReference::SetImmediateRelease(fbl::RefPtr<FenceReference>&& fence1,
                                         fbl::RefPtr<FenceReference>&& fence2) {
    release_fence1_ = fbl::move(fence1);
    release_fence2_ = fbl::move(fence2);
}

void FenceReference::OnReady() {
    if (release_fence1_) {
        release_fence1_->Signal();
        release_fence1_ = nullptr;
    }
    if (release_fence2_) {
        release_fence2_->Signal();
        release_fence2_ = nullptr;
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
