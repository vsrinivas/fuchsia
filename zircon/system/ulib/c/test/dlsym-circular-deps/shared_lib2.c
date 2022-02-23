// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

void test_func3(void);
__EXPORT
void test_func2(void) { test_func3(); }
