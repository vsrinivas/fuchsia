// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/types.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>

#include <mxalloc/new.h>

#include <mxio/debug.h>

#include <mxtl/unique_ptr.h>

#include <fs/vfs-dispatcher.h>

#include <unittest/unittest.h>

#undef MXDEBUG
#define MXDEBUG 0

// multithreaded dispatcher (vfs-dispatch) test suite

static constexpr auto MAX_MSG = 120;
static constexpr auto STR_DATA = "testdata";
static constexpr auto STR_KILL = "exit";
static constexpr auto kMaxFlushTime = 15; // wait at most 15 sec for messages to flush

class Msg : public mx_port_packet_t {
public:
    char str[64];
    unsigned idx;
    unsigned worker;

    Msg(unsigned _idx, const char* _str, unsigned _worker):
        idx(_idx), worker(_worker) {
        type = MX_PORT_PKT_TYPE_USER;
        strcpy(str, _str);
    }
};

class Handler {
    cnd_t writer_finished_cond_;
    mtx_t writer_finished_lock_;
    unsigned writer_count_;

public:
    unsigned counts[MAX_MSG];

    void signal_finished() {
        mtx_lock(&writer_finished_lock_);
        writer_count_--;
        cnd_signal(&writer_finished_cond_);
        mtx_unlock(&writer_finished_lock_);
    }

    bool wait_for_finish() {
        mx_status_t status;

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += kMaxFlushTime;

        mtx_lock(&writer_finished_lock_);
        while(writer_count_ > 0) {
            status = cnd_timedwait(&writer_finished_cond_, &writer_finished_lock_, &ts);
            ASSERT_EQ(status, 0, "");
        }
        mtx_unlock(&writer_finished_lock_);
        return true;
    }

    Handler(unsigned n_writers) {
        mtx_init(&writer_finished_lock_, mtx_plain);
        cnd_init(&writer_finished_cond_);
        writer_count_ = n_writers;
        memset(counts, 0, sizeof(counts));
    }
};

// we write operations down a channel, which result in callbacks

// to make sure we've given the channel a chance to clear, we send
// a final message with a sentinel value which signals a "done" condition

// the tests wait for all writers to report finished before tearing down
// the dispatcher

static bool signal_finished(mx_handle_t ch) {
    Msg pmsg(0, STR_KILL, 0);
    mx_status_t status;
    status = mx_channel_write(ch, 0u, &pmsg, sizeof(pmsg), nullptr, 0u);
    ASSERT_EQ(status, NO_ERROR, "");
    return true;
}


typedef mx_status_t (*handler_cb_t)(Msg* msg, mx_handle_t h, void* cookie);
static mx_status_t handler_cb(Msg* msg, mx_handle_t h, void* cookie)
{
    Handler* handler = (Handler*)cookie;
    if (strcmp(msg->str, STR_KILL) == 0) {
        // this is the dispatch from the last message sent. signal
        // that this part of the test is over.
        handler->signal_finished();
    } else {
        // after several levels of indirection, receive a message that
        // contains a unique index [0,MAX_MSG-]; bump the handler counts
        // for that index. we should get one bump per bucket.
        ASSERT_LT(msg->idx, MAX_MSG, "");
        ASSERT_EQ(strcmp(msg->str, STR_DATA), 0, "channel read bad string payoad");
        xprintf("worker %u: inc %u\n", msg->worker, msg->idx);
        handler->counts[msg->idx]++;
        // one thread can race through most of our callbacks; yeild to make
        // sure we mix things up a little
        thrd_yield();
    }

    return NO_ERROR;
}

static mx_status_t disp_cb(mx_handle_t h, void* handler_cb, void* handler_data) {
    handler_cb_t cb = (handler_cb_t)handler_cb;

    // read the message and call the handler
    ASSERT_NEQ(h, 0, "unexpected handle close in dispatcher");
    Msg imsg(0, "", 0);
    uint32_t dsz = sizeof(imsg);
    mx_status_t r;
    r = mx_channel_read(h, 0u, &imsg, nullptr, dsz, 0, &dsz, nullptr);
    ASSERT_EQ(r, 0, "channel read failed");
    ASSERT_EQ(dsz, sizeof(imsg), "channel read unexpected length");
    ASSERT_LT(imsg.idx, MAX_MSG, "channel read bad index payload");

    r = cb(&imsg, h, handler_data);
    ASSERT_EQ(r, NO_ERROR, "dispatch callback");

    r = mx_channel_write(h, 0, &imsg, sizeof(imsg), nullptr, 0);
    ASSERT_EQ(r, NO_ERROR, "channel reply");

    return r;
}

bool test_multi_basic(void) {
    // send MAX_MSG indexed writes down a channel attached to a dispatcher
    // attached handler will bump a counter for each dispatched index
    // make sure we get one bump for each and every message
    static constexpr unsigned DISPATCH_POOL_SIZE = 4;

    BEGIN_TEST;

    // create dispatcher
    mx_status_t status;

    mxtl::unique_ptr<fs::Dispatcher> disp;
    ASSERT_EQ(NO_ERROR, fs::VfsDispatcher::Create(disp_cb, DISPATCH_POOL_SIZE, &disp), "");

    // create a channel; write to one end, bind the other to the server port
    mx_handle_t ch[2];
    status = mx_channel_create(0u, &ch[0], &ch[1]);
    ASSERT_EQ(status, NO_ERROR, "");

    // associate a handler object that will track state
    Handler handler(1);
    status = disp->AddVFSHandler(ch[1], (void*)handler_cb, (void*)&handler);
    ASSERT_EQ(status, NO_ERROR, "");

    // write MAX_MSG messages -- should result in all handler counts == 1
    for (auto msgno=0; msgno<MAX_MSG; msgno++) {
        Msg omsg(msgno, STR_DATA, 0);
        status = mx_channel_write(ch[0], 0u, &omsg, sizeof(omsg), nullptr, 0u);
        ASSERT_EQ(status, NO_ERROR, "");
        thrd_yield();
    }
    signal_finished(ch[0]);

    handler.wait_for_finish();

    // tear down the dispatcher object (closes and waits for thread pool)
    disp = nullptr;

    status = mx_handle_close(ch[0]);
    ASSERT_EQ(status, NO_ERROR, "");

    // when all callbacks have finished, the handler counts
    // should all have been bumped
    for (auto i=0; i<MAX_MSG; i++) {
        ASSERT_EQ(handler.counts[i], 1, "");
    }

    END_TEST;
}


struct Work {
    uint32_t worker;
    uint32_t iter;
    mx_handle_t ch;
    uint32_t* idx;
    uint32_t idx_len;
};

static int parallel_writer_thread(void* arg) {
    Work* work = (Work*)arg;
    // write a random subset of MAX_MSG messages
    xprintf("WORKER %d: ch: %u idx: %p\n", work->worker, work->ch, work->idx);
    for (uint32_t iter=0; iter<work->iter; iter++) {
        for (uint32_t i=0; i<work->idx_len; i++) {
            Msg omsg(work->idx[i], STR_DATA, work->worker);

            xprintf("write msg %d\n", work->idx[i]);
            mx_status_t status;
            status = mx_channel_write(work->ch, 0u, &omsg, sizeof(omsg), nullptr, 0u);
            ASSERT_EQ(status, NO_ERROR, "");

            thrd_yield();
        }
    }
    signal_finished(work->ch);
    return true;
}

static bool parallel_write(mx_handle_t ch, Handler* handler,
                           uint32_t idx[],
                           uint32_t idx_len, uint32_t n_writers, uint32_t iter) {
    ASSERT_EQ(idx_len % n_writers, 0, "msg count must be multiple of pool size");
    uint32_t n_work = idx_len / n_writers;
    Work work[n_writers];
    for (uint32_t th=0; th < n_writers; th++) {
        work[th].worker = th;
        work[th].iter = iter;
        work[th].ch = ch;
        work[th].idx = idx + (th*n_work);
        work[th].idx_len = n_work;
    }

    thrd_t t[n_writers];

    // spin off your workers
    for (uint32_t th=0; th < n_writers; th++) {
        char name[128];
        snprintf(name, sizeof(name), "th-%d", th);
        int status = thrd_create_with_name(&t[th], (thrd_start_t)parallel_writer_thread, (void*)(work+th), name);
        ASSERT_EQ(status, thrd_success, "");
    }

    // wait for all of the workers t signal they're done
    handler->wait_for_finish();

    // wait for the writer threads to exit
    for (uint32_t th=0; th<n_writers; th++) {
        int rc;
        int status = thrd_join(t[th], &rc);
        ASSERT_EQ(status, thrd_success, "");
        ASSERT_EQ(rc, true, "");
    }
    return true;
}

bool test_multi_multi(void) {
    // similar to multi_basic, only partition the work of sending
    // messages among several threads and randomize the order of
    // the messages being sent
    static constexpr unsigned DISPATCH_POOL_SIZE = 4;
    static constexpr unsigned WRITER_POOL_SIZE = 6;
    static constexpr unsigned WRITE_ITER = 5;

    BEGIN_TEST;

    // create dispatcher
    mx_status_t status;

    mxtl::unique_ptr<fs::Dispatcher> disp;
    ASSERT_EQ(NO_ERROR, fs::VfsDispatcher::Create(disp_cb, DISPATCH_POOL_SIZE, &disp), "");

    // create a channel; write to one end, bind the other to the server port
    mx_handle_t ch[2];
    status = mx_channel_create(0u, &ch[0], &ch[1]);
    ASSERT_EQ(status, NO_ERROR, "");

    // associate a handler object that will track state
    Handler handler(WRITER_POOL_SIZE);
    status = disp->AddVFSHandler(ch[1], (void*)handler_cb, (void*)&handler);
    ASSERT_EQ(status, NO_ERROR, "");

    // make sure the counters get bumped in random order
    uint32_t idx[MAX_MSG];
    for (uint32_t i=0; i<countof(idx); i++) {
        idx[i] = i;
    }
    for (uint32_t i=0; i<countof(idx); i++) {
        auto i1 = rand() % MAX_MSG;
        auto i2 = rand() % MAX_MSG;
        auto tmp = idx[i1];
        idx[i1] = idx[i2];
        idx[i2] = tmp;
    }

    parallel_write(ch[0], &handler, idx, countof(idx), WRITER_POOL_SIZE, WRITE_ITER);

    // tear down the dispatcher object (closes and waits for thread pool)
    disp = nullptr;

    status = mx_handle_close(ch[0]);
    ASSERT_EQ(status, NO_ERROR, "");

    // all counts should be bumped == WRITE_ITER
    for (auto i=0; i<MAX_MSG; i++) {
        ASSERT_EQ(handler.counts[i], WRITE_ITER, "");
    }

    END_TEST;
}

BEGIN_TEST_CASE(multi_dispatch_tests)
RUN_TEST_MEDIUM(test_multi_basic)
RUN_TEST_MEDIUM(test_multi_multi)
END_TEST_CASE(multi_dispatch_tests)
