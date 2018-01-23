// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "performance_counters.h"

#include "address_manager.h"
#include "mock/mock_bus_mapper.h"
#include "mock/mock_mmio.h"
#include "gtest/gtest.h"

namespace {
class FakeOwner : public AddressManager::Owner {
public:
    FakeOwner(magma::RegisterIo* regs) : register_io_(regs) {}

    magma::RegisterIo* register_io() override { return register_io_; }

private:
    magma::RegisterIo* register_io_;
};

class TestConnectionOwner : public MsdArmConnection::Owner {
public:
    TestConnectionOwner(AddressManager* manager) : manager_(manager) {}

    void ScheduleAtom(std::shared_ptr<MsdArmAtom> atom) override {}
    void CancelAtoms(std::shared_ptr<MsdArmConnection> connection) override {}
    AddressSpaceObserver* GetAddressSpaceObserver() override { return manager_; }
    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

private:
    AddressManager* manager_;
    MockBusMapper bus_mapper_;
};

class TestCounterOwner : public PerformanceCounters::Owner {
public:
    TestCounterOwner(magma::RegisterIo* regs)
        : register_io_(regs), address_manager_owner_(regs),
          address_manager_(&address_manager_owner_, 2), connection_owner_(&address_manager_)
    {
    }

    magma::RegisterIo* register_io() override { return register_io_; }
    AddressManager* address_manager() override { return &address_manager_; }
    MsdArmConnection::Owner* connection_owner() override { return &connection_owner_; }

private:
    magma::RegisterIo* register_io_;

    FakeOwner address_manager_owner_;
    AddressManager address_manager_;
    TestConnectionOwner connection_owner_;
};

} // namespace

class PerformanceCounterTest {
public:
    static void TestStateChange()
    {
        auto mmio = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
        TestCounterOwner owner(mmio.get());
        PerformanceCounters perf_counters(&owner);
        uint64_t duration_ms;

        EXPECT_EQ(PerformanceCounters::PerformanceCounterState::kDisabled,
                  perf_counters.counter_state_);
        EXPECT_FALSE(perf_counters.TriggerRead(false));

        EXPECT_EQ(PerformanceCounters::PerformanceCounterState::kDisabled,
                  perf_counters.counter_state_);
        perf_counters.ReadCompleted(&duration_ms);
        EXPECT_EQ(PerformanceCounters::PerformanceCounterState::kDisabled,
                  perf_counters.counter_state_);
        EXPECT_TRUE(perf_counters.Enable());
        EXPECT_EQ(PerformanceCounters::PerformanceCounterState::kEnabled,
                  perf_counters.counter_state_);

        perf_counters.ReadCompleted(&duration_ms);
        EXPECT_EQ(PerformanceCounters::PerformanceCounterState::kEnabled,
                  perf_counters.counter_state_);

        EXPECT_TRUE(perf_counters.TriggerRead(false));
        EXPECT_EQ(PerformanceCounters::PerformanceCounterState::kTriggered,
                  perf_counters.counter_state_);

        EXPECT_FALSE(perf_counters.Enable());
        EXPECT_EQ(PerformanceCounters::PerformanceCounterState::kTriggered,
                  perf_counters.counter_state_);

        perf_counters.ReadCompleted(&duration_ms);
        EXPECT_EQ(PerformanceCounters::PerformanceCounterState::kDisabled,
                  perf_counters.counter_state_);
    }

    static void TestEnabled()
    {
        auto mmio = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
        TestCounterOwner owner(mmio.get());
        PerformanceCounters perf_counters(&owner);
        uint64_t duration_ms;

        EXPECT_EQ(nullptr, owner.address_manager()->GetMappingForSlot(0).get());
        EXPECT_TRUE(perf_counters.Enable());
        EXPECT_NE(nullptr, perf_counters.address_mapping_.get());
        EXPECT_EQ(perf_counters.address_mapping_, owner.address_manager()->GetMappingForSlot(0));

        EXPECT_TRUE(perf_counters.TriggerRead(false));
        registers::PerformanceCounterBase::Get().FromValue(4096 + 1024).WriteTo(mmio.get());
        auto result = perf_counters.ReadCompleted(&duration_ms);
        EXPECT_EQ(1024u / 4u, result.size());
        EXPECT_EQ(0u, result[0]);
        EXPECT_EQ(0u, registers::PerformanceCounterConfig::Get().ReadFrom(mmio.get()).reg_value());
    }

    static void TestKeepEnabled()
    {
        auto mmio = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
        TestCounterOwner owner(mmio.get());
        PerformanceCounters perf_counters(&owner);
        uint64_t duration_ms;

        EXPECT_TRUE(perf_counters.Enable());

        EXPECT_TRUE(perf_counters.TriggerRead(true));
        registers::PerformanceCounterBase::Get().FromValue(4096 + 1024).WriteTo(mmio.get());
        auto result = perf_counters.ReadCompleted(&duration_ms);
        EXPECT_EQ(1u, registers::PerformanceCounterConfig::Get().ReadFrom(mmio.get()).mode().get());
        EXPECT_EQ(4096u, registers::PerformanceCounterBase::Get().ReadFrom(mmio.get()).reg_value());
        EXPECT_TRUE(perf_counters.TriggerRead(true));
    }
};

TEST(PerfCounters, StateChange) { PerformanceCounterTest::TestStateChange(); }

TEST(PerfCounters, Enabled) { PerformanceCounterTest::TestEnabled(); }

TEST(PerfCounters, KeepEnabled) { PerformanceCounterTest::TestKeepEnabled(); }
