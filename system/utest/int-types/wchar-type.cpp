// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file explicitly tests that <wchar.h> is correct and does not
// have an implicit dependency on <stdint.h> or some other header.

// As such, do not include any other header in this file.


// Exact values etc. are checked in the other file where access to
// other constants is easy.
#include <wchar.h>

wchar_t cpp_wchar_t_min = WCHAR_MIN;
wchar_t cpp_wchar_t_max = WCHAR_MAX;
