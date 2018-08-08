// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Mark the symbols as exported to prevent the linker from stripping them.
#define EXPORT __attribute__((visibility("default")))
#define NOINLINE __attribute__((noinline))
