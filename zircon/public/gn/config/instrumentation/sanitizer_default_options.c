// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This code is used by targets generated in sanitizer_default_options.gni.
//
// It defines three macros:
//
// * DEFINE_SANITIZER_DEFAULT_OPTIONS: true or false
// * SANITIZER_DEFAULT_OPTIONS_NAME: an identifier, e.g. `asan_default_options`
// * SANITIZER_DEFAULT_OPTIONS_STRING: a string literal

#include <stdalign.h>
#include <stdbool.h>
#include <string.h>

// This generates char arrays in two special sections whose names are derived
// from the SANITIZER_DEFAULT_OPTIONS_NAME, e.g. `asan_default_options` yields:
// `asan_default_options_strings` and `asan_default_options_buffer`.
// `_strings` is a read-only section concatenating NUL-terminated strings
// together.  `_buffers` is a writable/zero-fill section of the same name.
//
// The `_strings` section collects all the options injected from the build
// system via sanitizer_default_options() or sanitizer_extra_options() targets.
// These are concatenated in link order, which is dependency post-order in the
// build system: dependencies precede their dependents.  The first value seen
// for each option in this order is the one that should be used.  Individual
// sanitizer_extra_options() targets depend on the sanitizer_default_options()
// target for the variant so their settings will take precedence over those in
// any GN build argument.
//
// Unfortunately, this link-time order is the reverse of the order the final
// single string of options needs to be in.  The sanitizer runtime parses the
// options in order, with the last setting for each option overriding any
// earlier ones.  There is no good way to reorder these things at link time so
// everything can stay in read-only data with no startup work.  So, the
// callback function defined here copies the strings collected at link time
// into a (static) runtime buffer to reverse the order, and join the separate
// NUL-terminated strings into a single ':'-separated string.

#define PASTE(a, b, c) PASTE1(a, b, c)
#define PASTE1(a, b, c) a##b##c

#define START_STRINGS PASTE(__start_, SANITIZER_DEFAULT_OPTIONS_NAME, _strings)
#define STOP_STRINGS PASTE(__stop_, SANITIZER_DEFAULT_OPTIONS_NAME, _strings)
#define START_BUF PASTE(__start_, SANITIZER_DEFAULT_OPTIONS_NAME, _buffer)
#define STOP_BUF PASTE(__stop_, SANITIZER_DEFAULT_OPTIONS_NAME, _buffer)
#define RUNTIME_CALLBACK PASTE(__, , SANITIZER_DEFAULT_OPTIONS_NAME)

#define STRING(a, b) STRING1(a, b)
#define STRING1(a, b) #a #b

#define IN_SECTION(scn_sfx) \
  __attribute__((section(STRING(SANITIZER_DEFAULT_OPTIONS_NAME, scn_sfx))))

// Explicit alignas prevents the compiler from "optimizing" with extra padding.
// Likewise, asan red zones would pollute the special section.
#define DEFINE_IN_SECTION(scn_sfx) \
  __attribute__((used, no_sanitize_address)) IN_SECTION(scn_sfx) alignas(char)

// The linker-generated start/stop symbols are always private to this same
// module and don't need GOT indirection.
#define HIDDEN_IN_SECTION(scn_sfx) __attribute__((visibility("hidden"))) IN_SECTION(scn_sfx)

// This contributes a string to the `*_strings` section.
// These collect in link order: first one wins.
DEFINE_IN_SECTION(_strings) static const char kDefaultOptions[] = SANITIZER_DEFAULT_OPTIONS_STRING;

// This contributes buffer space needed to cover the string added above.
// This particular space doesn't correspond to that string, only its size.
DEFINE_IN_SECTION(_buffer) static char gBufferSpace[sizeof(SANITIZER_DEFAULT_OPTIONS_STRING)];

// This is true for the main sanitizer_default_options() target that defines
// the callback.  This same source file is also compiled with this false for
// sanitizer_extra_options() targets (and dependency sanitizer_extra_options()
// targets), which just contribute their strings and buffer space to the
// special sections.
#if DEFINE_SANITIZER_DEFAULT_OPTIONS

// These are defined implicitly by the linker to point at the beginning and end
// of each special section.  If there are any sanitizer_extra_options() targets
// in the link, they contribute here first, with kDefaultOptions at the end.
HIDDEN_IN_SECTION(_strings) extern const char START_STRINGS[];
HIDDEN_IN_SECTION(_strings) extern const char STOP_STRINGS[];
HIDDEN_IN_SECTION(_buffer) extern char START_BUF[];
HIDDEN_IN_SECTION(_buffer) extern char STOP_BUF[];

// The runtime calls this to return the string.
const char* RUNTIME_CALLBACK(void) {
  // The strings collect in link order, where the first one should win.  But
  // the options in the final unified string are applied successively, so the
  // last one wins.  Move forward through the strings and copy them into the
  // buffer in reverse order, separated by ':'.
  const char* in = START_STRINGS;
  char* out = STOP_BUF;
  while (in < STOP_STRINGS) {
    size_t len = strlen(in);
    out = memcpy(out - len - 1, in, len);
    out[len] = ':';
    in += len + 1;
  }
  // Undo the last store from the first iteration so the string is terminated.
  STOP_BUF[-1] = '\0';
  return out;
}

#endif  // DEFINE_SANITIZER_DEFAULT_OPTIONS
