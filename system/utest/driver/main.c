// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This boiler-plate main.c is needed for standalone drivers to
// operate correctly when linked against libdriver.so

// Soon drivers will become shared libraries and this will go away.

#include <ddk/driver.h>

int devhost_init(void);
int devhost_cmdline(int argc, char** argv);
int devhost_start(void);

extern mx_driver_t __start_builtin_drivers[] __WEAK;
extern mx_driver_t __stop_builtin_drivers[] __WEAK;

static void init_builtin_drivers(void) {
    mx_driver_t* drv;
    for (drv = __start_builtin_drivers; drv < __stop_builtin_drivers; drv++) {
        driver_add(drv);
    }
}

int main(int argc, char** argv) {
    int r;
    if ((r = devhost_init()) < 0) {
        return r;
    }
    if ((r = devhost_cmdline(argc, argv)) < 0) {
        return r;
    }
    init_builtin_drivers();
    return devhost_start();
}
