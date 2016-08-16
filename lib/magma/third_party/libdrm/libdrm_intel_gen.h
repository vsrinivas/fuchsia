/**************************************************************************
 *
 * Copyright 2016 The Chromium Authors.
 * Copyright © 2007 Red Hat Inc.
 * Copyright © 2007-2012 Intel Corporation
 * Copyright 2006 Tungsten Graphics, Inc., Bismarck, ND., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 *          Keith Whitwell <keithw-at-tungstengraphics-dot-com>
 *      Eric Anholt <eric@anholt.net>
 *      Dave Airlie <airlied@linux.ie>
 */

#ifndef LIBDRM_INTEL_GEN_H_
#define LIBDRM_INTEL_GEN_H_

#include "magma_util/refcounted.h"
#include "msd_defs.h"
#include <stdint.h>
#include <vector>

class LibdrmIntelGen {
public:
    struct RelocTargetInfo;
    class BufferRefcount;

    class Buffer : public magma_buffer {
    public:
        static const int kInvalidIndex = -1;

        Buffer(const char* name, uint32_t align);

        uint64_t alignment() { return align_; }
        uint64_t max_relocs() { return max_relocs_; }

        Buffer* GetRelocTarget(unsigned int i);

        bool References(Buffer* target_buffer);

        const char* Name();
        void Incref();
        void Decref();

    protected:
        // Buffer lifetime is managed by refcount.
        virtual ~Buffer();

    private:
        // Ctor init
        BufferRefcount* refcount_;

        uint32_t align_{};
        uint64_t max_relocs_{};

        friend class BufferRefcount;
    };

    LibdrmIntelGen();

    static uint64_t ComputeMaxRelocs(uint64_t batch_size);
};

#endif // LIBDRM_INTEL_GEN_H_
