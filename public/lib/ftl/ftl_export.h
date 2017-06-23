// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_FTL_EXPORT_H_
#define LIB_FTL_FTL_EXPORT_H_

#include "lib/ftl/build_config.h"

#ifdef OS_FUCHSIA
#define FTL_EXPORT __attribute__((visibility("default")))
#else
#define FTL_EXPORT
#endif

#endif  // LIB_FTL_FTL_EXPORT_H_
