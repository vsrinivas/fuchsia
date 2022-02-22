// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_CLEANPATH_H_
#define LIB_FDIO_CLEANPATH_H_

#include <zircon/types.h>

#include <fbl/string_buffer.h>

namespace fdio_internal {

// PATH_MAX is defined in POSIX as being inclusive of the null terminator
// (unlike NAME_MAX and other constants which are exclusive). See the
// "Rationale" section of
// https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/limits.h.html for
// more background on this choice.  fbl::StringBuffer allocates space for a null
// terminator in addition to the requested size.
using PathBuffer = fbl::StringBuffer<PATH_MAX - 1>;

// Cleans an input path, transforming it to out, according to the
// rules defined by "Lexical File Names in Plan 9 or Getting Dot-Dot Right",
// accessible at: https://9p.io/sys/doc/lexnames.html
//
// Code heavily inspired by Go's filepath.Clean function, from:
// https://golang.org/src/path/filepath/path.go
//
// If the input cannot be parsed, returns false. Otherwise populates |out| with
// a clean path and sets is_dir to 'true' if the path is a directory.
bool CleanPath(const char* in, PathBuffer* out, bool* is_dir);

}  // namespace fdio_internal

#endif  // LIB_FDIO_CLEANPATH_H_
