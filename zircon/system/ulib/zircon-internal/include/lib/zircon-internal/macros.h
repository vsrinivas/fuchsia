// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if !defined(countof)
#define countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define KB (1024UL)
#define MB (1024UL * KB)
#define GB (1024UL * MB)
