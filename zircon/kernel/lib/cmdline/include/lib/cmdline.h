// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CMDLINE_INCLUDE_LIB_CMDLINE_H_
#define ZIRCON_KERNEL_LIB_CMDLINE_INCLUDE_LIB_CMDLINE_H_

#include <stdint.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#include <optional>
#include <string_view>

#include <fbl/function.h>

// Cmdline is used to build and access the kernel command line.
//
// The underlying data is stored as a sequence of zero or more C strings followed by a final \0
// (i.e. an empty string).
//
// It can be accessed using the Get* methods or via data() and size().
//
// The Get* methods treat later values as overrides for earlier ones.
//
// For example, an empty command line is [\0], and a command line containing "a=b" is [a=b\0\0].
class Cmdline {
 public:
  static constexpr uint32_t kCmdlineMax = 4096;

  // Append |str| to the command line.
  //
  // |str| should contain "key=value" elements, separated by spaces.  Repeated spaces in |str| will
  // be combined.  Invalid characters will be replaced with '.'.
  //
  // For example:
  //
  //   Append("key=value  red foo=bar\n");
  //
  // will result in [key=value\0red=\0foo=bar.\0\0]
  //
  // Append may be called repeatedly. If |kCmdlineMax| is exceeded, will panic.
  //
  // The command line will always be properly terminated.
  void Append(const char* str);

  // Return the last value for |key| or nullptr if not found.
  //
  // When |key| is nullptr, the entire command line is returned.
  const char* GetString(const char* key) const;

  // read-only  access to the underlying data
  const char* data() const { return data_; }

  // Return the size of data() including the final \0.
  //
  // Guaranteed to be >= 1;
  size_t size() const;

 protected:
  // Adds the given character to data_ and updates length_. If the character would cause the buffer
  // to exceed kCmdlineMax, panic.
  void AddOrAbort(char c);

  // Find the key of the explicitly specified length in our list of command line
  // arguments.
  const char* FindKey(const char* key, size_t key_len) const;

  // Zero-initialize to ensure the |gCmdline| instance of this class lives in the BSS rather than
  // DATA segment so we don't bloat the kernel.
  char data_[kCmdlineMax]{};
  // Does not include the final \0.
  size_t length_{};
};

extern Cmdline gCmdline;

#endif  // ZIRCON_KERNEL_LIB_CMDLINE_INCLUDE_LIB_CMDLINE_H_
