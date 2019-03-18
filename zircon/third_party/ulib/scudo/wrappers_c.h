//===-- wrappers_c.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_WRAPPERS_C_H_
#define SCUDO_WRAPPERS_C_H_

#include "platform.h"

#if SCUDO_ANDROID
// Bionic's mallinfo consists of 10 size_t (mallinfo(3) uses 10 int).
struct _mallinfo {
  size_t _[10];
};
#else
struct _mallinfo {
  int _[10];
};
#endif

#ifndef M_DECAY_TIME
#define M_DECAY_TIME -100
#endif

#ifndef M_PURGE
#define M_PURGE -101
#endif

#endif // SCUDO_WRAPPERS_C_H_
