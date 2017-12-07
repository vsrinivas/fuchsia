// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>
#include "interrupt_manager.h"
#include "gtest/gtest.h"
#include "mock/mock_mmio.h"
#include "platform_semaphore.h"
#include "registers.h"

class MockInterrupt : public magma::PlatformInterrupt {
public:
    void Signal() override { state_->semaphore->Signal(); }
    bool Wait() override { return state_->semaphore->Wait(); }
    void Complete() override { state_->completed_count++; }

    uint32_t completed_count() { return state_->completed_count; }

    struct State {
        std::unique_ptr<magma::PlatformSemaphore> semaphore = magma::PlatformSemaphore::Create();
        uint32_t completed_count{};
    };
    std::shared_ptr<State> state_ = std::make_shared<State>();
};

class MockPlatformDevice : public magma::PlatformPciDevice {
public:
    void* GetDeviceHandle() override { return nullptr; }

    std::unique_ptr<magma::PlatformInterrupt> RegisterInterrupt() override
    {
        auto interrupt = std::make_unique<MockInterrupt>();
        state_ = interrupt->state_;
        return interrupt;
    }

    std::shared_ptr<MockInterrupt::State> state_;
};

class TestInterruptManager : public InterruptManager::Owner {
public:
    TestInterruptManager()
    {
        platform_device_ = std::make_unique<MockPlatformDevice>();

        register_io_ =
            std::unique_ptr<RegisterIo>(new RegisterIo(MockMmio::Create(8 * 1024 * 1024)));

        interrupt_manager_ = InterruptManager::CreateCore(this);
    }

    MockInterrupt::State* mock_interrupt_state() { return platform_device_->state_.get(); }

    static constexpr uint32_t kRegisterStatus = 0x10;

    class Hook : public RegisterIo::Hook {
    public:
        Hook(RegisterIo* register_io) : register_io_(register_io) {}

        void Write32(uint32_t offset, uint32_t val) override
        {
            switch (offset) {
                // When interrupt manager disables interrupts, we overwrite that with the desired pending interrupt status
                case registers::MasterInterruptControl::kOffset:
                    if (val == 0)
                        register_io_->Write32(offset, kRegisterStatus);
                break;
            }
        }

        void Read32(uint32_t offset, uint32_t val) override {}
        void Read64(uint32_t offset, uint64_t val) override {}

    private:
        RegisterIo* register_io_;
    };

    void Basic()
    {
        register_io_->InstallHook(std::make_unique<Hook>(register_io_.get()));

        EXPECT_TRUE(interrupt_manager_->RegisterCallback(InterruptCallback, this, kRegisterStatus));

        auto start = std::chrono::high_resolution_clock::now();
        while (register_io_->Read32(registers::MasterInterruptControl::kOffset) != 0x80000000 &&
               std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() -
                                                         start)
                       .count() < 1000) {
            std::this_thread::yield();
        }
        EXPECT_EQ(register_io_->Read32(registers::MasterInterruptControl::kOffset), 0x80000000u);

        ASSERT_EQ(callback_count_, 0u);
        mock_interrupt_state()->semaphore->Signal();

        start = std::chrono::high_resolution_clock::now();
        while (callback_count_ != 1 && std::chrono::duration<double, std::milli>(
                    std::chrono::high_resolution_clock::now() - start)
                    .count() < 100) {
            std::this_thread::yield();
        }
        EXPECT_EQ(callback_count_, 1u);
        EXPECT_EQ(mock_interrupt_state()->completed_count, 1u);
        EXPECT_EQ(register_io_->Read32(registers::MasterInterruptControl::kOffset), 0x80000000u);
    }

    static void InterruptCallback(void* data, uint32_t master_interrupt_control)
    {
        auto test = reinterpret_cast<TestInterruptManager*>(data);
        test->callback_count_++;
    }

private:
    RegisterIo* register_io_for_interrupt() override { return register_io_.get(); }

    magma::PlatformPciDevice* platform_device() override { return platform_device_.get(); }

    std::unique_ptr<MockPlatformDevice> platform_device_;
    std::unique_ptr<RegisterIo> register_io_;
    std::unique_ptr<InterruptManager> interrupt_manager_;
    std::atomic_uint32_t callback_count_{};
};

TEST(InterruptManager, Basic)
{
    TestInterruptManager().Basic();
}
