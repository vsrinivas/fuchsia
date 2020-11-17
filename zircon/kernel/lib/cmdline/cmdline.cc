// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/cmdline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>

#include <fbl/auto_call.h>

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

  const char* ptr = FindKey(key, sz);

  if (ptr != nullptr) {
    // Return a pointer to the data after the '='
    const char* data = ptr + sz + 1;
    ZX_DEBUG_ASSERT(data < (data_ + length_));
    return data;
  }

  return nullptr;
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

void Cmdline::ProcessRamReservations(const ProcessRamReservationsCbk& cbk) {
  constexpr const char kHeader[] = "kernel.ram.reserve.";
  constexpr size_t kHeaderLen = sizeof(kHeader) - 1;
  constexpr char kErasedArgFillChar = 'x';

  // Our internal length_ (which does not include the final \0 termination) must
  // be strictly smaller than our internal storage.
  ZX_DEBUG_ASSERT(length_ < sizeof(data_));

  size_t offset = 0;
  while (offset < length_) {
    char* arg = data_ + offset;
    size_t arg_len = strlen(arg);

    // If this is the final zero-length argument, then we are at the end of the
    // command line argument list.  Otherwise, make sure that we always
    // increment offset to point to the next argument.
    if (!arg_len) {
      break;
    }
    offset += arg_len + 1;

    // Does our argument start with our header?  If not, then just move on to
    // the next argument.
    if (strncmp(kHeader, arg, kHeaderLen)) {
      continue;
    }

    // If something goes wrong from here on out, be sure to log a warning and
    // erase the entry.
    auto cleanup = fbl::MakeAutoCall([arg, arg_len]() {
      printf("WARN - Reservation was rejected or encountered a parsing error.  \"%s\"\n", arg);
      memset(arg, kErasedArgFillChar, arg_len);
    });

    // Find the '=' in our argument.  If we cannot find one, then reservation is
    // malformed and should be skipped.
    char* equal = strchr(arg, '=');
    if (equal == nullptr) {
      continue;
    }

    // If the '=' comes right after the header, then the user failed to supply a
    // unique name to the reservation.  Warn and skip.
    const char* reservation_name = arg + kHeaderLen;
    if (reservation_name == equal) {
      continue;
    }

    // If this is not the final instance of the region key in our command line
    // arguments, then someone must have overridden the argument while building
    // the kernel command line.  Erase this entry and move on.
    if (arg != FindKey(arg, equal - arg)) {
      cleanup.cancel();
      memset(arg, kErasedArgFillChar, arg_len);
      continue;
    }

    // reservations are always of the form "size,placeholder".  If we fail to
    // find the "," separator, then this is an invalid region reservation and we
    // should skip it.
    const char* comma = strchr(equal + 1, ',');
    if (comma == nullptr) {
      continue;
    }

    // Parse our size
    const char* value = equal + 1;
    char* end;
    size_t size = strtoul(value, &end, 0);
    if (end != comma) {
      continue;
    }

    // Sanity check to make sure that the user passed the placeholder for the
    // allocated address.
    constexpr const char* kDynamicToken = "0xXXXXXXXXXXXXXXXX";
    if (strcmp(kDynamicToken, comma + 1)) {
      continue;
    }

    // Great, we have all of our details ready to go.  Invoke the user-supplied
    // callback.
    std::string_view name_view(reservation_name, equal - reservation_name);
    auto result = cbk(size, name_view);

    // If the user accepted the reservation, record the base address and we are
    // done.
    if (result.has_value()) {
      char* dst = const_cast<char*>(comma + 1);
      size_t space = length_ - (dst - data_);
      snprintf(dst, space, "0x%016lx", result.value());
      cleanup.cancel();
    }
  }
}

size_t Cmdline::size() const { return length_ + 1; }

void Cmdline::AddOrAbort(char c) {
  if (length_ < kCmdlineMax - 1) {
    data_[length_++] = c;
  } else {
    ZX_PANIC("cmdline overflow");
  }
}

const char* Cmdline::FindKey(const char* key, size_t key_len) const {
  ZX_DEBUG_ASSERT(key && key_len);

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
    if (!strncmp(ptr, key, key_len) && ptr[key_len] == '=') {
      break;
    }
  }

  return ptr;
}
