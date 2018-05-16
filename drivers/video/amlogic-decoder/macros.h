// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MACROS_H_
#define MACROS_H_

#include <ddk/debug.h>

#define DECODE_ERROR(fmt, ...) \
  zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

#endif  // MACROS_H_
