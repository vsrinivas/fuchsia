// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <wlan/common/macaddr.h>
#include <wlan/mlme/sequence.h>

namespace wlan {
namespace common {
namespace {

class SequenceTest : public ::testing::Test {
   protected:
    virtual void SetUp() {
        addr1.Set("01:02:03:04:05:06");
        addr2.Set("02:02:03:04:05:06");
        addr3.Set("03:02:03:04:05:06");
    }

    virtual void TearDown() {}

    MacAddr addr1;
    MacAddr addr2;
    MacAddr addr3;
};

TEST_F(SequenceTest, FirstCall) {
    Sequence seq;

    EXPECT_EQ(0, seq.Sns1(addr1)->GetLastUsed());
    EXPECT_EQ(1, seq.Sns1(addr2)->Next());
    EXPECT_EQ(123, seq.Sns1(addr3)->SetTo(123));

    uint8_t tid = 15;
    uint8_t aci = 3;
    EXPECT_EQ(0, seq.Sns2(addr1, tid)->GetLastUsed());
    EXPECT_EQ(1, seq.Sns4(addr2, aci)->Next());
    EXPECT_EQ(123, seq.Sns5()->SetTo(123));
}

TEST_F(SequenceTest, SetTo) {
    Sequence seq;
    uint8_t tid = 1;

    for (uint32_t val = 0; val < 10000; val += 100) {
        seq_t want = val % 4096;
        seq_t got1 = seq.Sns2(addr1, tid)->SetTo(val);
        seq_t got2 = seq.Sns2(addr1, tid)->GetLastUsed();
        EXPECT_EQ(want, got1);
        EXPECT_EQ(want, got2);
    }
}

TEST_F(SequenceTest, Sns1Next) {
    Sequence seq;

    for (uint32_t i = 0; i < 4095; i++) {
        seq_t want = i + 1;
        EXPECT_EQ(want, seq.Sns1(addr1)->Next());
    }
    EXPECT_EQ(4095, seq.Sns1(addr1)->GetLastUsed());

    seq.Sns1(addr1)->Next();
    EXPECT_EQ(0, seq.Sns1(addr1)->GetLastUsed());
}

TEST_F(SequenceTest, Sns4Next) {
    Sequence seq;

    uint8_t aci = 1;
    for (uint32_t i = 0; i < 1023; i++) {
        seq_t want = i + 1;
        EXPECT_EQ(want, seq.Sns4(addr1, aci)->Next());
    }
    EXPECT_EQ(1023, seq.Sns4(addr1, aci)->GetLastUsed());

    seq.Sns4(addr1, aci)->Next();
    EXPECT_EQ(0, seq.Sns4(addr1, aci)->GetLastUsed());
}

TEST_F(SequenceTest, Racing) {
    Sequence seq;
    seq.Sns1(addr1)->SetTo(10);
    seq.Sns1(addr2)->SetTo(20);

    for (uint32_t i = 0; i < 10; i++) {
        seq.Sns1(addr1)->Next();
    }
    EXPECT_EQ(seq.Sns1(addr1)->GetLastUsed(), seq.Sns1(addr2)->GetLastUsed());

    for (uint32_t i = 0; i < 10; i++) {
        seq.Sns1(addr1)->Next();
    }
    EXPECT_EQ(seq.Sns1(addr1)->GetLastUsed(), seq.Sns1(addr2)->GetLastUsed() + 10);

    for (uint32_t i = 0; i < 10; i++) {
        seq.Sns1(addr2)->Next();
    }
    EXPECT_EQ(seq.Sns1(addr1)->GetLastUsed(), seq.Sns1(addr2)->GetLastUsed());

    for (uint32_t i = 0; i < 10; i++) {
        seq.Sns1(addr2)->Next();
    }
    EXPECT_EQ(seq.Sns1(addr1)->GetLastUsed() + 10, seq.Sns1(addr2)->GetLastUsed());
}

TEST_F(SequenceTest, MultipleSNS) {
    Sequence seq;
    uint8_t tid = 0;
    uint8_t aci = 0;

    seq.Sns1(addr1)->SetTo(10);
    seq.Sns2(addr1, tid)->SetTo(20);
    seq.Sns4(addr1, aci)->SetTo(30);

    EXPECT_EQ(seq.Sns1(addr1)->GetLastUsed(), 10);
    EXPECT_EQ(seq.Sns2(addr1, tid)->GetLastUsed(), 20);
    EXPECT_EQ(seq.Sns4(addr1, aci)->GetLastUsed(), 30);
}

}  // namespace
}  // namespace common
}  // namespace wlan
