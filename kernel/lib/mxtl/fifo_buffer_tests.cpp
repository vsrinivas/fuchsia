// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <unittest.h>
#include <mxtl/fifo_buffer.h>

namespace {

struct Packet {
    int val;
    char name[16];

    Packet()
        : val(-1), name{"abc"} {}
};

static bool fifo_basic(void* context) {
    BEGIN_TEST;

    mxtl::FifoBuffer<Packet> fifo;
    fifo.Init(16u);

    EXPECT_EQ(true, fifo.is_empty(), "should be empty");

    Packet* p = nullptr;

    int loops = 1;
    int count = 0;

    for (;; ++loops) {

        p = fifo.push_tail();
        if (!p)
            break;

        EXPECT_EQ(-1, p->val, "ctor was not called");
        EXPECT_EQ('c', p->name[2], "ctor was not called");

        p->val = ++count;

        p = fifo.push_tail();
        if (!p)
            break;

        p->val = ++count;
        p = fifo.pop_head();
        EXPECT_NEQ(nullptr, p, "missing buffer");
        if (p)
            EXPECT_EQ(loops, p->val, "missing buffer");
    }

    EXPECT_EQ(true, fifo.is_full(), "should be full");

    EXPECT_EQ(16, loops, "slot count mismatch");

    EXPECT_NEQ(nullptr, fifo.peek_head(), "peek failed");
    if (fifo.peek_head())
        EXPECT_EQ(16, fifo.peek_head()->val, "peek failed");

    loops = 0;
    while (!fifo.is_empty()) {
        ++loops;
        p = fifo.pop_head();
    }

    EXPECT_EQ(16, loops, "bad number of buffers");

    fifo.clear();

    END_TEST;
}
} // namespace

UNITTEST_START_TESTCASE(fifo_buffer_tests)
UNITTEST("FIFO basic", fifo_basic)
UNITTEST_END_TESTCASE(fifo_buffer_tests, "fifotests", "FIFO Buffer Tests", NULL, NULL);
