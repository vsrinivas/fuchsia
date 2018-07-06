// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include "controller.h"
#include "image.h"

namespace display {

Image::Image(Controller* controller, const image_t& image_config, zx::vmo handle)
        : info_(image_config), controller_(controller), vmo_(fbl::move(handle)) { }

Image::~Image() {
    ZX_DEBUG_ASSERT(!fbl::atomic_load(&in_use_));
    ZX_DEBUG_ASSERT(!list_in_list(&node.link));

    controller_->ReleaseImage(this);
}

void Image::PrepareFences(fbl::RefPtr<FenceReference>&& wait,
                          fbl::RefPtr<FenceReference>&& signal) {
    wait_fence_ = fbl::move(wait);
    signal_fence_ = fbl::move(signal);

    if (wait_fence_) {
        zx_status_t status = wait_fence_->StartReadyWait();
        if (status  != ZX_OK) {
            zxlogf(ERROR, "Failed to start waiting %d\n", status);
            // Mark the image as ready. Displaying garbage is better than hanging or crashing.
            wait_fence_ = nullptr;
        }
    }
}

void Image::OnFenceReady(FenceReference* fence) {
    if (wait_fence_.get() == fence) {
        wait_fence_ = nullptr;
    }
}

void Image::StartPresent() {
    ZX_DEBUG_ASSERT(wait_fence_ == nullptr);
    presenting_ = true;
}

void Image::EarlyRetire() {
    if (wait_fence_) {
        wait_fence_->SetImmediateRelease(fbl::move(signal_fence_));
        wait_fence_ = nullptr;
    }
    fbl::atomic_store(&in_use_, false);
}

void Image::StartRetire() {
    ZX_DEBUG_ASSERT(wait_fence_ == nullptr);

    if (!presenting_) {
        if (signal_fence_) {
            signal_fence_->Signal();
            signal_fence_ = nullptr;
        }
        fbl::atomic_store(&in_use_, false);
    } else {
        retiring_ = true;
        armed_signal_fence_ = fbl::move(signal_fence_);
    }
}

void Image::OnRetire() {
    presenting_ = false;

    if (retiring_) {
        // Retire and acquire are not synchronized, so set in_use_ before signaling so
        // that the image can be reused as soon as the event is signaled. We don't have
        // to worry about the armed signal fence being overwritten on reuse since it is
        // on set in StartRetire, which is called under the same lock as OnRetire.
        fbl::atomic_store(&in_use_, false);

        if (armed_signal_fence_) {
            armed_signal_fence_->Signal();
            armed_signal_fence_ = nullptr;
        }
        retiring_ = false;
    }
}

void Image::DiscardAcquire() {
    ZX_DEBUG_ASSERT(wait_fence_ == nullptr);

    fbl::atomic_store(&in_use_, false);
}

bool Image::Acquire() {
    return !fbl::atomic_exchange(&in_use_, true);
}

void Image::ResetFences() {
    if (wait_fence_) {
        wait_fence_->ResetReadyWait();
    }

    wait_fence_ = nullptr;
    armed_signal_fence_ = nullptr;
    signal_fence_ = nullptr;
}

} // namespace display
