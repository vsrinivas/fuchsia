// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PROC_LIB_SYNCIO_STUB_MISSING_INCLUDES_H_
#define SRC_PROC_LIB_SYNCIO_STUB_MISSING_INCLUDES_H_

// Adding includes that are not detected by rust-bindings because they are
// defined using functions

#include <lib/zxio/types.h>

const zxio_shutdown_options_t _ZXIO_SHUTDOWN_OPTIONS_READ = ZXIO_SHUTDOWN_OPTIONS_READ;
#undef ZXIO_SHUTDOWN_OPTIONS_READ
const zxio_shutdown_options_t ZXIO_SHUTDOWN_OPTIONS_READ = _ZXIO_SHUTDOWN_OPTIONS_READ;

const zxio_shutdown_options_t _ZXIO_SHUTDOWN_OPTIONS_WRITE = ZXIO_SHUTDOWN_OPTIONS_WRITE;
#undef ZXIO_SHUTDOWN_OPTIONS_WRITE
const zxio_shutdown_options_t ZXIO_SHUTDOWN_OPTIONS_WRITE = _ZXIO_SHUTDOWN_OPTIONS_WRITE;

#endif  // SRC_PROC_LIB_SYNCIO_STUB_MISSING_INCLUDES_H_
