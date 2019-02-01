// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/mlme/client/channel_scheduler.h>
#include "mock_device.h"

namespace wlan {
namespace {

struct ChannelSchedulerTest : public ::testing::Test, public OnChannelHandler {
    ChannelSchedulerTest() : chan_sched_(this, &device_, device_.CreateTimer(1)) {
        chan_sched_.SetChannel(wlan_channel_t{.primary = 1, .cbw = CBW20, .secondary80 = 0});
    }

    virtual void HandleOnChannelFrame(fbl::unique_ptr<Packet>) override { str_ += "frame_on,"; }

    virtual void PreSwitchOffChannel() override { str_ += "pre_switch,"; }

    virtual void ReturnedOnChannel() override { str_ += "returned_on_chan,"; }

    MockDevice device_;
    ChannelScheduler chan_sched_;
    std::string str_;
};

OffChannelRequest CreateOffChannelRequest(OffChannelHandler* handler, uint8_t chan) {
    return OffChannelRequest{.chan = {.primary = chan, .cbw = CBW20, .secondary80 = 0},
                             .duration = zx::msec(200),
                             .handler = handler};
}

struct MockOffChannelHandler : OffChannelHandler {
    MockOffChannelHandler(std::string* str, size_t num_chained)
        : str_(str), num_chained_(num_chained) {}

    std::string* str_;
    size_t num_chained_;

    virtual void BeginOffChannelTime() { (*str_) += "begin_off,"; }

    virtual void HandleOffChannelFrame(fbl::unique_ptr<Packet>) { (*str_) += "frame_off,"; }

    virtual bool EndOffChannelTime(bool interrupted, OffChannelRequest* next_req) {
        (*str_) += "end_off(";
        (*str_) += (interrupted ? "true" : "false");
        (*str_) += "),";
        if (num_chained_ > 0) {
            num_chained_--;
            *next_req = CreateOffChannelRequest(this, 9);
            return true;
        } else {
            return false;
        }
    }
};

TEST_F(ChannelSchedulerTest, OnChannelFrame) {
    chan_sched_.HandleIncomingFrame(fbl::make_unique<Packet>(GetBuffer(10), 10));
    EXPECT_EQ("frame_on,", str_);
}

TEST_F(ChannelSchedulerTest, RequestOffChannelTime) {
    MockOffChannelHandler handler(&str_, 0);
    chan_sched_.RequestOffChannelTime(CreateOffChannelRequest(&handler, 7));

    EXPECT_EQ("pre_switch,begin_off,", str_);
    EXPECT_FALSE(chan_sched_.OnChannel());
    EXPECT_EQ(7u, device_.GetChannelNumber());
    str_.clear();

    chan_sched_.HandleIncomingFrame(fbl::make_unique<Packet>(GetBuffer(10), 10));
    EXPECT_EQ("frame_off,", str_);
    str_.clear();

    chan_sched_.HandleTimeout();
    EXPECT_EQ("end_off(false),returned_on_chan,", str_);
    EXPECT_TRUE(chan_sched_.OnChannel());
    EXPECT_EQ(1u, device_.GetChannelNumber());
}

TEST_F(ChannelSchedulerTest, RequestOffChannelTimeChained) {
    MockOffChannelHandler handler(&str_, 1);
    chan_sched_.RequestOffChannelTime(CreateOffChannelRequest(&handler, 7));

    EXPECT_EQ("pre_switch,begin_off,", str_);
    EXPECT_FALSE(chan_sched_.OnChannel());
    EXPECT_EQ(7u, device_.GetChannelNumber());
    str_.clear();

    // The MockOffChannelHandler will return another off-channel request
    // in its EndOffChannelTime method
    chan_sched_.HandleTimeout();
    EXPECT_EQ("end_off(false),begin_off,", str_);
    EXPECT_FALSE(chan_sched_.OnChannel());
    EXPECT_EQ(9u, device_.GetChannelNumber());
    str_.clear();

    chan_sched_.HandleTimeout();
    EXPECT_EQ("end_off(false),returned_on_chan,", str_);
    EXPECT_TRUE(chan_sched_.OnChannel());
    EXPECT_EQ(1u, device_.GetChannelNumber());
}

TEST_F(ChannelSchedulerTest, SetChannelSwitchesWhenOnChannel) {
    chan_sched_.SetChannel(wlan_channel_t{.primary = 6, .cbw = CBW20, .secondary80 = 0});
    EXPECT_TRUE(chan_sched_.OnChannel());
    EXPECT_EQ(6u, device_.GetChannelNumber());
}

TEST_F(ChannelSchedulerTest, SetChannelDoesNotSwitchWhenOffChannel) {
    // Go off channel
    MockOffChannelHandler handler(&str_, 0);
    chan_sched_.RequestOffChannelTime(CreateOffChannelRequest(&handler, 7));
    EXPECT_EQ(7u, device_.GetChannelNumber());
    EXPECT_FALSE(chan_sched_.OnChannel());

    // Change the 'on' channel. Expect to stay off channel
    chan_sched_.SetChannel(wlan_channel_t{.primary = 6, .cbw = CBW20, .secondary80 = 0});
    EXPECT_FALSE(chan_sched_.OnChannel());
    EXPECT_EQ(7u, device_.GetChannelNumber());

    // End the off-channel time. Expect to switch to the new main channel
    chan_sched_.HandleTimeout();
    EXPECT_TRUE(chan_sched_.OnChannel());
    EXPECT_EQ(6u, device_.GetChannelNumber());
}

TEST_F(ChannelSchedulerTest, EnsureOnChannelDelaysOffChannelRequest) {
    chan_sched_.EnsureOnChannel(zx::time() + zx::sec(2));
    EXPECT_EQ("", str_);
    EXPECT_EQ(1u, device_.GetChannelNumber());

    // Make an off-channel request. It shouldn't be served immediately
    // since we are committed to staying on the main channel for some time
    MockOffChannelHandler handler(&str_, 0);
    chan_sched_.RequestOffChannelTime(CreateOffChannelRequest(&handler, 7));

    EXPECT_EQ("", str_);
    EXPECT_EQ(1u, device_.GetChannelNumber());

    // Let the guaranteed on channel time expire
    chan_sched_.HandleTimeout();

    // The off-channel request should be served now
    EXPECT_EQ("pre_switch,begin_off,", str_);
    EXPECT_EQ(7u, device_.GetChannelNumber());
}

TEST_F(ChannelSchedulerTest, EnsureOnChannelCancelsOffChannelRequest) {
    MockOffChannelHandler handler(&str_, 0);
    chan_sched_.RequestOffChannelTime(CreateOffChannelRequest(&handler, 7));

    EXPECT_EQ("pre_switch,begin_off,", str_);
    EXPECT_EQ(7u, device_.GetChannelNumber());
    str_.clear();

    chan_sched_.EnsureOnChannel(zx::time() + zx::sec(2));
    EXPECT_EQ("end_off(true),returned_on_chan,", str_);
    EXPECT_EQ(1u, device_.GetChannelNumber());
}

}  // namespace
}  // namespace wlan
