// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_TESTS_UTIL_TEST_UTILS_H_
#define LIB_FIDL_CPP_BINDINGS_TESTS_UTIL_TEST_UTILS_H_

#include <string>

#include <zx/channel.h>

namespace fidl {
namespace test {

// Writes a message to |handle| with message data |text|. Returns true on
// success.
bool WriteTextMessage(const zx::channel& handle, const std::string& text);

// Reads a message from |handle|, putting its contents into |*text|. Returns
// true on success. (This blocks if necessary and will call |MojoReadMessage()|
// multiple times, e.g., to query the size of the message.)
bool ReadTextMessage(const zx::channel& handle, std::string* text);

// Discards a message from |handle|. Returns true on success. (This does not
// block. It will fail if no message is available to discard.)
bool DiscardMessage(const zx::channel& handle);

}  // namespace test
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_TESTS_UTIL_TEST_UTILS_H_
