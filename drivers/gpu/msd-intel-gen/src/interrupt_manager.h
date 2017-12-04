// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INTERRUPT_MANAGER_H
#define INTERRUPT_MANAGER_H

#include "magma_util/register_io.h"
#include "platform_interrupt.h"
#include <thread>
#include <type_traits>

class InterruptManager {
public:
    class Owner {
    public:
        virtual RegisterIo* register_io_for_interrupt() = 0;
    };

    ~InterruptManager();

    static std::unique_ptr<InterruptManager>
    Create(Owner* owner, std::unique_ptr<magma::PlatformInterrupt> platform_interrupt);

    using InterruptCallback =
        std::add_pointer_t<void(void* data, uint32_t master_interrupt_control)>;

    bool RegisterCallback(InterruptCallback callback, void* data, uint32_t interrupt_mask);

private:
    InterruptManager(Owner* owner, std::unique_ptr<magma::PlatformInterrupt> platform_interrupt)
        : owner_(owner), interrupt_(std::move(platform_interrupt))
    {
    }

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

    friend class TestInterruptManager;
};

#endif // INTERRUPT_MANAGER_H
