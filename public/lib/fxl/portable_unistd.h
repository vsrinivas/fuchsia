// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_PORTABLE_UNISTD_H_
#define LIB_FXL_PORTABLE_UNISTD_H_

#include "lib/fxl/build_config.h"

#if defined(OS_WIN)
#include <io.h>
#include <direct.h>
#include <stdlib.h>

#define STDERR_FILENO _fileno(stderr)
#define PATH_MAX _MAX_PATH

#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define R_OK 4

#define mkdir(path, mode) _mkdir(path)

#else
#include <unistd.h>
#endif

#endif  // LIB_FXL_PORTABLE_UNISTD_H_
