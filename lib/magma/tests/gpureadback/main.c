// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gpureadback.h>
#include <fcntl.h>
#include <stdio.h>

int main(void) {
    int fd = open("/dev/dri/card0", O_RDWR);
    printf("got fd %d\n", fd);
    return test_gpu_readback(fd);
}
