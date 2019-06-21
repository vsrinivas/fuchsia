// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// This is separate from lib/vdso.h so it can be used from userboot.

enum class VdsoVariant {
    FULL,
    TEST1,
    TEST2,
    COUNT
};
