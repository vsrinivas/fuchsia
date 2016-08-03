// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _MOCK_MSD_H_
#define _MOCK_MSD_H_

#include "magma_util/macros.h"
#include "msd.h"
#include <memory>

// These classes contain default implementations of msd_device functionality.
// To override a specific function to contain test logic, inherit from the
// desired class, override the desired function, and pass as the msd_abi object

class MsdMockBuffer : public msd_buffer {
public:
    MsdMockBuffer(struct msd_platform_buffer* platform_buf) { magic_ = kMagic; }
    virtual ~MsdMockBuffer() {}

    static MsdMockBuffer* cast(msd_buffer* buf)
    {
        DASSERT(buf);
        DASSERT(buf->magic_ == kMagic);
        return static_cast<MsdMockBuffer*>(buf);
    }

private:
    static const uint32_t kMagic = 0x6d6b6266; // "mkbf" (Mock Buffer)
};

class MsdMockContext : public msd_context {
public:
    MsdMockContext() { magic_ = kMagic; }
    virtual ~MsdMockContext() {}

    static MsdMockContext* cast(msd_context* ctx)
    {
        DASSERT(ctx);
        DASSERT(ctx->magic_ == kMagic);
        return static_cast<MsdMockContext*>(ctx);
    }

private:
    static const uint32_t kMagic = 0x6d6b6378; // "mkcx" (Mock Context)
};

class MsdMockDevice : public msd_device {
public:
    MsdMockDevice() { magic_ = kMagic; }
    virtual ~MsdMockDevice() {}

    virtual int32_t Open(msd_client_id client_id) { return 0; }
    virtual int32_t Close(msd_client_id client_id) { return 0; }
    virtual uint32_t GetDeviceId() { return 0; }

    virtual MsdMockContext* CreateContext() { return new MsdMockContext; }

    virtual void DestroyContext(MsdMockContext* ctx) { delete ctx; }

    static MsdMockDevice* cast(msd_device* dev)
    {
        DASSERT(dev);
        DASSERT(dev->magic_ == kMagic);
        return static_cast<MsdMockDevice*>(dev);
    }

private:
    static const uint32_t kMagic = 0x6d6b6476; // "mkdv" (Mock Device)
};

class MsdMockDriver : public msd_driver {
public:
    MsdMockDriver() { magic_ = kMagic; }
    virtual ~MsdMockDriver() {}

    virtual MsdMockDevice* CreateDevice() { return new MsdMockDevice(); }

    virtual void DestroyDevice(MsdMockDevice* dev) { delete dev; }

    static MsdMockDriver* cast(msd_driver* drv)
    {
        DASSERT(drv);
        DASSERT(drv->magic_ == kMagic);
        return static_cast<MsdMockDriver*>(drv);
    }

private:
    static const uint32_t kMagic = 0x6d6b6472; // "mkdr" (Mock Driver)
};

// There is no buffermanager concept in the msd abi right now, so this class is
// for testing purposes only, making it a little different than the other
// classes in this header

class MsdMockBufferManager {
public:
    MsdMockBufferManager() {}
    virtual ~MsdMockBufferManager() {}

    virtual MsdMockBuffer* CreateBuffer(struct msd_platform_buffer* platform_buf)
    {
        return new MsdMockBuffer(platform_buf);
    }

    virtual void DestroyBuffer(MsdMockBuffer* buf) { delete buf; }

    class ScopedMockBufferManager {
    public:
        ScopedMockBufferManager(std::unique_ptr<MsdMockBufferManager> bufmgr)
        {
            SetTestBufferManager(std::move(bufmgr));
        }

        ~ScopedMockBufferManager() { SetTestBufferManager(nullptr); }

        MsdMockBufferManager* get();
    };

private:
    static void SetTestBufferManager(std::unique_ptr<MsdMockBufferManager> bufmgr);
};

#endif