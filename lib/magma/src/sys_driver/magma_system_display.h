// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_DISPLAY_H_
#define _MAGMA_SYSTEM_DISPLAY_H_

#include "magma_system.h"
#include "magma_system_buffer.h"
#include "magma_system_display_abi.h"
#include "magma_util/macros.h"

#include <map>
#include <memory>

class MagmaSystemDisplay : public magma_system_display {
public:
    class Owner {
    public:
        virtual std::shared_ptr<MagmaSystemBuffer> GetBufferForToken(uint32_t token) = 0;
        virtual void PageFlip(std::shared_ptr<MagmaSystemBuffer> buf,
                              magma_system_pageflip_callback_t callback, void* data) = 0;
    };

    MagmaSystemDisplay(Owner* owner);

    // Imports the buffer for the given token and adds it to the buffer map.
    // If successful, |handle_out| is filled with the handle of the imported
    // buffer
    bool ImportBuffer(uint32_t token, uint32_t* handle_out);
    // Attempts to locate a buffer by handle in the buffer map and return it.
    // Returns nullptr if the buffer is not found
    std::shared_ptr<MagmaSystemBuffer> LookupBuffer(uint32_t handle);
    // This removes the reference to the shared_ptr in the map
    // Returns false if no buffer with the given handle exists in the map
    bool ReleaseBuffer(uint32_t handle);

    void PageFlip(std::shared_ptr<MagmaSystemBuffer> buf, magma_system_pageflip_callback_t callback,
                  void* data)
    {
        owner_->PageFlip(buf, callback, data);
    }

    static MagmaSystemDisplay* cast(magma_system_display* display)
    {
        DASSERT(display);
        DASSERT(display->magic_ == kMagic);
        return static_cast<MagmaSystemDisplay*>(display);
    }

private:
    Owner* owner_;
    std::map<uint32_t, std::shared_ptr<MagmaSystemBuffer>> buffer_map_;

    static const uint32_t kMagic = 0x64697370; // "disp" (Display)
};

#endif //_MAGMA_SYSTEM_DISPLAY_H_