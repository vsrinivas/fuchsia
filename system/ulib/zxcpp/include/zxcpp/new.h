// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

// Fake std::nothrow_t without a standard C++ library.
namespace std {
struct nothrow_t {};
}  // namespace std

// The kernel does not want non-AllocCheckered non-placement new
// overloads, but userspace can have them.
#if !_KERNEL
void* operator new(size_t);
void* operator new[](size_t);
#endif // !_KERNEL

void* operator new(size_t, const std::nothrow_t&) noexcept;
void* operator new[](size_t, const std::nothrow_t&) noexcept;

void* operator new(size_t, void *ptr);

void* operator new[](size_t, void *ptr);

void operator delete(void *p);
void operator delete[](void *p);
void operator delete(void *p, size_t);
void operator delete[](void *p, size_t);
