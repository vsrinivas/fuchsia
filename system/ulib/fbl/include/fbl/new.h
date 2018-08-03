// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// When building with the standard C++ library available, use its declarations.
// Otherwise code that includes both this file and <new> might have conflicts.
#if __has_include(<new>)

#include <new>

#else  // ! __has_include(<new>)

// No standard library in this build, so declare them locally.

#include <stddef.h>

// Declare placement allocation functions.
// Note: This library does not provide an implementation of these functions.
void* operator new(size_t size, void* ptr) noexcept;
void* operator new[](size_t size, void* ptr) noexcept;

// Declare (but don't define) non-throwing allocation functions.
// Note: This library does not provide an implementation of these functions.
namespace std {
struct nothrow_t;
} // namespace std
void* operator new(size_t size, const std::nothrow_t&) noexcept;
void* operator new[](size_t size, const std::nothrow_t&) noexcept;

#endif  //  __has_include(<new>)
