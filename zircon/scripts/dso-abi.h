// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file defines macros to handle the output of the shlib-symbols
// script (run without the -a switch).  These macros generate assembly
// code that should define symbols in a DSO such that the resultant DSO
// has the same ABI as the DSO input to shlib-symbols.

#define FUNCTION(NAME, SIZE) FUNCTION_1(global, NAME)
#define WEAK_FUNCTION(NAME, SIZE) FUNCTION_1(weak, NAME)

#define FUNCTION_1(BINDING, NAME)      \
  .pushsection.text, "ax", % progbits; \
  .BINDING NAME;                       \
  .type NAME, % function;              \
  NAME:                                \
  .space 1;                            \
  .popsection

#define OBJECT_1(SECTION, SECFLAGS, SECTYPE, BINDING, NAME, SIZE) \
  .pushsection SECTION, SECFLAGS, % SECTYPE;                      \
  .BINDING NAME;                                                  \
  .type NAME, % object;                                           \
  NAME:                                                           \
  .space SIZE;                                                    \
  .size NAME, SIZE;                                               \
  .popsection

#define RODATA_OBJECT(NAME, SIZE) OBJECT_1(.rodata, "a", progbits, global, NAME, SIZE)
#define DATA_OBJECT(NAME, SIZE) OBJECT_1(.data, "aw", progbits, global, NAME, SIZE)
#define WEAK_DATA_OBJECT(NAME, SIZE) OBJECT_1(.data, "aw", progbits, weak, NAME, SIZE)
#define BSS_OBJECT(NAME, SIZE) OBJECT_1(.bss, "aw", nobits, global, NAME, SIZE)

#define UNDEFINED_WEAK(NAME, SIZE) UNDEFINED_1(weak, NAME)
#define UNDEFINED(NAME, SIZE) UNDEFINED_1(globl, NAME)
#define UNDEFINED_1(BINDING, NAME)          \
  .pushsection.undefined, "aw", % progbits; \
  .BINDING NAME;                            \
  .dc.a NAME;                               \
  .popsection
