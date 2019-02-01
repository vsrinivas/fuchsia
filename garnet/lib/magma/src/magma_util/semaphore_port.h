// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SEMAPHORE_PORT_H
#define SEMAPHORE_PORT_H

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "magma_util/status.h"
#include "platform_port.h"
#include "platform_semaphore.h"
#include "platform_trace.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace magma {

class SemaphorePort {
public:
    using shared_semaphore_vector_t = std::vector<std::shared_ptr<magma::PlatformSemaphore>>;

    static std::unique_ptr<SemaphorePort> Create()
    {
        auto port = magma::PlatformPort::Create();
        if (!port)
            return DRETP(nullptr, "failed to created port");

        return std::make_unique<SemaphorePort>(std::move(port));
    }

    SemaphorePort(std::unique_ptr<magma::PlatformPort> port) : port_(std::move(port)) {}

    void Close() { port_->Close(); }

    class WaitSet {
    public:
        WaitSet(std::function<void(WaitSet* batch)> callback, shared_semaphore_vector_t semaphores)
            : callback_(callback), semaphores_(semaphores)
        {
        }

        void SemaphoreComplete()
        {
            TRACE_DURATION("magma:sync", "WaitSet::SemaphoreComplete");

            DASSERT(completed_count_ < semaphore_count());
            ++completed_count_;
            DLOG("completed_count %u semaphore count %u", completed_count_, semaphore_count());
            if (completed_count_ == semaphore_count()) {
                for (auto semaphore : semaphores_) {
                    semaphore->Reset();
                }
                callback_(this);
            }
        }

        uint32_t semaphore_count() { return semaphores_.size(); }

        magma::PlatformSemaphore* semaphore(uint32_t index)
        {
            DASSERT(index < semaphore_count());
            return semaphores_[index].get();
        }

    private:
        std::function<void(WaitSet* wait_set)> callback_;
        shared_semaphore_vector_t semaphores_;
        uint32_t completed_count_ = 0;
    };

    // Note: fails if a given semaphore has already been waited (and not signalled),
    // because semaphores should not have multiple waiters.  See PlatformSemaphore.
    bool AddWaitSet(std::unique_ptr<WaitSet> wait_set)
    {
        if (wait_set->semaphore_count() == 0)
            return DRETF(false, "waitset has no semaphores");

        std::shared_ptr<WaitSet> shared_wait_set(std::move(wait_set));

        std::unique_lock<std::mutex> lock(map_mutex_);

        for (uint32_t i = 0; i < shared_wait_set->semaphore_count(); i++) {
            auto semaphore = shared_wait_set->semaphore(i);

            auto iter = map_.find(semaphore->id());
            if (iter != map_.end())
                return DRETF(false, "semaphore 0x%" PRIx64 " already in the map", semaphore->id());

            DLOG("adding semaphore 0x%" PRIx64 " to the map", semaphore->id());
            map_[semaphore->id()] = shared_wait_set;

            if (!semaphore->WaitAsync(port_.get()))
                return DRETF(false, "WaitAsync failed");
        }

        return true;
    }

    Status WaitOne()
    {
        TRACE_DURATION("magma:sync", "SemaphorePort::WaitOne");
        uint64_t id;

        Status status = port_->Wait(&id);
        if (!status)
            return status;

        std::unique_lock<std::mutex> lock(map_mutex_);

        auto iter = map_.find(id);
        DASSERT(iter != map_.end());

        std::shared_ptr<WaitSet> wait_set = std::move(iter->second);
        map_.erase(iter);

        lock.unlock();

        wait_set->SemaphoreComplete();

        return MAGMA_STATUS_OK;
    }

private:
    std::unique_ptr<magma::PlatformPort> port_;
    std::mutex map_mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<WaitSet>> map_;
};

} // namespace magma

#endif // SEMAPHORE_PORT_H
