// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <zircon/types.h>
#include <zx/vmo.h>

namespace video {
namespace usb {

class VideoBuffer {

public:
    // Creates a VideoBuffer with the given VMO buffer handle.
    // If successful, returns ZX_OK and a pointer to the created
    // VideoBuffer will be stored in out.
    static zx_status_t Create(zx::vmo&& vmo,
                              fbl::unique_ptr<VideoBuffer>* out);

    uint64_t size() const { return size_; }
    void*    virt() const { return virt_; }

    ~VideoBuffer();

private:
    VideoBuffer(zx::vmo&& vmo, uint64_t size, void* virt)
        : vmo_(fbl::move(vmo)),
          size_(size),
          virt_(virt) {}

    // VMO backing the video buffer.
    zx::vmo vmo_;
    // Size of the VMO.
    uint64_t size_ = 0;
    // The mapped address of the start of the video buffer.
    void* virt_ = nullptr;

    // TODO(jocelyndang): add methods for getting, locking and releasing frames.
};

} // namespace usb
} // namespace video
