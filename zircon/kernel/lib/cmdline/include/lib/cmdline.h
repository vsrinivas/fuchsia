// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CMDLINE_INCLUDE_LIB_CMDLINE_H_
#define ZIRCON_KERNEL_LIB_CMDLINE_INCLUDE_LIB_CMDLINE_H_

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include <zircon/compiler.h>

// Cmdline is used to build and access the kernel command line.
//
// The underlying data is stored as a sequence of zero or more C strings followed by a final \0
// (i.e. an empty string).
//
// It can be accessed using the Get* methods or via data() and size().
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
  // Append may be called repeatedly. If |kCmdlineMax| is exceeded, the last key-value pair may be
  // truncated.  If |str| is too big or not \0-terminated, it will be truncated so that the command
  // line does not exceed |kCmdlineMax|.
  //
  // The command line will always be propertly terminated.
  void Append(const char* str);

  // Return the first value for |key| or nullptr if not found.
  //
  // When |key| is nullptr, the entire command line is returned.
  const char* GetString(const char* key) const;

  // Return the first value for |key| or |default_value| if not found.
  //
  // "0", "false", and "off" are considered false.  All other values are considered true.
  bool GetBool(const char* key, bool default_value) const;

  // Return the first value for |key| or |default_value| if not found.
  uint32_t GetUInt32(const char* key, uint32_t default_value) const;

  // Return the first value for |key| or |default_value| if not found.
  uint64_t GetUInt64(const char* key, uint64_t default_value) const;

  // Returns a pointer to the command line.  This is a sequence of zero or more \0-terminated
  // strings followed by a \0.
  const char* data() const { return data_; }

  // Return the size of data() including the final \0.
  //
  // Guaranteed to be >= 1;
  size_t size() const;

 private:
  // Zero-initialize to ensure the |gCmdline| instance of this class lives in the BSS rather than
  // DATA segment so we don't bloat the kernel.
  char data_[kCmdlineMax]{};
  // Does not include the final \0.
  size_t length_{};
};

extern Cmdline gCmdline;

#endif  // ZIRCON_KERNEL_LIB_CMDLINE_INCLUDE_LIB_CMDLINE_H_
