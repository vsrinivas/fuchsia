// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PERFORMANCE_MEMORY_PROFILE_TRACE_CONSTANTS_H_
#define SRC_PERFORMANCE_MEMORY_PROFILE_TRACE_CONSTANTS_H_

// Message names.
constexpr char LAYOUT[] = "layout";
constexpr char ALLOC[] = "alloc";
constexpr char DEALLOC[] = "dealloc";

// Attribute names.
constexpr char TRACE_ID[] = "trace_id";
constexpr char ADDR[] = "addr";
constexpr char SIZE[] = "size";

#endif  // SRC_PERFORMANCE_MEMORY_PROFILE_TRACE_CONSTANTS_H_
