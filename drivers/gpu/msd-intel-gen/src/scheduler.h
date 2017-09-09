// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <memory>

class MsdIntelContext;
class CommandBuffer;

class Scheduler {
public:
    virtual ~Scheduler() = default;

    // Notifies the scheduler that a command buffer has been scheduled on the given context.
    virtual void CommandBufferQueued(std::weak_ptr<MsdIntelContext> context) = 0;

    // Notifies the scheduler that a command buffer has been completed on the given context.
    virtual void CommandBufferCompleted(std::shared_ptr<MsdIntelContext> context) = 0;

    // Selects the context whose command buffer will be executed next.
    virtual std::shared_ptr<MsdIntelContext> ScheduleContext() = 0;

    static std::unique_ptr<Scheduler> CreateFifoScheduler();
};

#endif // SCHEDULER_H
