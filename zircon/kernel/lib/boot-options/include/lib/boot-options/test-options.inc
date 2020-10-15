// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// Serves as documentation for types usable in options.inc
// Any type backing a non-test option should have an option here to exercise it
// in tests.

// The basic integer and bool types are what most options use.
DEFINE_OPTION("test.option.uint32", uint32_t, test_uint32, {123}, R"""(
Test of a 32-bit integer boot option.  Not used by the kernel.
)""")

// This declares a member `bool test_uint64{456};`, i.e. init must use braces.
DEFINE_OPTION("test.option.uint64", uint64_t, test_uint64, {456}, R"""(
Test of a 64-bit integer boot option.  Not used by the kernel.
)""")

// A bool is parsed as true for mere presence, and as false only if it exactly
// matches "0", "false", or "off".
DEFINE_OPTION("test.option.bool", bool, test_bool, {false}, R"""(
Test of a Boolean boot option.  Not used by the kernel.
)""")

// This is just a C string of fixed maximum size.
DEFINE_OPTION("test.option.smallstring", SmallString, test_smallstring, {"test-default-value"},
              R"""(
Test of a string-valued boot option.  Not used by the kernel.
)""")

// A multiple-choice option type can be defined in boot-options.h with a
// simple Enum<Name> specialization defined in the parser/printer code.  Note
// that this doesn't technically have to be an enum, just whatever copyable
// type comparable for equality in `.Case("name", <value-expression>)` calls.
DEFINE_OPTION("test.option.enum", TestEnum, test_enum, {TestEnum::kDefault},
              R"""(
Test of a multiple-choice boot option.  Not used by the kernel.
)""")

// Special cases can have hand-written explicit parsing and printing functions.
DEFINE_OPTION("test.option.struct", TestStruct, test_struct, {}, R"""(
Test of a special-case boot option.  Not used by the kernel.
The value must be exactly "test" or "{}", or a warning will be printed.
)""")


DEFINE_OPTION("test.option.redacted_hex", RedactedHex, test_redacted_hex, {}, R"""(
Test of an option backed by RedactedHex.  Not used by the kernel.
)""")
