// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper.h"

#include <cstdio>

#include <zxtest/cpp/zxtest.h>

int main(int argc, char** argv) {
    int res = zxtest::RunAllTests(argc, argv);
    zxtest::test::CheckAll();
    return res;
}
