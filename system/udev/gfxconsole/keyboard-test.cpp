// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>
#include <unistd.h>
#include <unittest/unittest.h>

#include "keyboard.h"

namespace {

void keypress_handler(uint8_t keycode, int modifiers) {
}

// Currently this just tests that the keyboard input thread exits when it
// reads EOF.
bool test_keyboard_input_thread() {
    BEGIN_TEST;

    int pipe_fds[2];
    int rc = pipe(pipe_fds);
    EXPECT_EQ(rc, 0, "");

    auto* args = new vc_input_thread_args;
    args->fd = pipe_fds[1];
    args->keypress_handler = keypress_handler;
    thrd_t thread;
    int ret = thrd_create_with_name(&thread, vc_input_thread, args, "input");
    EXPECT_EQ(ret, thrd_success, "");

    ret = close(pipe_fds[0]);
    EXPECT_EQ(ret, 0, "");

    EXPECT_EQ(thrd_join(thread, &ret), thrd_success, "");

    END_TEST;
}

BEGIN_TEST_CASE(gfxconsole_keyboard_tests)
RUN_TEST(test_keyboard_input_thread)
END_TEST_CASE(gfxconsole_keyboard_tests)

}
