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
    return false;                      \
  }

// Cleans an input path, transforming it to out, according to the
// rules defined by "Lexical File Names in Plan 9 or Getting Dot-Dot Right",
// accessible at: https://9p.io/sys/doc/lexnames.html
//
// Code heavily inspired by Go's filepath.Clean function, from:
// https://golang.org/src/path/filepath/path.go
//
// Sets is_dir to 'true' if the path is a directory, and 'false' otherwise.
bool CleanPath(const char* in, PathBuffer* out, bool* is_dir) {
  if (in[0] == 0) {
    out->Set(".");
    *is_dir = true;
    return true;
  }

  bool rooted = (in[0] == '/');
  size_t in_index = 0;  // Index of the next byte to read

  if (rooted) {
    out->Append('/');
    in_index++;
    *is_dir = true;
  }
  size_t dotdot = out->length();  // The output index at which '..' cannot be cleaned further.

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
      if (out->length() > dotdot) {
        // 3. Eliminate .. path elements (the parent directory) and the element that
        // precedes them.
        size_t last_elem = out->length() - 1;
        while (last_elem > dotdot && (*out)[last_elem] != '/') {
          last_elem--;
        }
        out->Resize(last_elem);
      } else if (rooted) {
        // 4. Eliminate .. elements that begin a rooted path, that is, replace /.. by / at
        // the beginning of a path.
        continue;
      } else if (!rooted) {
        if (out->length() > 0) {
          out->Append('/');
        }
        // 5. Leave intact .. elements that begin a non-rooted path.
        out->Append(std::string_view(".."));
        dotdot = out->length();
      }
    } else {
      *is_dir = false;
      if ((rooted && out->length() != 1) || (!rooted && out->length() != 0)) {
        // Add '/' before normal path component, for non-root components.
        out->Append('/');
      }

      while (!IS_SEPARATOR(in[in_index])) {
        CHECK_CAN_INCREMENT(in_index);
        out->Append(in[in_index++]);
      }
    }
  }

  if (out->length() == 0) {
    out->Append('.');
    *is_dir = true;
  }

  return true;
}

}  // namespace fdio_internal
