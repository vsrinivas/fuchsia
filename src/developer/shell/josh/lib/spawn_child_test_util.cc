// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used to test task.kill and task.killall
// an executable that does not end until it is killed
#include <zircon/syscalls.h>
#include <zircon/types.h>

int main(int argc, char** argv) { zx_nanosleep(zx_deadline_after(ZX_MIN(10))); }
