// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if defined(__aarch64__)

#define hw_mb()    __asm__ volatile("dmb osh" : : : "memory")
#define hw_rmb()   __asm__ volatile("dmb oshld" : : : "memory")
#define hw_wmb()   __asm__ volatile("dmb oshst" : : : "memory")

#elif defined(__x86_64__)

#define hw_mb()    __asm__ volatile ("mfence" ::: "memory")
#define hw_rmb()   __asm__ volatile ("lfence" ::: "memory")
#define hw_wmb()   __asm__ volatile ("sfence" ::: "memory")

#endif