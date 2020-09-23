// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <poll.h>
#include <sys/inotify.h>

#include "private.h"

int inotify_init() {
    return ERRNO(ENOSYS);
}
int inotify_init1(int flags) {
    return ERRNO(ENOSYS);
}
int inotify_add_watch(int fd, const char *pathname, uint32_t mask) {
    return ERRNO(ENOSYS);
}
int inotify_rm_watch(int fd, int wd) {
    return ERRNO(ENOSYS);
}
