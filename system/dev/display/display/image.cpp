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

    controller_->ReleaseImage(this);
}

void Image::StartPresent() {
    presenting_ = true;
}

void Image::StartRetire() {
    if (!presenting_) {
        fbl::atomic_store(&in_use_, false);
    } else {
        retiring_ = true;
    }
}

void Image::OnRetire() {
    presenting_ = false;

    if (retiring_) {
        fbl::atomic_store(&in_use_, false);
    }
}

void Image::DiscardAcquire() {
    fbl::atomic_store(&in_use_, false);
}

bool Image::Acquire() {
    return !fbl::atomic_exchange(&in_use_, true);
}

} // namespace display
