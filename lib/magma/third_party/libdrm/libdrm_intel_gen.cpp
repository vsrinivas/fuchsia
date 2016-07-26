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

#include "libdrm_intel_gen.h"
#include "common/magma_defs.h"
#include "magma_util/macros.h"
#include <errno.h>
#include <memory.h>

// Alignment must be a power of 2
#define ALIGN(value, alignment) (((value) + ((alignment)-1)) & ~((alignment)-1))

struct LibdrmIntelGen::RelocTargetInfo {
    Buffer* bo;
    int flags;
};

class LibdrmIntelGen::BufferRefcount : public magma::Refcounted {
public:
    BufferRefcount(const char* name, Buffer* buffer) : magma::Refcounted(name), buffer_(buffer) {}

    virtual void Delete()
    {
        delete buffer_;
        delete this;
    }

private:
    Buffer* buffer_;
};

LibdrmIntelGen::Buffer::Buffer(const char* name, uint32_t align)
    : refcount_(new BufferRefcount(name, this)), align_(align)
{
}

LibdrmIntelGen::Buffer::~Buffer() {}

const char* LibdrmIntelGen::Buffer::Name() { return refcount_->name(); }

void LibdrmIntelGen::Buffer::Incref() { return refcount_->Incref(); }

void LibdrmIntelGen::Buffer::Decref() { return refcount_->Decref(); }

bool LibdrmIntelGen::Buffer::References(Buffer* target_buffer)
{
    DLOG("TODO: References");
    return false;
}

//////////////////////////////////////////////////////////////////////////////

LibdrmIntelGen::LibdrmIntelGen() {}

uint64_t LibdrmIntelGen::ComputeMaxRelocs(uint64_t batch_size)
{
    /* Let's go with one relocation per every 2 dwords (but round down a bit
     * since a power of two will mean an extra page allocation for the reloc
     * buffer).
     *
     * Every 4 was too few for the blender benchmark.
     */
    return batch_size / sizeof(uint32_t) / 2 - 2;
}
