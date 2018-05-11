// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/display-controller.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/zx/vmo.h>

#include "display/c/fidl.h"
#include "fence.h"
#include "id-map.h"

namespace display {

class Controller;

class Image : public fbl::RefCounted<Image>,
        public IdMappable<fbl::RefPtr<Image>>,
        public fbl::DoublyLinkedListable<fbl::RefPtr<Image>> {
public:
    Image(Controller* controller, const image_t& info, zx::vmo vmo);
    ~Image();

    image_t& info() { return info_; }
    // Marks the image as in use.
    bool Acquire();
    // Marks the image as not in use. Should only be called before PrepareFences.
    void DiscardAcquire();
    // Called to set this image's fences and prepare the image to be displayed.
    void PrepareFences(fbl::RefPtr<FenceReference>&& wait, fbl::RefPtr<FenceReference>&& present,
                       fbl::RefPtr<FenceReference>&& signal);
    // Called to immedately retire the image if StartPresent hasn't been called yet.
    void EarlyRetire();
    // Called when the image is passed to the display hardware.
    void StartPresent();
    // Called on vsync when the image is presented.
    void OnPresent();
    // Called when another image is presented after this one.
    void StartRetire();
    // Called on vsync after StartRetire has been called.
    void OnRetire();

    // Called on all waiting images when any fence fires.
    void OnFenceReady(FenceReference* fence);

    bool IsReady() const { return wait_fence_ == nullptr; }

    const zx::vmo& vmo() { return vmo_; }
private:
    image_t info_;
    Controller* controller_;

    fbl::RefPtr<FenceReference> wait_fence_ = nullptr;
    fbl::RefPtr<FenceReference> present_fence_ = nullptr;
    fbl::RefPtr<FenceReference> signal_fence_ = nullptr;
    // See comment in ::OnRetire for why this is necessary
    fbl::RefPtr<FenceReference> armed_signal_fence_ = nullptr;

    // Flag which indicates that the image is currently in some display configuration.
    fbl::atomic_bool in_use_ = {};
    // Flag indicating that the image is being managed by the display hardware.
    bool presenting_ = false;
    // Flag indicating that the image has started the process of retiring and will be free after
    // the next vsync. This is distinct from presenting_ due to multiplexing the display between
    // multiple clients.
    bool retiring_ = false;

    const zx::vmo vmo_;
};

} // namespace display
