// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define ZX_PAGE_SHIFT (12UL)
#define ZX_PAGE_SIZE  (1UL << ZX_PAGE_SHIFT)
#define ZX_PAGE_MASK  (ZX_PAGE_SIZE - 1UL)

