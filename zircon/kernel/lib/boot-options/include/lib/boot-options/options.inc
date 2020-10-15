// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file is #include'd multiple times with the DEFINE_OPTION macro defined.
//
// #define DEFINE_OPTION(name, type, member, {init}, docstring) ...
// #include "options.inc"
// #undef DEFINE_OPTION
//
// See boot-options.h for admonitions about what kinds of types can be used, as
// well as test-optons.inc for basic examples.

// RedactedHex parses an arbitrary-length (but bounded like SmallString) string
// of ASCII hex digits, and then overwrites those digits in the original
// command line text in the ZBI's physical memory so the entropy_mixin string
// in the BootOptions struct is the only place that has those bits.
DEFINE_OPTION("kernel.entropy-mixin", RedactedHex, entropy_mixin, {}, R"""(
Provides entropy to be mixed into the kernel's CPRNG.  The value must be a
string of lowercase hexadecimal digits.

The original value will be scrubbed from memory as soon as possible and will be
redacted from all diagnostic output.
)""")

// x86-specific options are included here for all the kernel places.
// In the generator program, they're included separately.
#if defined(__x86_64__) && !BOOT_OPTIONS_GENERATOR
#include "x86.inc"
#endif

#if BOOT_OPTIONS_TESTONLY_OPTIONS
#include "test-options.inc"
#endif
