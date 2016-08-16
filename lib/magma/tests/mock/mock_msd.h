// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

class MsdMockConnection;

class MsdMockContext : public msd_context {
public:
    MsdMockContext(MsdMockConnection* connection) : connection_(connection) { magic_ = kMagic; }
    virtual ~MsdMockContext();

    static MsdMockContext* cast(msd_context* ctx)
    {
        DASSERT(ctx);
        DASSERT(ctx->magic_ == kMagic);
        return static_cast<MsdMockContext*>(ctx);
    }

private:
    MsdMockConnection* connection_;
    static const uint32_t kMagic = 0x6d6b6378; // "mkcx" (Mock Context)
};

class MsdMockConnection : public msd_connection {
public:
    MsdMockConnection() { magic_ = kMagic; }
    virtual ~MsdMockConnection() {}

    virtual MsdMockContext* CreateContext() { return new MsdMockContext(this); }

    virtual void DestroyContext(MsdMockContext* ctx) {}

    static MsdMockConnection* cast(msd_connection* connection)
    {
        DASSERT(connection);
        DASSERT(connection->magic_ == kMagic);
        return static_cast<MsdMockConnection*>(connection);
    }

private:
    static const uint32_t kMagic = 0x6d6b636e; // "mkcn" (Mock Connection)
};

class MsdMockDevice : public msd_device {
public:
    MsdMockDevice() { magic_ = kMagic; }
    virtual ~MsdMockDevice() {}

    virtual msd_connection* Open(msd_client_id client_id) { return new MsdMockConnection(); }
    virtual uint32_t GetDeviceId() { return 0; }

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