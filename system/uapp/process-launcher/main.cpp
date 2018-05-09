// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/process/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <zircon/status.h>

int main(int argc, char** argv) {
    async::Loop loop;
    return loop.Run();
}
