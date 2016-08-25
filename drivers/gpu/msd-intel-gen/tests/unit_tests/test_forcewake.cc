// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "forcewake.h"
#include "magma_util/platform/platform_mmio.h"
#include "mock/mock_mmio.h"
#include "registers.h"
#include "gtest/gtest.h"

class TestForceWake {
public:
    TestForceWake()
    {
        register_io_ = std::unique_ptr<RegisterIo>(
            new RegisterIo(MockMmio::Create(registers::MultiForceWake::kStatusOffset + 4)));
    }

    void Reset()
    {
        register_io_->mmio()->Write32(0, registers::MultiForceWake::kOffset);
        ForceWake::reset(register_io_.get());
        EXPECT_EQ(0xFFFF0000, register_io_->mmio()->Read32(registers::MultiForceWake::kOffset));
    }

    void Request()
    {
        register_io_->mmio()->Write32(0, registers::MultiForceWake::kStatusOffset);

        // Verify timeout waiting for status
        auto start = std::chrono::high_resolution_clock::now();
        ForceWake::request(register_io_.get());
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;

        EXPECT_EQ(0x00010001u, register_io_->mmio()->Read32(registers::MultiForceWake::kOffset));
        EXPECT_GE(elapsed.count(), (uint32_t)ForceWake::kRetryMaxMs);
    }

    void Release()
    {
        register_io_->mmio()->Write32(0xFFFFFFFF, registers::MultiForceWake::kStatusOffset);

        // Verify timeout waiting for status
        auto start = std::chrono::high_resolution_clock::now();
        ForceWake::release(register_io_.get());
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;

        EXPECT_EQ(0x00010000u, register_io_->mmio()->Read32(registers::MultiForceWake::kOffset));
        EXPECT_GE(elapsed.count(), (uint32_t)ForceWake::kRetryMaxMs);
    }

private:
    std::unique_ptr<RegisterIo> register_io_;
};

TEST(ForceWake, Reset)
{
    TestForceWake test;
    test.Reset();
}

TEST(ForceWake, Request)
{
    TestForceWake test;
    test.Request();
}

TEST(ForceWake, Release)
{
    TestForceWake test;
    test.Release();
}
