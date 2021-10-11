// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_UTILS_LOGGING_H_
#define SRC_UI_SCENIC_LIB_UTILS_LOGGING_H_

#include <lib/syslog/cpp/macros.h>

#if !defined(USE_FLATLAND_VERBOSE_LOGGING)
#define FLATLAND_VERBOSE_LOG FX_EAT_STREAM_PARAMETERS(true)
#else
#define FLATLAND_VERBOSE_LOG FX_LOGS(INFO) << "................. "
#endif

#endif  // SRC_UI_SCENIC_LIB_UTILS_LOGGING_H_
