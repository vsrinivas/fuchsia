// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "interrupt_manager.h"

#include <thread>
#include <type_traits>

#include "platform_thread.h"
#include "registers.h"

class InterruptManagerCore : public InterruptManager {
public:
    InterruptManagerCore(Owner* owner, std::unique_ptr<magma::PlatformInterrupt> platform_interrupt)
        : owner_(owner), interrupt_(std::move(platform_interrupt))
    {
    }

    ~InterruptManagerCore() override;

    bool RegisterCallback(InterruptCallback callback, void* data, uint32_t interrupt_mask) override;

private:
    void ThreadLoop();

    RegisterIo* register_io() { return owner_->register_io_for_interrupt(); }

    magma::PlatformInterrupt* platform_interrupt() { return interrupt_.get(); }

    Owner* owner_;
    std::unique_ptr<magma::PlatformInterrupt> interrupt_;
    std::thread thread_;
    std::atomic_bool quit_flag_{false};
    InterruptCallback callback_ = nullptr;
    void* data_;
    uint32_t interrupt_mask_;

    friend class TestInterruptManagerCore;
};

InterruptManagerCore::~InterruptManagerCore()
{
    quit_flag_ = true;

    if (thread_.joinable()) {
        interrupt_->Signal();
        DLOG("joining interrupt thread");
        thread_.join();
        DLOG("joined");
    }
}

bool InterruptManagerCore::RegisterCallback(InterruptCallback callback, void* data,
                                            uint32_t interrupt_mask)
{
    if (callback_)
        return DRETF(false, "interrupt callback already registered");
    callback_ = callback;
    data_ = data;
    interrupt_mask_ = interrupt_mask;

    DASSERT(!thread_.joinable());
    thread_ = std::thread([this] { this->ThreadLoop(); });

    return true;
}

void InterruptManagerCore::ThreadLoop()
{
    magma::PlatformThreadHelper::SetCurrentThreadName("InterruptThread");
    DLOG("Interrupt thread started");

    while (!quit_flag_) {
        registers::MasterInterruptControl::write(register_io(), true);

        DLOG("waiting for interrupt");
        interrupt_->Wait();
        DLOG("Returned from interrupt wait!");

        registers::MasterInterruptControl::write(register_io(), false);

        if (quit_flag_)
            break;

        uint32_t master_interrupt_control = registers::MasterInterruptControl::read(register_io());
        if (master_interrupt_control & interrupt_mask_) {
            DASSERT(callback_);
            callback_(data_, master_interrupt_control);
        }

        interrupt_->Complete();
    }

    DLOG("Interrupt thread exited");
}

///////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<InterruptManager> InterruptManager::CreateCore(Owner* owner)
{
    auto platform_interrupt = owner->platform_device()->RegisterInterrupt();
    if (!platform_interrupt)
        return DRETP(nullptr, "failed to register interrupt");

    return std::make_unique<InterruptManagerCore>(owner, std::move(platform_interrupt));
}
