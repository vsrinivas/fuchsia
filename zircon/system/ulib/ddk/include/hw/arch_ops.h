// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HW_ARCH_OPS_H_
#define HW_ARCH_OPS_H_

#if defined(__aarch64__)

#define hw_mb() __asm__ volatile("dmb sy" : : : "memory")
#define hw_rmb() __asm__ volatile("dmb ld" : : : "memory")
#define hw_wmb() __asm__ volatile("dmb st" : : : "memory")

#elif defined(__x86_64__)

#define hw_mb() __asm__ volatile("mfence" ::: "memory")
#define hw_rmb() __asm__ volatile("lfence" ::: "memory")
#define hw_wmb() __asm__ volatile("sfence" ::: "memory")

#endif

#endif  // HW_ARCH_OPS_H_
