// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utils/String8.h>

#include <cassert>
#include <codecvt>
#include <cwchar>
#include <locale>
#include <string>

namespace android {

String8::String8() = default;

status_t String8::appendFormat(const char* format, ...) {
  va_list ap;
  va_start(ap, format);

  status_t result = appendFormatV(format, ap);

  va_end(ap);
  return result;
}

status_t String8::appendFormatV(const char* format, va_list ap) {
  va_list tmp_ap;
  // ap is undefined after vsnprintf, so we need a copy here to avoid the secodn
  // vsnprintf accessing undefined ap.
  va_copy(tmp_ap, ap);
  int n = vsnprintf(nullptr, 0, format, tmp_ap);
  va_end(tmp_ap);
  if (n) {
    size_t old_length = size();

    // With -fno-exceptions, I believe the behavior will be to abort() the
    // process instead of throwing std::bad_alloc.
    reserve(old_length + n);

    // TODO: C++17 has a data() accessor that'll return non-const CharT*.
    // Once all relevant toolchains are C++17, we could switch to using that
    // here to avoid this allocation and copy, and just vsnprintf() directly
    // into the latter part of the string instead.

    // Similar to above, with -fno-exceptions, I believe the behavior will be to
    // abort() the proces instead of throwing std::bad_alloc.
    std::unique_ptr<char> temp = std::make_unique<char>(n + 1);

    int actual = vsnprintf(temp.get(), n + 1, format, ap);
    (void)actual;

    // Concurrent modification of the string by mulitple threads isn't
    // supported.
    assert(n == actual);

    // passing the "n" only to avoid forcing a re-count
    append(temp.get(), n);
  }
  return NO_ERROR;
}

const char* String8::string() const { return c_str(); }

}  // namespace android
