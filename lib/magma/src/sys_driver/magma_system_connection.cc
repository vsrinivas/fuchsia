// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_connection.h"
#include "magma_system_device.h"
#include "magma_util/macros.h"
#include <vector>

MagmaSystemConnection::MagmaSystemConnection(std::weak_ptr<MagmaSystemDevice> weak_device,
                                             msd_connection_unique_ptr_t msd_connection_t,
                                             uint32_t capabilities)
    : device_(weak_device), msd_connection_(std::move(msd_connection_t))
{
    DASSERT(msd_connection_);

    has_display_capability_ = capabilities & MAGMA_CAPABILITY_DISPLAY;
    has_render_capability_ = capabilities & MAGMA_CAPABILITY_RENDERING;

    // should already be enforced in MagmaSystemDevice
    DASSERT(has_render_capability_ || has_display_capability_);
    DASSERT((capabilities & ~(MAGMA_CAPABILITY_DISPLAY | MAGMA_CAPABILITY_RENDERING)) == 0);
}

MagmaSystemConnection::~MagmaSystemConnection()
{
    auto device = device_.lock();
    if (device) {
        for (auto iter = buffer_map_.begin(); iter != buffer_map_.end();) {
            auto id = iter->first;
            iter = buffer_map_.erase(iter);
            device->ReleaseBuffer(id);
        }
        device->ConnectionClosed(std::this_thread::get_id());
    }
}

uint32_t MagmaSystemConnection::GetDeviceId()
{
    auto device = device_.lock();
    return device ? device->GetDeviceId() : 0;
}

bool MagmaSystemConnection::CreateContext(uint32_t context_id)
{
    if (!has_render_capability_)
        return DRETF(false, "Attempting to create a context without render capability");

    auto iter = context_map_.find(context_id);
    if (iter != context_map_.end())
        return DRETF(false, "Attempting to add context with duplicate id");

    auto msd_ctx = msd_connection_create_context(msd_connection());
    if (!msd_ctx)
        return DRETF(false, "Failed to create msd context");

    auto ctx = std::unique_ptr<MagmaSystemContext>(
        new MagmaSystemContext(this, msd_context_unique_ptr_t(msd_ctx, &msd_context_destroy)));

    context_map_.insert(std::make_pair(context_id, std::move(ctx)));
    return true;
}

bool MagmaSystemConnection::DestroyContext(uint32_t context_id)
{
    if (!has_render_capability_)
        return DRETF(false, "Attempting to destroy a context without render capability");

    auto iter = context_map_.find(context_id);
    if (iter == context_map_.end())
        return DRETF(false, "MagmaSystemConnection:Attempting to destroy invalid context id");
    context_map_.erase(iter);
    return true;
}

MagmaSystemContext* MagmaSystemConnection::LookupContext(uint32_t context_id)
{
    if (!has_render_capability_)
        return DRETP(nullptr, "Attempting to look up a context without render capability");

    auto iter = context_map_.find(context_id);
    if (iter == context_map_.end())
        return DRETP(nullptr, "MagmaSystemConnection: Attempting to lookup invalid context id");

    return iter->second.get();
}

magma::Status MagmaSystemConnection::ExecuteCommandBuffer(uint64_t command_buffer_id,
                                                          uint32_t context_id)
{
    if (!has_render_capability_)
        return DRET_MSG(MAGMA_STATUS_ACCESS_DENIED,
                        "Attempting to execute a command buffer without render capability");

    auto command_buffer = LookupBuffer(command_buffer_id);
    if (!command_buffer)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                        "Attempting to execute invalid command buffer id");

    auto context = LookupContext(context_id);
    if (!context)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                        "Attempting to execute command buffer on invalid context");

    return context->ExecuteCommandBuffer(command_buffer);
}

magma::Status MagmaSystemConnection::WaitRendering(uint64_t buffer_id)
{
    if (!has_render_capability_)
        return DRET_MSG(MAGMA_STATUS_ACCESS_DENIED,
                        "Attempting to wait rendering without render capability");

    std::shared_ptr<MagmaSystemBuffer> system_buffer = LookupBuffer(buffer_id);
    if (!system_buffer)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Couldn't find system buffer for id 0x%lx",
                        buffer_id);

    magma_status_t result =
        msd_connection_wait_rendering(msd_connection(), system_buffer->msd_buf());

    return DRET_MSG(result, "msd_connection_wait_rendering failed: %d", result);
}

bool MagmaSystemConnection::ImportBuffer(uint32_t handle, uint64_t* id_out)
{
    auto device = device_.lock();
    if (!device)
        return DRETF(false, "failed to lock device");

    auto buf = device->ImportBuffer(handle);
    if (!buf)
        return DRETF(false, "failed to get buffer for handle");

    uint64_t id = buf->id();

    auto iter = buffer_map_.find(id);
    if (iter == buffer_map_.end())
        buffer_map_.insert(std::make_pair(id, buf));

    *id_out = id;
    return true;
}

bool MagmaSystemConnection::ReleaseBuffer(uint64_t id)
{
    auto device = device_.lock();
    if (!device)
        return DRETF(false, "failed to lock device");

    auto iter = buffer_map_.find(id);
    if (iter == buffer_map_.end())
        return DRETF(false, "Attempting to free invalid buffer id");

    buffer_map_.erase(iter);
    // Now that our shared reference has been dropped we tell our
    // device that we're done with the buffer
    device->ReleaseBuffer(id);

    return true;
}

bool MagmaSystemConnection::ImportObject(uint32_t handle, magma::PlatformObject::Type object_type)
{
    auto device = device_.lock();
    if (!device)
        return DRETF(false, "failed to lock device");

    switch (object_type) {
        case magma::PlatformObject::SEMAPHORE: {
            uint64_t id;
            if (!magma::PlatformObject::IdFromHandle(handle, &id))
                return DRETF(false, "failed to get semaphore id for handle");

            auto iter = semaphore_map_.find(id);
            if (iter != semaphore_map_.end())
                return true;

            auto semaphore = MagmaSystemSemaphore::Create(magma::PlatformSemaphore::Import(handle));
            if (!semaphore)
                return DRETF(false, "failed to import platform semaphore");

            semaphore_map_.insert(std::make_pair(id, std::move(semaphore)));
        } break;
    }

    return true;
}

bool MagmaSystemConnection::ReleaseObject(uint64_t object_id,
                                          magma::PlatformObject::Type object_type)
{
    switch (object_type) {
        case magma::PlatformObject::SEMAPHORE: {
            auto iter = semaphore_map_.find(object_id);
            if (iter == semaphore_map_.end())
                return DRETF(false, "Attempting to free invalid semaphore id 0x%" PRIx64,
                             object_id);

            semaphore_map_.erase(iter);
        } break;
    }
    return true;
}

std::shared_ptr<MagmaSystemBuffer> MagmaSystemConnection::LookupBuffer(uint64_t id)
{
    auto iter = buffer_map_.find(id);
    if (iter == buffer_map_.end())
        return DRETP(nullptr, "Attempting to lookup invalid buffer id");

    return iter->second;
}

std::shared_ptr<MagmaSystemSemaphore> MagmaSystemConnection::LookupSemaphore(uint64_t id)
{
    auto iter = semaphore_map_.find(id);
    if (iter == semaphore_map_.end())
        return nullptr;
    return iter->second;
}

magma::Status MagmaSystemConnection::PageFlip(uint64_t id, uint32_t wait_semaphore_count,
                                              uint32_t signal_semaphore_count,
                                              uint64_t* semaphore_ids)
{
    if (!has_display_capability_)
        return DRET_MSG(MAGMA_STATUS_ACCESS_DENIED,
                        "Attempting to pageflip without display capability");

    auto buf = LookupBuffer(id);
    if (!buf)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Attempting to page flip with invalid buffer");

    std::vector<std::shared_ptr<MagmaSystemSemaphore>> semaphores(wait_semaphore_count +
                                                                  signal_semaphore_count);
    for (uint32_t i = 0; i < wait_semaphore_count + signal_semaphore_count; i++) {
        auto semaphore = LookupSemaphoreForContext(semaphore_ids[i]);
        if (!semaphore)
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "wait semaphore id not found 0x%" PRIx64,
                            semaphore_ids[i]);
        semaphores[i] = semaphore;
    }

    auto device = device_.lock();
    if (!device)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR,
                        "Attempting to page flip, failed to lock device");

    magma_system_image_descriptor image_desc{MAGMA_IMAGE_TILING_OPTIMAL};

    device->PageFlip(buf, &image_desc, wait_semaphore_count, signal_semaphore_count,
                     std::move(semaphores));
    return MAGMA_STATUS_OK;
}
