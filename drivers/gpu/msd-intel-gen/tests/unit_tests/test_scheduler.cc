// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/sleep.h"
#include "mock/mock_address_space.h"
#include "mock/mock_mapped_batch.h"
#include "msd_intel_context.h"
#include "scheduler.h"
#include "gtest/gtest.h"

class TestScheduler {
public:
    static constexpr uint32_t kNumContext = 3;

    TestScheduler()
    {
        auto address_space = std::make_shared<MockAddressSpace>(0, PAGE_SIZE);
        for (uint32_t i = 0; i < kNumContext; i++) {
            context_[i] = std::make_shared<ClientContext>(connection_, address_space);
        }
    }

    void Fifo()
    {
        auto scheduler = Scheduler::CreateFifoScheduler();

        auto context = scheduler->ScheduleContext();
        EXPECT_EQ(nullptr, context);

        context_[0]->pending_batch_queue().push(std::make_unique<MockMappedBatch>());
        scheduler->CommandBufferQueued(context_[0]);

        context = scheduler->ScheduleContext();
        EXPECT_EQ(context, context_[0]);

        context_[1]->pending_batch_queue().push(std::make_unique<MockMappedBatch>());
        scheduler->CommandBufferQueued(context_[1]);

        // 0 is still current
        context = scheduler->ScheduleContext();
        EXPECT_EQ(nullptr, context);

        context_[2]->pending_batch_queue().push(std::make_unique<MockMappedBatch>());
        scheduler->CommandBufferQueued(context_[2]);

        // 0 is still current
        context = scheduler->ScheduleContext();
        EXPECT_EQ(nullptr, context);

        scheduler->CommandBufferCompleted(context_[0]);

        context = scheduler->ScheduleContext();
        EXPECT_EQ(context_[1], context);

        scheduler->CommandBufferCompleted(context_[1]);

        context = scheduler->ScheduleContext();
        EXPECT_EQ(context_[2], context);

        scheduler->CommandBufferCompleted(context_[2]);

        context = scheduler->ScheduleContext();
        EXPECT_EQ(nullptr, context);
    }

private:
    std::weak_ptr<MsdIntelConnection> connection_;
    std::shared_ptr<MsdIntelContext> context_[kNumContext];
};

TEST(Scheduler, Fifo)
{
    TestScheduler test;
    test.Fifo();
}
