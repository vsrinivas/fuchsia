// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_GLUE_BASE_LOGGING_H_
#define APPS_MOZART_GLUE_BASE_LOGGING_H_

#include "lib/ftl/logging.h"

#if 0
#define DVLOG(level) FTL_DLOG(INFO)
#define VLOG_IS_ON(level) true
#else
#define DVLOG(level) FTL_EAT_STREAM_PARAMETERS(true)
#define VLOG_IS_ON(level) false
#endif

#endif  // APPS_MOZART_GLUE_BASE_LOGGING_H_
