// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define PAGE_SIZE (4096)
#define PAGE_MASK (PAGE_SIZE - 1)

#define BYTES_TO_PAGES(n) (((n) + PAGE_MASK) / PAGE_SIZE)

// Ensure there are two pages preceeding the
// Ramdisk so that the kernel start code can
// use them to prepend bootdata items if desired.
#define FRONT_PAGES (2)
#define FRONT_BYTES (PAGE_SIZE * FRONT_PAGES)