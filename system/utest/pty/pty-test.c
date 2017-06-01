// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include <mxio/io.h>

#include <magenta/device/pty.h>

// returns an int to avoid sign errors from ASSERT_*()
static int fd_signals(int fd) {
    uint32_t signals = 0;
    mxio_wait_fd(fd, 0, &signals, 0);
    return signals;
}

static int write_full(int fd) {
    char tmp[300];
    memset(tmp, 0x33, sizeof(tmp));
    int total = 0;
    for (;;) {
        int r = write(fd, tmp, sizeof(tmp));
        if (r < 0) {
            if (errno == EAGAIN) {
                break;
            }
            return -errno;
        }
        if (r == 0) {
            break;
        }
        total += r;
    }
    return total;
}

static int read_all(int fd) {
    char tmp[700];
    int total = 0;
    for (;;) {
        int r = read(fd, tmp, sizeof(tmp));
        if (r < 0) {
            if (errno == EAGAIN) {
                break;
            }
            return -errno;
        }
        if (r == 0) {
            break;
        }
        for (int n = 0; n < r; n++) {
            if (tmp[n] != 0x33) {
                return -EFAULT;
            }
        }
        total += r;
    }
    return total;
}

static bool pty_test(void) {
    BEGIN_TEST;

    int ps = open("/dev/misc/ptmx", O_RDWR | O_NONBLOCK);
    ASSERT_GE(ps, 0, "");

    int pc = openat(ps, "0", O_RDWR | O_NONBLOCK);
    ASSERT_GE(pc, 0, "");

    char tmp[32];

    ASSERT_EQ(fd_signals(ps), POLLOUT, "");
    ASSERT_EQ(fd_signals(pc), POLLOUT, "");

    // nothing to read
    ASSERT_EQ(read(ps, tmp, 32), -1, "");
    ASSERT_EQ(errno, EAGAIN, "");
    ASSERT_EQ(read(pc, tmp, 32), -1, "");
    ASSERT_EQ(errno, EAGAIN, "");

    // write server, read client
    ASSERT_EQ(write(ps, "xyzzy", 5), 5, "");
    ASSERT_EQ(fd_signals(pc), POLLIN | POLLOUT, "");

    memset(tmp, 0xee, 5);
    ASSERT_EQ(read(pc, tmp, 5), 5, "");
    ASSERT_EQ(memcmp(tmp, "xyzzy", 5), 0, "");
    ASSERT_EQ(fd_signals(pc), POLLOUT, "");

    // write client, read server
    ASSERT_EQ(write(pc, "xyzzy", 5), 5, "");
    ASSERT_EQ(fd_signals(ps), POLLIN | POLLOUT, "");

    memset(tmp, 0xee, 5);
    ASSERT_EQ(read(ps, tmp, 5), 5, "");
    ASSERT_EQ(memcmp(tmp, "xyzzy", 5), 0, "");
    ASSERT_EQ(fd_signals(ps), POLLOUT, "");

    // write server until full, then drain
    ASSERT_EQ(write_full(ps), 4096, "");
    ASSERT_EQ(fd_signals(ps), 0, "");
    ASSERT_EQ(read_all(pc), 4096, "");
    ASSERT_EQ(fd_signals(ps), POLLOUT, "");

    // write client until full, then drain
    ASSERT_EQ(write_full(pc), 4096, "");
    ASSERT_EQ(fd_signals(pc), 0, "");
    ASSERT_EQ(read_all(ps), 4096, "");
    ASSERT_EQ(fd_signals(pc), POLLOUT, "");

    // verify no events pending
    uint32_t events;
    ASSERT_EQ(ioctl_pty_read_events(pc, &events), (ssize_t)sizeof(events), "");
    ASSERT_EQ(events, 0u, "");

    // write a ctrl-c
    ASSERT_EQ(write(ps, "\x03", 1), 1, "");

    // should be an event now
    ASSERT_EQ(ioctl_pty_read_events(pc, &events), (ssize_t)sizeof(events), "");
    ASSERT_EQ(events, PTY_EVENT_INTERRUPT, "");

    // should vanish once we read it
    ASSERT_EQ(ioctl_pty_read_events(pc, &events), (ssize_t)sizeof(events), "");
    ASSERT_EQ(events, 0u, "");

    // write something containing a special char
    // should write up to and including the special char
    // converting the special char to a signal
    ASSERT_EQ(write(ps, "hello\x03world", 11), 6, "");
    ASSERT_EQ(read(pc, tmp, 6), 5, "");
    ASSERT_EQ(memcmp(tmp, "hello", 5), 0, "");
    ASSERT_EQ(ioctl_pty_read_events(pc, &events), (ssize_t)sizeof(events), "");
    ASSERT_EQ(events, PTY_EVENT_INTERRUPT, "");


    pty_window_size_t ws;
    ASSERT_EQ(ioctl_pty_get_window_size(pc, &ws), (ssize_t)sizeof(ws), "");
    ASSERT_EQ(ws.width, 0u, "");
    ASSERT_EQ(ws.height, 0u, "");
    ws.width = 80;
    ws.height = 25;
    ASSERT_EQ(ioctl_pty_set_window_size(ps, &ws), 0, "");
    ASSERT_EQ(ioctl_pty_get_window_size(pc, &ws), (ssize_t)sizeof(ws), "");
    ASSERT_EQ(ws.width, 80u, "");
    ASSERT_EQ(ws.height, 25u, "");

    // verify that we don't get events for special chars in raw mode
    pty_clr_set_t cs = {
        .clr = 0,
        .set = PTY_FEATURE_RAW,
    };
    ASSERT_EQ(ioctl_pty_clr_set_feature(pc, &cs), 0, "");
    ASSERT_EQ(write(ps, "\x03", 1), 1, "");
    ASSERT_EQ(read(pc, tmp, 1), 1, "");
    ASSERT_EQ(tmp[0], '\x03', "");
    ASSERT_EQ(ioctl_pty_read_events(pc, &events), (ssize_t)sizeof(events), "");
    ASSERT_EQ(events, 0u, "");

    // create a second client
    int pc1 = openat(pc, "1", O_RDWR | O_NONBLOCK);
    ASSERT_GE(pc1, 0, "");

    // reads/writes to non-active client should block
    ASSERT_EQ(fd_signals(pc1), 0, "");
    ASSERT_EQ(write(pc1, "test", 4), -1, "");
    ASSERT_EQ(errno, EAGAIN, "");
    ASSERT_EQ(read(pc1, tmp, 4), -1, "");
    ASSERT_EQ(errno, EAGAIN, "");

    uint32_t n = 2;
    ASSERT_EQ(ioctl_pty_make_active(pc, &n), MX_ERR_NOT_FOUND, "");

    // non-controlling client cannot change active client
    ASSERT_EQ(ioctl_pty_make_active(pc1, &n), MX_ERR_ACCESS_DENIED, "");

    // but controlling client can
    n = 1;
    ASSERT_EQ(ioctl_pty_make_active(pc, &n), MX_OK, "");
    ASSERT_EQ(fd_signals(pc), 0, "");
    ASSERT_EQ(fd_signals(pc1), POLLOUT, "");
    ASSERT_EQ(write(pc1, "test", 4), 4, "");
    ASSERT_EQ(read(ps, tmp, 4), 4, "");
    ASSERT_EQ(memcmp(tmp, "test", 4), 0, "");

    // make sure controlling client observes departing active client
    close(pc1);
    ASSERT_EQ(fd_signals(pc), POLLHUP | POLLPRI, "");
    ASSERT_EQ(ioctl_pty_read_events(pc, &events), (ssize_t)sizeof(events), "");
    ASSERT_EQ(events, PTY_EVENT_HANGUP, "");

    // verify that server observes depature of last client
    close(pc);
    ASSERT_EQ(fd_signals(ps), POLLHUP | POLLIN, "");

    close(ps);

    END_TEST;
}

BEGIN_TEST_CASE(pty_tests)
RUN_TEST(pty_test)
END_TEST_CASE(pty_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
