// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/cmdline.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>

Cmdline gCmdline;

void Cmdline::Append(const char* str) {
  if (str == nullptr || *str == 0) {
    return;
  }

  bool found_equal = false;
  for (;;) {
    unsigned c = *str++;
    if (c == 0) {
      // Finish an in-progress argument.
      if (length_ > 0 && data_[length_ - 1] != 0) {
        if (!found_equal) {
          AddOrAbort('=');
        }
        // Terminate the string.
        AddOrAbort(0);
      }
      break;
    }

    if (c == '=') {
      found_equal = true;
    } else if ((c < ' ') || (c > 127)) {
      // It's a special character of some kind.
      if ((c == '\n') || (c == '\r') || (c == '\t')) {
        c = ' ';
      } else {
        c = '.';
      }
    }

    if (c == ' ') {
      // Spaces become \0's, but do not double up.
      if (length_ == 0 || (data_[length_ - 1] == 0)) {
        // No need to add another terminator, so loop back to the start.
        continue;
      }

      if (!found_equal) {
        AddOrAbort('=');
      } else {
        found_equal = false;
      }
      // Add the terminator.
      AddOrAbort(0);
      continue;
    }

    AddOrAbort(static_cast<char>(c));
  }
}

const char* Cmdline::GetString(const char* key) const {
  if (!key) {
    return data_;
  }

  const size_t sz = strlen(key);
  if (sz == 0) {
    return nullptr;
  }

  if (length_ == 0) {
    return nullptr;
  }

  // Start at the end and work backwards so that repeated values appended later
  // to the command line override earlier settings.
  const char* ptr = data_ + length_;
  for (;;) {
    // At the top of the loop, we're always at the first character of the item
    // *after* the one we're going to search next. This is, pointing at the
    // character one beyond the \0 of the string to be considered next. On the
    // first time through the loop, we'll be pointing at the extra \0 that
    // terminates the whole buffer.

    --ptr;  // Step back to the \0 of the previous item.
    --ptr;  // Step back to the last character of the previous item.

    if (ptr < data_) {
      // If beyond the beginning of the data, the key was not found.
      return nullptr;
    }

    // Walk backwards either to the terminator of the item preceding the one
    // we're on, or to the beginning of the buffer.
    while (ptr > data_ && *ptr != 0) {
      --ptr;
    }

    // If not at the first character of the buffer, then increment back past the
    // terminator of the previous item to the first character of the string.
    if (ptr != data_) {
      ++ptr;
    }
    if (!strncmp(ptr, key, sz) && ptr[sz] == '=') {
      break;
    }
  }
  ptr += sz;
  ptr++;  // Increment past the '='.
  return ptr;
}

bool Cmdline::GetBool(const char* key, bool default_value) const {
  const char* value = GetString(key);
  if (value == nullptr) {
    return default_value;
  }
  if ((strcmp(value, "0") == 0) || (strcmp(value, "false") == 0) || (strcmp(value, "off") == 0)) {
    return false;
  }
  return true;
}

uint64_t Cmdline::GetUInt64(const char* key, uint64_t default_value) const {
  const char* value_str = GetString(key);
  if (value_str == nullptr || *value_str == '\0') {
    return default_value;
  }

  char* end;
  static_assert(sizeof(unsigned long int) == sizeof(uint64_t));
  uint64_t value = strtol(value_str, &end, 0);
  if (*end != '\0') {
    return default_value;
  }
  return value;
}

uint32_t Cmdline::GetUInt32(const char* key, uint32_t default_value) const {
  return static_cast<uint32_t>(Cmdline::GetUInt64(key, default_value));
}

size_t Cmdline::size() const {
  return length_ + 1;
}


void Cmdline::AddOrAbort(char c) {
  if (length_ < kCmdlineMax - 1) {
    data_[length_++] = c;
  } else {
    ZX_PANIC("cmdline overflow");
  }
}
