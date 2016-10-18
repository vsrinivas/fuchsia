// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_DISPLAY_H_
#define _MAGMA_SYSTEM_DISPLAY_H_

#include "magma_system_buffer.h"
#include "magma_system_buffer_manager.h"
#include "magma_system_display_abi.h"
#include "magma_util/macros.h"

#include <memory>

class MagmaSystemDisplay : public MagmaSystemBufferManager, public magma_system_display {
public:
    class Owner : virtual public MagmaSystemBufferManager::Owner {
    public:
        virtual void PageFlip(std::shared_ptr<MagmaSystemBuffer> buf,
                              magma_system_pageflip_callback_t callback, void* data) = 0;
    };

    MagmaSystemDisplay(Owner* owner);

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
    static const uint32_t kMagic = 0x64697370; // "disp" (Display)
};

#endif //_MAGMA_SYSTEM_DISPLAY_H_