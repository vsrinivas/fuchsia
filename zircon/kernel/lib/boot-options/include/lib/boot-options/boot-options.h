// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_BOOT_OPTIONS_H_
#define ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_BOOT_OPTIONS_H_

// TODO(fxbug.dev/32926): Once possible, only define this for test code.
// In any case, we do not want to include tests options when generating
// boot option documentation.
#define BOOT_OPTIONS_TESTONLY_OPTIONS !BOOT_OPTIONS_GENERATOR

#include <stdio.h>
#include <zircon/assert.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#if BOOT_OPTIONS_TESTONLY_OPTIONS
#include "test-types.h"
#endif
#include "types.h"

struct BootOptions;  // Declared below.

// This points to the only instance of BootOptions that ever exists outside
// test code.  It's allocated in reserved physical memory by physboot and then
// handed off to the kernel proper.  A global by this name exists both in
// physboot with the physical address pointer and in the kernel with the
// virtual address pointer.
extern BootOptions* gBootOptions;

struct BootOptions {
  // General string values get this done to each character.
  static constexpr char SanitizeChar(char c) {
    switch (c) {
      // Only printable ASCII characters come through as is.
      case ' ' ... '~':
        return c;

        // Other whitespace chars become ' '.
      case '\n':
      case '\r':
      case '\t':
        return ' ';

        // Anything else becomes '.'.
      default:
        return '.';
    }
  }

  // Return the length in the out buffer filled with the sanitized contents of
  // the input string.
  static size_t SanitizeString(char* out, size_t out_size, std::string_view in) {
    ZX_ASSERT(out_size >= in.size());
    return std::transform(in.begin(), in.end(), out, SanitizeChar) - out;
  }

  // This modifies the string in place even though it's const.
  static void Redact(std::string_view string) {
    constexpr char kRedacted = 'x';
    char* begin = const_cast<char*>(&string[0]);
    char* end = begin + string.size();
    std::fill(begin, end, kRedacted);
  }

  // Parse a string to an integer in C syntax.
  static std::optional<int64_t> ParseInt(std::string_view);

  // Split the command line into words and parse each one as an option.  This
  // can be called multiple times with separate command line fragments.  Each
  // word is processed in order and sets its corresponding member in this
  // struct, replacing any earlier option argument or the initial default.
  // If complain is set, print messages there if a key is not recognized.
  void SetMany(std::string_view cmdline, FILE* complain = nullptr);

  // Display the key, its value, and its default.
  int Show(std::string_view key, bool defaults = true, FILE* out = stdout);

  // Display all keys, values, and defaults.
  void Show(bool defaults = true, FILE* out = stdout);

  // Write out "key=value".
  template <typename T>
  static void Print(std::string_view key, const T& value, FILE* out = stdout) {
    fprintf(out, "%.*s=", static_cast<int>(key.size()), key.data());
    PrintValue(value, out);
  }

  // Overloads parse and print values of various types.
  // If the string can't be parsed in Parse, the member is not written.
#define OPTION_TYPE(T)                                  \
  void Parse(std::string_view, T BootOptions::*member); \
  static void PrintValue(const T& value, FILE* out = stdout)

  OPTION_TYPE(bool);
  OPTION_TYPE(uint64_t);
  OPTION_TYPE(uint32_t);
  OPTION_TYPE(SmallString);
  OPTION_TYPE(RedactedHex);
#if BOOT_OPTIONS_TESTONLY_OPTIONS
  OPTION_TYPE(TestEnum);
  OPTION_TYPE(TestStruct);
#endif

#undef OPTION_TYPE

  struct WordResult {
    std::string_view key;
    bool known = false;
  };

  // Returns true if the command-line word matches a known "key=value" or "key"
  // string.  If the value was parsable for the type of the key's member, then
  // the member was updated, else it wasn't touched (but still returns true).
  WordResult ParseWord(std::string_view);

#define DEFINE_OPTION(name, type, member, init, doc) type member init;
#include "options.inc"
#if BOOT_OPTIONS_GENERATOR
#include "x86.inc"
#endif
#undef DEFINE_OPTION
};

#endif  // ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_BOOT_OPTIONS_H_
