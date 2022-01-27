// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/fdio/cleanpath.h"

#include <limits.h>
#include <string.h>

namespace fdio_internal {

#define IS_SEPARATOR(c) ((c) == '/' || (c) == 0)

// Checks that if we increment this index forward, we'll
// still have enough space for a null terminator within
// PATH_MAX bytes.
#define CHECK_CAN_INCREMENT(i)         \
  if (unlikely((i) + 1 >= PATH_MAX)) { \
    return ZX_ERR_BAD_PATH;            \
  }

// Cleans an input path, transforming it to out, according to the
// rules defined by "Lexical File Names in Plan 9 or Getting Dot-Dot Right",
// accessible at: https://9p.io/sys/doc/lexnames.html
//
// Code heavily inspired by Go's filepath.Clean function, from:
// https://golang.org/src/path/filepath/path.go
//
// out is expected to be PATH_MAX bytes long.
// Sets is_dir to 'true' if the path is a directory, and 'false' otherwise.
zx_status_t cleanpath(const char* in, char* out, size_t* outlen, bool* is_dir) {
  if (in[0] == 0) {
    strcpy(out, ".");
    *outlen = 1;
    *is_dir = true;
    return ZX_OK;
  }

  bool rooted = (in[0] == '/');
  size_t in_index = 0;   // Index of the next byte to read
  size_t out_index = 0;  // Index of the next byte to write

  if (rooted) {
    out[out_index++] = '/';
    in_index++;
    *is_dir = true;
  }
  size_t dotdot = out_index;  // The output index at which '..' cannot be cleaned further.

  while (in[in_index] != 0) {
    *is_dir = true;
    if (in[in_index] == '/') {
      // 1. Reduce multiple slashes to a single slash
      CHECK_CAN_INCREMENT(in_index);
      in_index++;
    } else if (in[in_index] == '.' && IS_SEPARATOR(in[in_index + 1])) {
      // 2. Eliminate . path name elements (the current directory)
      CHECK_CAN_INCREMENT(in_index);
      in_index++;
    } else if (in[in_index] == '.' && in[in_index + 1] == '.' && IS_SEPARATOR(in[in_index + 2])) {
      CHECK_CAN_INCREMENT(in_index + 1);
      in_index += 2;
      if (out_index > dotdot) {
        // 3. Eliminate .. path elements (the parent directory) and the element that
        // precedes them.
        out_index--;
        while (out_index > dotdot && out[out_index] != '/') {
          out_index--;
        }
      } else if (rooted) {
        // 4. Eliminate .. elements that begin a rooted path, that is, replace /.. by / at
        // the beginning of a path.
        continue;
      } else if (!rooted) {
        if (out_index > 0) {
          out[out_index++] = '/';
        }
        // 5. Leave intact .. elements that begin a non-rooted path.
        out[out_index++] = '.';
        out[out_index++] = '.';
        dotdot = out_index;
      }
    } else {
      *is_dir = false;
      if ((rooted && out_index != 1) || (!rooted && out_index != 0)) {
        // Add '/' before normal path component, for non-root components.
        out[out_index++] = '/';
      }

      while (!IS_SEPARATOR(in[in_index])) {
        CHECK_CAN_INCREMENT(in_index);
        out[out_index++] = in[in_index++];
      }
    }
  }

  if (out_index == 0) {
    strcpy(out, ".");
    *outlen = 1;
    *is_dir = true;
    return ZX_OK;
  }

  // Append null character
  *outlen = out_index;
  out[out_index++] = 0;
  return ZX_OK;
}

}  // namespace fdio_internal
