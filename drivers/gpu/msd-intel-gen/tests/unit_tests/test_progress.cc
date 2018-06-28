// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu_progress.h"
#include "gtest/gtest.h"

constexpr uint32_t kTimeoutMs = 1000;

TEST(HangcheckTimeout, Init)
{
    auto time = std::chrono::steady_clock::now();
    GpuProgress progress;
    EXPECT_TRUE(progress.GetHangcheckTimeout(kTimeoutMs, time) ==
                std::chrono::steady_clock::duration::max());
}

TEST(HangcheckTimeout, SubmitOne)
{
    auto time = std::chrono::steady_clock::now();
    GpuProgress progress;
    progress.Submitted(0x1000, time);
    EXPECT_TRUE(progress.GetHangcheckTimeout(kTimeoutMs, time) ==
                std::chrono::milliseconds(kTimeoutMs));
}

TEST(HangcheckTimeout, SubmitMany)
{
    auto time = std::chrono::steady_clock::now();
    GpuProgress progress;
    progress.Submitted(0x1000, time);
    progress.Submitted(0x1001, time + std::chrono::milliseconds(10));
    progress.Submitted(0x1002, time + std::chrono::milliseconds(20));
    progress.Submitted(0x1003, time + std::chrono::milliseconds(30));
    EXPECT_TRUE(progress.GetHangcheckTimeout(kTimeoutMs, time) ==
                std::chrono::milliseconds(kTimeoutMs));
}

TEST(HangcheckTimeout, CompleteOne)
{
    auto time = std::chrono::steady_clock::now();
    GpuProgress progress;
    progress.Submitted(0x1000, time);
    progress.Completed(0x1000, time);
    EXPECT_TRUE(progress.GetHangcheckTimeout(kTimeoutMs, time) ==
                std::chrono::steady_clock::duration::max());
}

// Note, each sequence starts when the previous one completes.
TEST(HangcheckTimeout, CompleteMany)
{
    auto time = std::chrono::steady_clock::now();
    GpuProgress progress;
    progress.Submitted(0x1000, time);
    progress.Submitted(0x1001, time + std::chrono::milliseconds(10));
    progress.Submitted(0x1002, time + std::chrono::milliseconds(20));
    progress.Submitted(0x1003, time + std::chrono::milliseconds(30));
    progress.Completed(0x1000, time + std::chrono::milliseconds(50));
    EXPECT_TRUE(progress.GetHangcheckTimeout(kTimeoutMs, time) ==
                std::chrono::milliseconds(kTimeoutMs + 50));
    progress.Completed(0x1001, time + std::chrono::milliseconds(100));
    progress.Completed(0x1002, time + std::chrono::milliseconds(500));
    EXPECT_TRUE(progress.GetHangcheckTimeout(kTimeoutMs, time) ==
                std::chrono::milliseconds(kTimeoutMs + 500));
    progress.Completed(0x1003, time + std::chrono::milliseconds(600));
    EXPECT_TRUE(progress.GetHangcheckTimeout(kTimeoutMs, time) ==
                std::chrono::steady_clock::duration::max());
}
