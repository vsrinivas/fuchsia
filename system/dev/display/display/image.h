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
    // Marks the image as not in use. Should only be called after Acquire if no
    // other methods have been called.
    void DiscardAcquire();

    // Called when the image is passed to the display hardware.
    void StartPresent();
    // Called when another image is presented after this one.
    void StartRetire();

    // Called on vsync after StartRetire has been called.
    void OnRetire();
private:
    image_t info_;
    Controller* controller_;

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
