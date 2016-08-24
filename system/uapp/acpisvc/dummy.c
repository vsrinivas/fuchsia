// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "processor.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>

int main(int argc, char** argv) {
    // The devmgr handles will autoclose, which will signal the
    // correct behavior.
    return 0;
}
