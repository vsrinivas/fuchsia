// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

// Declare placement allocation functions.
// Note: This library does not provide an implementation of these functions.
void* operator new(size_t size, void* ptr);
void* operator new[](size_t size, void* ptr);

// Declare (but don't define) non-throwing allocation functions.
// Note: This library does not provide an implementation of these functions.
namespace std {
struct nothrow_t;
} // namespace std
void* operator new(size_t size, const std::nothrow_t&) noexcept;
void* operator new[](size_t size, const std::nothrow_t&) noexcept;
