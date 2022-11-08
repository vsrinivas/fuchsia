// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This only exists as a DSO that sanitizer-module-loaded-test-helper explicitly
// depends on so we can verify load order on startup.

#include <zircon/compiler.h>

__EXPORT void extern_func() {}
