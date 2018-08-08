// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_TESTS_MOCK_MOCK_MSD_H_
#define GARNET_LIB_MAGMA_TESTS_MOCK_MOCK_MSD_H_

#include "magma_util/command_buffer.h"
#include "magma_util/macros.h"
#include "msd.h"
#include "platform_buffer.h"
#include <memory>
#include <vector>

// These classes contain default implementations of msd_device_t functionality.
// To override a specific function to contain test logic, inherit from the
// desired class, override the desired function, and pass as the msd_abi object

class MsdMockBuffer : public msd_buffer_t {
public:
    MsdMockBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf)
        : platform_buf_(std::move(platform_buf))
    {
        magic_ = kMagic;
    }
    virtual ~MsdMockBuffer() {}

    static MsdMockBuffer* cast(msd_buffer_t* buf)
    {
        DASSERT(buf);
        DASSERT(buf->magic_ == kMagic);
        return static_cast<MsdMockBuffer*>(buf);
    }

    magma::PlatformBuffer* platform_buffer() { return platform_buf_.get(); }

private:
    std::unique_ptr<magma::PlatformBuffer> platform_buf_;
    static const uint32_t kMagic = 0x6d6b6266; // "mkbf" (Mock Buffer)
};

class MsdMockConnection;

class MsdMockCommandBuffer : public magma::CommandBuffer {
public:
    MsdMockCommandBuffer(MsdMockBuffer* buffer) : buffer_(buffer) {}

    magma::PlatformBuffer* platform_buffer() override { return buffer_->platform_buffer(); }

private:
    MsdMockBuffer* buffer_;
};

class MsdMockContext : public msd_context_t {
public:
    MsdMockContext(MsdMockConnection* connection) : connection_(connection) { magic_ = kMagic; }
    virtual ~MsdMockContext();

    magma_status_t ExecuteCommandBuffer(msd_buffer_t* cmd_buf_in, msd_buffer_t** exec_resources)
    {
        auto cmd_buf = MsdMockCommandBuffer(MsdMockBuffer::cast(cmd_buf_in));
        if (!cmd_buf.Initialize())
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to Initialize command buffer");
        last_submitted_exec_resources_.clear();
        for (uint32_t i = 0; i < cmd_buf.num_resources(); i++) {
            last_submitted_exec_resources_.push_back(MsdMockBuffer::cast(exec_resources[i]));
        }
        return MAGMA_STATUS_OK;
    }

    static MsdMockContext* cast(msd_context_t* ctx)
    {
        DASSERT(ctx);
        DASSERT(ctx->magic_ == kMagic);
        return static_cast<MsdMockContext*>(ctx);
    }

    std::vector<MsdMockBuffer*>& last_submitted_exec_resources()
    {
        return last_submitted_exec_resources_;
    }

private:
    std::vector<MsdMockBuffer*> last_submitted_exec_resources_;

    MsdMockConnection* connection_;
    static const uint32_t kMagic = 0x6d6b6378; // "mkcx" (Mock Context)
};

class MsdMockConnection : public msd_connection_t {
public:
    MsdMockConnection() { magic_ = kMagic; }
    virtual ~MsdMockConnection() {}

    virtual MsdMockContext* CreateContext() { return new MsdMockContext(this); }

    virtual void DestroyContext(MsdMockContext* ctx) {}

    static MsdMockConnection* cast(msd_connection_t* connection)
    {
        DASSERT(connection);
        DASSERT(connection->magic_ == kMagic);
        return static_cast<MsdMockConnection*>(connection);
    }

private:
    static const uint32_t kMagic = 0x6d6b636e; // "mkcn" (Mock Connection)
};

class MsdMockDevice : public msd_device_t {
public:
    MsdMockDevice() { magic_ = kMagic; }
    virtual ~MsdMockDevice() {}

    virtual msd_connection_t* Open(msd_client_id_t client_id) { return new MsdMockConnection(); }
    virtual uint32_t GetDeviceId() { return 0; }

    static MsdMockDevice* cast(msd_device_t* dev)
    {
        DASSERT(dev);
        DASSERT(dev->magic_ == kMagic);
        return static_cast<MsdMockDevice*>(dev);
    }

private:
    static const uint32_t kMagic = 0x6d6b6476; // "mkdv" (Mock Device)
};

class MsdMockDriver : public msd_driver_t {
public:
    MsdMockDriver() { magic_ = kMagic; }
    virtual ~MsdMockDriver() {}

    virtual MsdMockDevice* CreateDevice() { return new MsdMockDevice(); }

    virtual void DestroyDevice(MsdMockDevice* dev) { delete dev; }

    static MsdMockDriver* cast(msd_driver_t* drv)
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

    virtual MsdMockBuffer* CreateBuffer(uint32_t handle)
    {
        auto platform_buf = magma::PlatformBuffer::Import(handle);
        return new MsdMockBuffer(std::move(platform_buf));
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