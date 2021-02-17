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

DEFINE_OPTION("kernel.jitterentropy.bs", uint32_t, jitterentropy_bs, {64}, R"""(
Sets the "memory block size" parameter for jitterentropy. When jitterentropy is
performing memory operations (to increase variation in CPU timing), the memory
will be accessed in blocks of this size.
)""")

DEFINE_OPTION("kernel.jitterentropy.bc", uint32_t, jitterentropy_bc, {512}, R"""(
Sets the "memory block count" parameter for jitterentropy. When jitterentropy
is performing memory operations (to increase variation in CPU timing), this
controls how many blocks (of size `kernel.jitterentropy.bs`) are accessed.
)""")

DEFINE_OPTION("kernel.jitterentropy.ml", uint32_t, jitterentropy_ml, {32}, R"""(
Sets the "memory loops" parameter for jitterentropy. When jitterentropy is
performing memory operations (to increase variation in CPU timing), this
controls how many times the memory access routine is repeated. This parameter
is only used when `kernel.jitterentropy.raw` is true. If the value of this
parameter is `0` or if `kernel.jitterentropy.raw` is `false`, then
jitterentropy chooses the number of loops is a random-ish way.
)""")

DEFINE_OPTION("kernel.jitterentropy.ll", uint32_t, jitterentropy_ll, {1}, R"""(
Sets the "LFSR loops" parameter for jitterentropy (the default is 1). When
jitterentropy is performing CPU-intensive LFSR operations (to increase variation
in CPU timing), this controls how many times the LFSR routine is repeated.  This
parameter is only used when `kernel.jitterentropy.raw` is true. If the value of
this parameter is `0` or if `kernel.jitterentropy.raw` is `false`, then
jitterentropy chooses the number of loops is a random-ish way.
)""")

DEFINE_OPTION("kernel.jitterentropy.raw", bool, jitterentropy_raw, {true}, R"""(
When true (the default), the jitterentropy entropy collector will return raw,
unprocessed samples. When false, the raw samples will be processed by
jitterentropy, producing output data that looks closer to uniformly random. Note
that even when set to false, the CPRNG will re-process the samples, so the
processing inside of jitterentropy is somewhat redundant.
)""")

DEFINE_OPTION("kernel.serial", uart::all::Driver, serial, {}, R"""(
TODO(53594)
)""")

// Machine-specific options are included here for all the kernel places.
// In the generator program, they're included separately.

#if defined(__x86_64__) && !BOOT_OPTIONS_GENERATOR
#include "x86.inc"
#elif defined(__aarch64__) && !BOOT_OPTIONS_GENERATOR
#include "arm64.inc"
#endif

#if BOOT_OPTIONS_TESTONLY_OPTIONS
#include "test-options.inc"
#endif
